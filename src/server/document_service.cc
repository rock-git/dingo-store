// Copyright (c) 2023 dingodb.com, Inc. All Rights Reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "server/document_service.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "butil/compiler_specific.h"
#include "butil/status.h"
#include "common/constant.h"
#include "common/context.h"
#include "common/helper.h"
#include "common/logging.h"
#include "common/synchronization.h"
#include "common/version.h"
#include "document/codec.h"
#include "engine/storage.h"
#include "fmt/core.h"
#include "gflags/gflags.h"
#include "meta/store_meta_manager.h"
#include "proto/common.pb.h"
#include "proto/coordinator.pb.h"
#include "proto/error.pb.h"
#include "proto/index.pb.h"
#include "proto/store.pb.h"
#include "server/server.h"
#include "server/service_helper.h"

using dingodb::pb::error::Errno;

DECLARE_int32(raft_apply_worker_max_pending_num);

namespace dingodb {

DEFINE_int64(document_max_batch_count, 1024, "document max batch count in one request");
DEFINE_int64(document_max_request_size, 8388608, "document max batch count in one request");
DEFINE_bool(enable_async_document_search, true, "enable async vector search");
DEFINE_bool(enable_async_document_count, true, "enable async vector count");
DEFINE_bool(enable_async_document_operation, true, "enable async vector operation");

extern bvar::LatencyRecorder g_txn_latches_recorder;

static void IndexRpcDone(BthreadCond* cond) { cond->DecreaseSignal(); }

DECLARE_int64(max_prewrite_count);
DECLARE_int64(max_scan_lock_limit);
DECLARE_int64(vector_max_background_task_count);

DECLARE_bool(dingo_log_switch_scalar_speed_up_detail);

DocumentServiceImpl::DocumentServiceImpl() = default;

bool DocumentServiceImpl::IsRaftApplyPendingExceed() {
  if (BAIDU_UNLIKELY(raft_apply_worker_set_ != nullptr && FLAGS_raft_apply_worker_max_pending_num > 0 &&
                     raft_apply_worker_set_->PendingTaskCount() > FLAGS_raft_apply_worker_max_pending_num)) {
    DINGO_LOG(WARNING) << "raft apply worker pending task count " << raft_apply_worker_set_->PendingTaskCount()
                       << " is greater than " << FLAGS_raft_apply_worker_max_pending_num;
    return true;
  } else {
    return false;
  }
}

bool DocumentServiceImpl::IsBackgroundPendingTaskCountExceed() {
  return vector_index_manager_->GetBackgroundPendingTaskCount() > FLAGS_vector_max_background_task_count;
}

static butil::Status ValidateDocumentBatchQueryRequest(StoragePtr storage,
                                                       const pb::document::DocumentBatchQueryRequest* request,
                                                       store::RegionPtr region) {
  auto status = ServiceHelper::ValidateRegionEpoch(request->context().region_epoch(), region);
  if (!status.ok()) {
    return status;
  }

  if (request->context().region_id() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "Param region_id is error");
  }

  if (request->document_ids().empty()) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "Param vector_ids is error");
  }

  if (request->document_ids().size() > FLAGS_document_max_batch_count) {
    return butil::Status(pb::error::EDOCUMENT_EXCEED_MAX_BATCH_COUNT,
                         fmt::format("Param vector_ids size {} is exceed max batch count {}",
                                     request->document_ids().size(), FLAGS_document_max_batch_count));
  }

  status = storage->ValidateLeader(region);
  if (!status.ok()) {
    return status;
  }

  return ServiceHelper::ValidateIndexRegion(region, Helper::PbRepeatedToVector(request->document_ids()));
}

void DoDocumentBatchQuery(StoragePtr storage, google::protobuf::RpcController* controller,
                          const pb::document::DocumentBatchQueryRequest* request,
                          pb::document::DocumentBatchQueryResponse* response, TrackClosure* done) {
  brpc::Controller* cntl = (brpc::Controller*)controller;
  brpc::ClosureGuard done_guard(done);
  auto tracker = done->Tracker();
  tracker->SetServiceQueueWaitTime();

  int64_t region_id = request->context().region_id();
  auto region = Server::GetInstance().GetRegion(region_id);
  if (region == nullptr) {
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREGION_NOT_FOUND,
                            fmt::format("Not found region {} at server {}", region_id, Server::GetInstance().Id()));
    return;
  }

  butil::Status status = ValidateDocumentBatchQueryRequest(storage, request, region);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());
    ServiceHelper::GetStoreRegionInfo(region, response->mutable_error());
    return;
  }

  auto ctx = std::make_shared<Engine::DocumentReader::Context>();
  ctx->partition_id = region->PartitionId();
  ctx->region_id = region->Id();
  ctx->region_range = region->Range();
  ctx->document_ids = Helper::PbRepeatedToVector(request->document_ids());
  ctx->selected_scalar_keys = Helper::PbRepeatedToVector(request->selected_keys());
  ctx->with_scalar_data = !request->without_scalar_data();
  ctx->raw_engine_type = region->GetRawEngineType();
  ctx->store_engine_type = region->GetStoreEngineType();

  std::vector<pb::common::DocumentWithId> document_with_ids;
  status = storage->DocumentBatchQuery(ctx, document_with_ids);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());
    return;
  }

  for (auto& document_with_id : document_with_ids) {
    response->add_doucments()->Swap(&document_with_id);
  }
}

void DocumentServiceImpl::DocumentBatchQuery(google::protobuf::RpcController* controller,
                                             const pb::document::DocumentBatchQueryRequest* request,
                                             pb::document::DocumentBatchQueryResponse* response,
                                             google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  if (!FLAGS_enable_async_document_operation) {
    return DoDocumentBatchQuery(storage_, controller, request, response, svr_done);
  }

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoDocumentBatchQuery(storage_, controller, request, response, svr_done);
  });
  bool ret = read_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

static butil::Status ValidateDocumentSearchRequest(StoragePtr storage,
                                                   const pb::document::DocumentSearchRequest* request,
                                                   store::RegionPtr region) {
  if (region == nullptr) {
    return butil::Status(
        pb::error::EREGION_NOT_FOUND,
        fmt::format("Not found region {} at server {}", request->context().region_id(), Server::GetInstance().Id()));
  }

  auto status = ServiceHelper::ValidateRegionEpoch(request->context().region_epoch(), region);
  if (!status.ok()) {
    return status;
  }

  if (request->context().region_id() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "Param region_id is error");
  }

  if (request->parameter().top_n() > FLAGS_document_max_batch_count) {
    return butil::Status(pb::error::EDOCUMENT_EXCEED_MAX_BATCH_COUNT,
                         fmt::format("Param top_n {} is exceed max batch count {}", request->parameter().top_n(),
                                     FLAGS_document_max_batch_count));
  }

  // we limit the max request size to 4M and max batch count to 1024
  // for response, the limit is 10 times of request, which may be 40M
  // this size is less than the default max message size 64M
  if (request->parameter().top_n() * request->document_with_ids_size() > FLAGS_document_max_batch_count * 10) {
    return butil::Status(
        pb::error::EDOCUMENT_EXCEED_MAX_BATCH_COUNT,
        fmt::format("Param top_n {} * document_with_ids_size {} is exceed max batch count {} * 10",
                    request->parameter().top_n(), request->document_with_ids_size(), FLAGS_document_max_batch_count));
  }

  if (request->parameter().top_n() < 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "Param top_n is error");
  }

  if (request->document_with_ids().empty()) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "Param document_with_ids is empty");
  }

  status = storage->ValidateLeader(region);
  if (!status.ok()) {
    return status;
  }

  if (!region->VectorIndexWrapper()->IsReady()) {
    if (region->VectorIndexWrapper()->IsBuildError()) {
      return butil::Status(pb::error::EDOCUMENT_INDEX_BUILD_ERROR,
                           fmt::format("Vector index {} build error, please wait for recover.", region->Id()));
    }
    return butil::Status(pb::error::EDOCUMENT_INDEX_NOT_READY,
                         fmt::format("Vector index {} not ready, please retry.", region->Id()));
  }

  std::vector<int64_t> vector_ids;
  if (request->document_with_ids_size() <= 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "Param document_with_ids is empty");
  } else {
    for (const auto& vector : request->document_with_ids()) {
      if (vector.id() > 0) {
        vector_ids.push_back(vector.id());
      }
    }
  }

  return ServiceHelper::ValidateIndexRegion(region, vector_ids);
}

void DoDocumentSearch(StoragePtr storage, google::protobuf::RpcController* controller,
                      const pb::document::DocumentSearchRequest* request,
                      pb::document::DocumentSearchResponse* response, TrackClosure* done) {
  brpc::Controller* cntl = (brpc::Controller*)controller;
  brpc::ClosureGuard done_guard(done);
  auto tracker = done->Tracker();
  tracker->SetServiceQueueWaitTime();

  int64_t region_id = request->context().region_id();
  auto region = Server::GetInstance().GetRegion(region_id);
  if (region == nullptr) {
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREGION_NOT_FOUND,
                            fmt::format("Not found region {} at server {}", region_id, Server::GetInstance().Id()));
    return;
  }

  butil::Status status = ValidateDocumentSearchRequest(storage, request, region);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());
    ServiceHelper::GetStoreRegionInfo(region, response->mutable_error());
    return;
  }
  if (request->parameter().top_n() == 0) {
    return;
  }

  auto* mut_request = const_cast<pb::document::DocumentSearchRequest*>(request);
  auto ctx = std::make_shared<Engine::DocumentReader::Context>();
  ctx->partition_id = region->PartitionId();
  ctx->region_id = region->Id();
  ctx->document_index = region->DocumentIndexWrapper();
  ctx->region_range = region->Range();
  ctx->parameter.Swap(mut_request->mutable_parameter());
  ctx->raw_engine_type = region->GetRawEngineType();
  ctx->store_engine_type = region->GetStoreEngineType();

  auto scalar_schema = region->ScalarSchema();
  DINGO_LOG_IF(INFO, FLAGS_dingo_log_switch_scalar_speed_up_detail)
      << fmt::format("vector search scalar schema: {}", scalar_schema.ShortDebugString());
  if (0 != scalar_schema.fields_size()) {
    ctx->scalar_schema = scalar_schema;
  }

  if (request->document_with_ids_size() <= 0) {
    auto* err = response->mutable_error();
    err->set_errcode(pb::error::EILLEGAL_PARAMTETERS);
    err->set_errmsg("Param document_with_ids is empty");
    return;
  } else {
    for (const auto& document : request->document_with_ids()) {
      ctx->document_with_ids.push_back(document);
    }
  }

  std::vector<pb::document::DocumentWithScoreResult> document_results;
  status = storage->DocumentBatchSearch(ctx, document_results);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());
    return;
  }

  for (auto& document_result : document_results) {
    *(response->add_batch_results()) = document_result;
  }
}

void DocumentServiceImpl::DocumentSearch(google::protobuf::RpcController* controller,
                                         const pb::document::DocumentSearchRequest* request,
                                         pb::document::DocumentSearchResponse* response,
                                         google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  if (!FLAGS_enable_async_document_search) {
    return DoDocumentSearch(storage_, controller, request, response, svr_done);
  }

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoDocumentSearch(storage_, controller, request, response, svr_done);
  });
  bool ret = read_worker_set_->ExecuteLeastQueue(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

static butil::Status ValidateDocumentAddRequest(StoragePtr storage, const pb::document::DocumentAddRequest* request,
                                                store::RegionPtr region) {
  auto status = ServiceHelper::ValidateRegionEpoch(request->context().region_epoch(), region);
  if (!status.ok()) {
    return status;
  }

  if (request->context().region_id() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "Param region_id is error");
  }

  if (request->documents().empty()) {
    return butil::Status(pb::error::EDOCUMENT_EMPTY, "Document quantity is empty");
  }

  if (request->documents_size() > FLAGS_document_max_batch_count) {
    return butil::Status(pb::error::EDOCUMENT_EXCEED_MAX_BATCH_COUNT,
                         fmt::format("Param documents size {} is exceed max batch count {}", request->documents_size(),
                                     FLAGS_document_max_batch_count));
  }

  if (request->ByteSizeLong() > FLAGS_document_max_request_size) {
    return butil::Status(pb::error::EDOCUMENT_EXCEED_MAX_REQUEST_SIZE,
                         fmt::format("Param documents size {} is exceed max batch size {}", request->ByteSizeLong(),
                                     FLAGS_document_max_request_size));
  }

  status = storage->ValidateLeader(region);
  if (!status.ok()) {
    return status;
  }

  auto document_index_wrapper = region->DocumentIndexWrapper();
  if (!document_index_wrapper->IsReady()) {
    if (region->DocumentIndexWrapper()->IsBuildError()) {
      return butil::Status(pb::error::EDOCUMENT_INDEX_BUILD_ERROR,
                           fmt::format("Vector index {} build error, please wait for recover.", region->Id()));
    }
    return butil::Status(pb::error::EDOCUMENT_INDEX_NOT_READY,
                         fmt::format("Vector index {} not ready, please retry.", region->Id()));
  }

  for (const auto& document : request->documents()) {
    if (BAIDU_UNLIKELY(!DocumentCodec::IsLegalDocumentId(document.id()))) {
      return butil::Status(pb::error::EILLEGAL_PARAMTETERS,
                           "Param vector id is not allowed to be zero, INT64_MAX or negative");
    }
  }

  status = ServiceHelper::ValidateClusterReadOnly();
  if (!status.ok()) {
    return status;
  }

  std::vector<int64_t> documents_ids;
  for (const auto& vector : request->documents()) {
    documents_ids.push_back(vector.id());
  }

  return ServiceHelper::ValidateIndexRegion(region, documents_ids);
}

void DoDocumentAdd(StoragePtr storage, google::protobuf::RpcController* controller,
                   const pb::document::DocumentAddRequest* request, pb::document::DocumentAddResponse* response,
                   TrackClosure* done, bool is_sync) {
  brpc::Controller* cntl = (brpc::Controller*)controller;
  brpc::ClosureGuard done_guard(done);
  auto tracker = done->Tracker();
  tracker->SetServiceQueueWaitTime();

  int64_t region_id = request->context().region_id();
  auto region = Server::GetInstance().GetRegion(region_id);
  if (region == nullptr) {
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREGION_NOT_FOUND,
                            fmt::format("Not found region {} at server {}", region_id, Server::GetInstance().Id()));
    return;
  }

  auto status = ValidateDocumentAddRequest(storage, request, region);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());
    ServiceHelper::GetStoreRegionInfo(region, response->mutable_error());
    return;
  }

  auto ctx = std::make_shared<Context>(cntl, is_sync ? nullptr : done_guard.release(), request, response);
  ctx->SetRegionId(request->context().region_id());
  ctx->SetTracker(tracker);
  ctx->SetCfName(Constant::kStoreDataCF);
  ctx->SetRegionEpoch(request->context().region_epoch());
  ctx->SetRawEngineType(region->GetRawEngineType());
  ctx->SetStoreEngineType(region->GetStoreEngineType());

  std::vector<pb::common::DocumentWithId> documents;
  for (const auto& document : request->documents()) {
    documents.push_back(document);
  }

  status = storage->DocumentAdd(ctx, is_sync, documents);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());

    if (!is_sync) done->Run();
  }
}

void DocumentServiceImpl::DocumentAdd(google::protobuf::RpcController* controller,
                                      const pb::document::DocumentAddRequest* request,
                                      pb::document::DocumentAddResponse* response, google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  if (IsRaftApplyPendingExceed()) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "Raft apply queue is full, please wait and retry, please wait and retry");
    return;
  }

  if (IsBackgroundPendingTaskCountExceed()) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "Background pending task count is full, please wait and retry");
    return;
  }

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoDocumentAdd(storage_, controller, request, response, svr_done, true);
  });
  bool ret = write_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

static butil::Status ValidateDocumentDeleteRequest(StoragePtr storage,
                                                   const pb::document::DocumentDeleteRequest* request,
                                                   store::RegionPtr region) {
  auto status = ServiceHelper::ValidateRegionEpoch(request->context().region_epoch(), region);
  if (!status.ok()) {
    return status;
  }

  if (request->context().region_id() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "Param region_id is error");
  }

  if (request->ids().empty()) {
    return butil::Status(pb::error::EDOCUMENT_EMPTY, "Document id quantity is empty");
  }

  if (request->ids_size() > FLAGS_document_max_batch_count) {
    return butil::Status(pb::error::EDOCUMENT_EXCEED_MAX_BATCH_COUNT,
                         fmt::format("Param ids size {} is exceed max batch count {}", request->ids_size(),
                                     FLAGS_document_max_batch_count));
  }

  status = storage->ValidateLeader(region);
  if (!status.ok()) {
    return status;
  }

  auto vector_index_wrapper = region->VectorIndexWrapper();
  if (!vector_index_wrapper->IsReady()) {
    if (region->VectorIndexWrapper()->IsBuildError()) {
      return butil::Status(pb::error::EDOCUMENT_INDEX_BUILD_ERROR,
                           fmt::format("Vector index {} build error, please wait for recover.", region->Id()));
    }
    return butil::Status(pb::error::EDOCUMENT_INDEX_NOT_READY,
                         fmt::format("Vector index {} not ready, please retry.", region->Id()));
  }

  return ServiceHelper::ValidateIndexRegion(region, Helper::PbRepeatedToVector(request->ids()));
}

void DoDocumentDelete(StoragePtr storage, google::protobuf::RpcController* controller,
                      const pb::document::DocumentDeleteRequest* request,
                      pb::document::DocumentDeleteResponse* response, TrackClosure* done, bool is_sync) {
  brpc::Controller* cntl = (brpc::Controller*)controller;
  brpc::ClosureGuard done_guard(done);
  auto tracker = done->Tracker();
  tracker->SetServiceQueueWaitTime();

  int64_t region_id = request->context().region_id();
  auto region = Server::GetInstance().GetRegion(region_id);
  if (region == nullptr) {
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREGION_NOT_FOUND,
                            fmt::format("Not found region {} at server {}", region_id, Server::GetInstance().Id()));
    return;
  }

  auto status = ValidateDocumentDeleteRequest(storage, request, region);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());
    ServiceHelper::GetStoreRegionInfo(region, response->mutable_error());
    return;
  }

  auto ctx = std::make_shared<Context>(cntl, is_sync ? nullptr : done_guard.release(), request, response);
  ctx->SetRegionId(request->context().region_id());
  ctx->SetTracker(tracker);
  ctx->SetCfName(Constant::kStoreDataCF);
  ctx->SetRegionEpoch(request->context().region_epoch());
  ctx->SetRawEngineType(region->GetRawEngineType());
  ctx->SetStoreEngineType(region->GetStoreEngineType());

  status = storage->DocumentDelete(ctx, is_sync, Helper::PbRepeatedToVector(request->ids()));
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());

    if (!is_sync) done->Run();
  }
}

void DocumentServiceImpl::DocumentDelete(google::protobuf::RpcController* controller,
                                         const pb::document::DocumentDeleteRequest* request,
                                         pb::document::DocumentDeleteResponse* response,
                                         google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  if (IsRaftApplyPendingExceed()) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "Raft apply queue is full, please wait and retry");
    return;
  }

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoDocumentDelete(storage_, controller, request, response, svr_done, true);
  });
  bool ret = write_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

static butil::Status ValidateDocumentGetBorderIdRequest(StoragePtr storage,
                                                        const pb::document::DocumentGetBorderIdRequest* request,
                                                        store::RegionPtr region) {
  auto status = ServiceHelper::ValidateRegionEpoch(request->context().region_epoch(), region);
  if (!status.ok()) {
    return status;
  }

  if (request->context().region_id() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "Param region_id is error");
  }

  status = storage->ValidateLeader(region);
  if (!status.ok()) {
    return status;
  }

  return ServiceHelper::ValidateIndexRegion(region, {});
}

void DoDocumentGetBorderId(StoragePtr storage, google::protobuf::RpcController* controller,
                           const pb::document::DocumentGetBorderIdRequest* request,
                           pb::document::DocumentGetBorderIdResponse* response, TrackClosure* done) {
  brpc::Controller* cntl = (brpc::Controller*)controller;
  brpc::ClosureGuard done_guard(done);
  auto tracker = done->Tracker();
  tracker->SetServiceQueueWaitTime();

  int64_t region_id = request->context().region_id();
  auto region = Server::GetInstance().GetRegion(region_id);
  if (region == nullptr) {
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREGION_NOT_FOUND,
                            fmt::format("Not found region {} at server {}", region_id, Server::GetInstance().Id()));
    return;
  }

  butil::Status status = ValidateDocumentGetBorderIdRequest(storage, request, region);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());
    ServiceHelper::GetStoreRegionInfo(region, response->mutable_error());
    return;
  }
  int64_t vector_id = 0;
  status = storage->DocumentGetBorderId(region, request->get_min(), vector_id);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());

    return;
  }

  response->set_id(vector_id);
}

void DocumentServiceImpl::DocumentGetBorderId(google::protobuf::RpcController* controller,
                                              const pb::document::DocumentGetBorderIdRequest* request,
                                              pb::document::DocumentGetBorderIdResponse* response,
                                              google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  if (!FLAGS_enable_async_document_operation) {
    return DoDocumentGetBorderId(storage_, controller, request, response, svr_done);
  }

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoDocumentGetBorderId(storage_, controller, request, response, svr_done);
  });
  bool ret = read_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

static butil::Status ValidateDocumentScanQueryRequest(StoragePtr storage,
                                                      const pb::document::DocumentScanQueryRequest* request,
                                                      store::RegionPtr region) {
  auto status = ServiceHelper::ValidateRegionEpoch(request->context().region_epoch(), region);
  if (!status.ok()) {
    return status;
  }

  if (request->context().region_id() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "Param region_id is error");
  }

  if (request->document_id_start() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "Param document_id_start is error");
  }

  if (request->max_scan_count() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "Param max_scan_count cant be 0");
  }

  if (request->max_scan_count() > FLAGS_document_max_batch_count) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "Param max_scan_count is bigger than %ld",
                         FLAGS_document_max_batch_count);
  }

  status = storage->ValidateLeader(region);
  if (!status.ok()) {
    return status;
  }

  // for DocumentScanQuery, client can do scan from any id, so we don't need to check vector id
  // sdk will merge, sort, limit_cut of all the results for user.
  return ServiceHelper::ValidateIndexRegion(region, {});
}

void DoDocumentScanQuery(StoragePtr storage, google::protobuf::RpcController* controller,
                         const pb::document::DocumentScanQueryRequest* request,
                         pb::document::DocumentScanQueryResponse* response, TrackClosure* done) {
  brpc::Controller* cntl = (brpc::Controller*)controller;
  brpc::ClosureGuard done_guard(done);
  auto tracker = done->Tracker();
  tracker->SetServiceQueueWaitTime();

  int64_t region_id = request->context().region_id();
  auto region = Server::GetInstance().GetRegion(region_id);
  if (region == nullptr) {
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREGION_NOT_FOUND,
                            fmt::format("Not found region {} at server {}", region_id, Server::GetInstance().Id()));
    return;
  }

  butil::Status status = ValidateDocumentScanQueryRequest(storage, request, region);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());
    ServiceHelper::GetStoreRegionInfo(region, response->mutable_error());
    return;
  }

  auto ctx = std::make_shared<Engine::DocumentReader::Context>();
  ctx->partition_id = region->PartitionId();
  ctx->region_id = region->Id();
  ctx->region_range = region->Range();
  ctx->selected_scalar_keys = Helper::PbRepeatedToVector(request->selected_keys());
  ctx->with_scalar_data = !request->without_scalar_data();
  ctx->start_id = request->document_id_start();
  ctx->end_id = request->document_id_end();
  ctx->is_reverse = request->is_reverse_scan();
  ctx->limit = request->max_scan_count();
  ctx->raw_engine_type = region->GetRawEngineType();
  ctx->store_engine_type = region->GetStoreEngineType();

  std::vector<pb::common::DocumentWithId> document_with_ids;
  status = storage->DocumentScanQuery(ctx, document_with_ids);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());

    return;
  }

  for (auto& document_with_id : document_with_ids) {
    response->add_documents()->Swap(&document_with_id);
  }
}

void DocumentServiceImpl::DocumentScanQuery(google::protobuf::RpcController* controller,
                                            const pb::document::DocumentScanQueryRequest* request,
                                            pb::document::DocumentScanQueryResponse* response,
                                            google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  if (!FLAGS_enable_async_document_operation) {
    return DoDocumentScanQuery(storage_, controller, request, response, svr_done);
  }

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoDocumentScanQuery(storage_, controller, request, response, svr_done);
  });
  bool ret = read_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

static butil::Status ValidateDocumentGetRegionMetricsRequest(
    StoragePtr storage, const pb::document::DocumentGetRegionMetricsRequest* request, store::RegionPtr region) {
  auto status = ServiceHelper::ValidateRegionEpoch(request->context().region_epoch(), region);
  if (!status.ok()) {
    return status;
  }

  if (request->context().region_id() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "Param region_id is error");
  }

  status = storage->ValidateLeader(region);
  if (!status.ok()) {
    return status;
  }

  auto vector_index_wrapper = region->VectorIndexWrapper();
  if (!vector_index_wrapper->IsReady()) {
    if (region->VectorIndexWrapper()->IsBuildError()) {
      return butil::Status(pb::error::EDOCUMENT_INDEX_BUILD_ERROR,
                           fmt::format("Vector index {} build error, please wait for recover.", region->Id()));
    }
    return butil::Status(pb::error::EDOCUMENT_INDEX_NOT_READY,
                         fmt::format("Vector index {} not ready, please retry.", region->Id()));
  }

  return ServiceHelper::ValidateIndexRegion(region, {});
}

void DoDocumentGetRegionMetrics(StoragePtr storage, google::protobuf::RpcController* controller,
                                const pb::document::DocumentGetRegionMetricsRequest* request,
                                pb::document::DocumentGetRegionMetricsResponse* response, TrackClosure* done) {
  brpc::Controller* cntl = (brpc::Controller*)controller;
  brpc::ClosureGuard done_guard(done);
  auto tracker = done->Tracker();
  tracker->SetServiceQueueWaitTime();

  int64_t region_id = request->context().region_id();
  auto region = Server::GetInstance().GetRegion(region_id);
  if (region == nullptr) {
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREGION_NOT_FOUND,
                            fmt::format("Not found region {} at server {}", region_id, Server::GetInstance().Id()));
    return;
  }

  butil::Status status = ValidateDocumentGetRegionMetricsRequest(storage, request, region);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());
    ServiceHelper::GetStoreRegionInfo(region, response->mutable_error());
    return;
  }

  pb::common::DocumentIndexMetrics metrics;
  status = storage->DocumentGetRegionMetrics(region, region->DocumentIndexWrapper(), metrics);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());

    return;
  }

  *(response->mutable_metrics()) = metrics;
}

void DocumentServiceImpl::DocumentGetRegionMetrics(google::protobuf::RpcController* controller,
                                                   const pb::document::DocumentGetRegionMetricsRequest* request,
                                                   pb::document::DocumentGetRegionMetricsResponse* response,
                                                   google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  if (!FLAGS_enable_async_document_operation) {
    return DoDocumentGetRegionMetrics(storage_, controller, request, response, svr_done);
  }

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoDocumentGetRegionMetrics(storage_, controller, request, response, svr_done);
  });
  bool ret = read_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

static butil::Status ValidateDocumentCountRequest(StoragePtr storage, const pb::document::DocumentCountRequest* request,
                                                  store::RegionPtr region) {
  auto status = ServiceHelper::ValidateRegionEpoch(request->context().region_epoch(), region);
  if (!status.ok()) {
    return status;
  }

  if (request->context().region_id() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "Param region_id is error");
  }

  if (request->document_id_start() > request->document_id_end()) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "Param document_id_start/document_id_end range is error");
  }

  status = storage->ValidateLeader(region);
  if (!status.ok()) {
    return status;
  }

  std::vector<int64_t> vector_ids;
  if (request->document_id_start() != 0) {
    vector_ids.push_back(request->document_id_start());
  }
  if (request->document_id_end() != 0) {
    vector_ids.push_back(request->document_id_end() - 1);
  }

  return ServiceHelper::ValidateIndexRegion(region, vector_ids);
}

static pb::common::Range GenCountRange(store::RegionPtr region, int64_t start_vector_id,  // NOLINT
                                       int64_t end_vector_id) {                           // NOLINT
  pb::common::Range range;

  auto region_start_key = region->Range().start_key();
  auto region_part_id = region->PartitionId();
  if (start_vector_id == 0) {
    range.set_start_key(region->Range().start_key());
  } else {
    std::string key;
    DocumentCodec::EncodeDocumentKey(region_start_key[0], region_part_id, start_vector_id, key);
    range.set_start_key(key);
  }

  if (end_vector_id == 0) {
    range.set_end_key(region->Range().end_key());
  } else {
    std::string key;
    DocumentCodec::EncodeDocumentKey(region_start_key[0], region_part_id, end_vector_id, key);
    range.set_end_key(key);
  }

  return range;
}

void DoDocumentCount(StoragePtr storage, google::protobuf::RpcController* controller,
                     const pb::document::DocumentCountRequest* request, pb::document::DocumentCountResponse* response,
                     TrackClosure* done) {
  brpc::Controller* cntl = (brpc::Controller*)controller;
  brpc::ClosureGuard done_guard(done);
  auto tracker = done->Tracker();
  tracker->SetServiceQueueWaitTime();

  int64_t region_id = request->context().region_id();
  auto region = Server::GetInstance().GetRegion(region_id);
  if (region == nullptr) {
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREGION_NOT_FOUND,
                            fmt::format("Not found region {} at server {}", region_id, Server::GetInstance().Id()));
    return;
  }

  butil::Status status = ValidateDocumentCountRequest(storage, request, region);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());
    ServiceHelper::GetStoreRegionInfo(region, response->mutable_error());
    return;
  }

  int64_t count = 0;
  status = storage->DocumentCount(
      region, GenCountRange(region, request->document_id_start(), request->document_id_end()), count);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());

    return;
  }

  response->set_count(count);
}

void DocumentServiceImpl::DocumentCount(google::protobuf::RpcController* controller,
                                        const pb::document::DocumentCountRequest* request,
                                        pb::document::DocumentCountResponse* response,
                                        ::google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  if (!FLAGS_enable_async_document_count) {
    return DoDocumentCount(storage_, controller, request, response, svr_done);
  }

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoDocumentCount(storage_, controller, request, response, svr_done);
  });
  bool ret = read_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

// txn
static butil::Status ValidateTxnGetRequest(const pb::store::TxnGetRequest* request, store::RegionPtr region) {
  // check if region_epoch is match
  auto epoch_ret = ServiceHelper::ValidateRegionEpoch(request->context().region_epoch(), region);
  if (!epoch_ret.ok()) {
    return epoch_ret;
  }

  if (request->key().empty()) {
    return butil::Status(pb::error::EKEY_EMPTY, "Key is empty");
  }

  std::vector<std::string_view> keys = {request->key()};
  auto status = ServiceHelper::ValidateRegion(region, keys);
  if (!status.ok()) {
    return status;
  }

  return butil::Status();
}

void DoTxnGetDocument(StoragePtr storage, google::protobuf::RpcController* controller,
                      const pb::store::TxnGetRequest* request, pb::store::TxnGetResponse* response,
                      TrackClosure* done) {
  brpc::Controller* cntl = (brpc::Controller*)controller;
  brpc::ClosureGuard done_guard(done);
  auto tracker = done->Tracker();
  tracker->SetServiceQueueWaitTime();

  int64_t region_id = request->context().region_id();
  auto region = Server::GetInstance().GetRegion(region_id);
  if (region == nullptr) {
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREGION_NOT_FOUND,
                            fmt::format("Not found region {} at server {}", region_id, Server::GetInstance().Id()));
    return;
  }

  butil::Status status = ValidateTxnGetRequest(request, region);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());
    ServiceHelper::GetStoreRegionInfo(region, response->mutable_error());
    return;
  }

  auto ctx = std::make_shared<Context>();
  ctx->SetRegionId(request->context().region_id());
  ctx->SetTracker(tracker);
  ctx->SetCfName(Constant::kStoreDataCF);
  ctx->SetRegionEpoch(request->context().region_epoch());
  ctx->SetIsolationLevel(request->context().isolation_level());
  ctx->SetRawEngineType(region->GetRawEngineType());
  ctx->SetStoreEngineType(region->GetStoreEngineType());

  std::vector<std::string> keys;
  auto* mut_request = const_cast<pb::store::TxnGetRequest*>(request);
  keys.emplace_back(std::move(*mut_request->release_key()));

  std::set<int64_t> resolved_locks;
  for (const auto& lock : request->context().resolved_locks()) {
    resolved_locks.insert(lock);
  }

  pb::store::TxnResultInfo txn_result_info;

  std::vector<pb::common::KeyValue> kvs;
  status = storage->TxnBatchGet(ctx, request->start_ts(), keys, resolved_locks, txn_result_info, kvs);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());

    return;
  }

  if (!kvs.empty()) {
    for (auto& kv : kvs) {
      pb::common::DocumentWithId document_with_id;

      if (!kv.value().empty()) {
        auto parse_ret = document_with_id.ParseFromString(kv.value());
        if (!parse_ret) {
          auto* err = response->mutable_error();
          err->set_errcode(static_cast<Errno>(pb::error::EINTERNAL));
          err->set_errmsg("parse document_with_id failed");
          return;
        }
      }

      response->mutable_document()->Swap(&document_with_id);
    }
  }
  *response->mutable_txn_result() = txn_result_info;
}

void DocumentServiceImpl::TxnGet(google::protobuf::RpcController* controller, const pb::store::TxnGetRequest* request,
                                 pb::store::TxnGetResponse* response, google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoTxnGetDocument(storage_, controller, request, response, svr_done);
  });
  bool ret = read_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

static butil::Status ValidateTxnScanRequestIndex(const pb::store::TxnScanRequest* request, store::RegionPtr region,
                                                 const pb::common::Range& req_range) {
  // check if limit is valid
  if (request->limit() > FLAGS_document_max_batch_count) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "limit is exceed vector max batch count");
  }

  auto status = ServiceHelper::ValidateRegionEpoch(request->context().region_epoch(), region);
  if (!status.ok()) {
    return status;
  }

  if (region == nullptr) {
    return butil::Status(pb::error::EREGION_NOT_FOUND, "Not found region");
  }

  status = ServiceHelper::ValidateRange(req_range);
  if (!status.ok()) {
    return status;
  }

  status = ServiceHelper::ValidateRangeInRange(region->Range(), req_range);
  if (!status.ok()) {
    return status;
  }

  status = ServiceHelper::ValidateRegionState(region);
  if (!status.ok()) {
    return status;
  }

  return butil::Status();
}

void DoTxnScanDocument(StoragePtr storage, google::protobuf::RpcController* controller,
                       const pb::store::TxnScanRequest* request, pb::store::TxnScanResponse* response,
                       TrackClosure* done) {
  brpc::Controller* cntl = (brpc::Controller*)controller;
  brpc::ClosureGuard done_guard(done);
  auto tracker = done->Tracker();
  tracker->SetServiceQueueWaitTime();

  int64_t region_id = request->context().region_id();
  auto region = Server::GetInstance().GetRegion(region_id);
  if (region == nullptr) {
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREGION_NOT_FOUND,
                            fmt::format("Not found region {} at server {}", region_id, Server::GetInstance().Id()));
    return;
  }

  auto uniform_range = Helper::TransformRangeWithOptions(request->range());
  butil::Status status = ValidateTxnScanRequestIndex(request, region, uniform_range);
  if (!status.ok()) {
    if (pb::error::ERANGE_INVALID == static_cast<pb::error::Errno>(status.error_code())) {
      return;
    }
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());
    ServiceHelper::GetStoreRegionInfo(region, response->mutable_error());
    return;
  }

  auto ctx = std::make_shared<Context>();
  ctx->SetRegionId(request->context().region_id());
  ctx->SetTracker(tracker);
  ctx->SetCfName(Constant::kStoreDataCF);
  ctx->SetRegionEpoch(request->context().region_epoch());
  ctx->SetIsolationLevel(request->context().isolation_level());
  ctx->SetRawEngineType(region->GetRawEngineType());
  ctx->SetStoreEngineType(region->GetStoreEngineType());

  std::set<int64_t> resolved_locks;
  for (const auto& lock : request->context().resolved_locks()) {
    resolved_locks.insert(lock);
  }

  pb::store::TxnResultInfo txn_result_info;
  std::vector<pb::common::KeyValue> kvs;
  bool has_more = false;
  std::string end_key{};

  auto correction_range = Helper::IntersectRange(region->Range(), uniform_range);
  status = storage->TxnScan(ctx, request->start_ts(), correction_range, request->limit(), request->key_only(),
                            request->is_reverse(), resolved_locks, txn_result_info, kvs, has_more, end_key,
                            !request->has_coprocessor(), request->coprocessor());
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());

    return;
  }

  if (!kvs.empty()) {
    for (auto& kv : kvs) {
      pb::common::DocumentWithId document_with_id;

      if (!kv.value().empty()) {
        auto parse_ret = document_with_id.ParseFromString(kv.value());
        if (!parse_ret) {
          auto* err = response->mutable_error();
          err->set_errcode(static_cast<Errno>(pb::error::EINTERNAL));
          err->set_errmsg("parse document_with_id failed");
          return;
        }
      }

      response->add_documents()->Swap(&document_with_id);
    }
  }

  if (txn_result_info.ByteSizeLong() > 0) {
    *response->mutable_txn_result() = txn_result_info;
  }
  response->set_end_key(end_key);
  response->set_has_more(has_more);
}

void DocumentServiceImpl::TxnScan(google::protobuf::RpcController* controller, const pb::store::TxnScanRequest* request,
                                  pb::store::TxnScanResponse* response, google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoTxnScanDocument(storage_, controller, request, response, svr_done);
  });
  bool ret = read_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

butil::Status ValidateTxnPessimisticLockRequest(const dingodb::pb::store::TxnPessimisticLockRequest* request);

void DoTxnPessimisticLock(StoragePtr storage, google::protobuf::RpcController* controller,
                          const dingodb::pb::store::TxnPessimisticLockRequest* request,
                          dingodb::pb::store::TxnPessimisticLockResponse* response, TrackClosure* done, bool is_sync);

void DocumentServiceImpl::TxnPessimisticLock(google::protobuf::RpcController* controller,
                                             const pb::store::TxnPessimisticLockRequest* request,
                                             pb::store::TxnPessimisticLockResponse* response,
                                             google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  if (IsRaftApplyPendingExceed()) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "Raft apply queue is full, please wait and retry");
    return;
  }

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoTxnPessimisticLock(storage_, controller, request, response, svr_done, true);
  });
  bool ret = write_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

butil::Status ValidateTxnPessimisticRollbackRequest(const dingodb::pb::store::TxnPessimisticRollbackRequest* request);

void DoTxnPessimisticRollback(StoragePtr storage, google::protobuf::RpcController* controller,
                              const dingodb::pb::store::TxnPessimisticRollbackRequest* request,
                              dingodb::pb::store::TxnPessimisticRollbackResponse* response, TrackClosure* done,
                              bool is_sync);

void DocumentServiceImpl::TxnPessimisticRollback(google::protobuf::RpcController* controller,
                                                 const pb::store::TxnPessimisticRollbackRequest* request,
                                                 pb::store::TxnPessimisticRollbackResponse* response,
                                                 google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  if (IsRaftApplyPendingExceed()) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "Raft apply queue is full, please wait and retry");
    return;
  }

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoTxnPessimisticRollback(storage_, controller, request, response, svr_done, true);
  });
  bool ret = write_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

static butil::Status ValidateIndexTxnPrewriteRequest(StoragePtr storage, const pb::store::TxnPrewriteRequest* request,
                                                     store::RegionPtr region) {
  // check if region_epoch is match
  auto epoch_ret = ServiceHelper::ValidateRegionEpoch(request->context().region_epoch(), region);
  if (!epoch_ret.ok()) {
    return epoch_ret;
  }

  if (request->mutations_size() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "mutations is empty");
  }

  if (request->mutations_size() > FLAGS_max_prewrite_count) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "mutations size is too large, max=1024");
  }

  if (request->primary_lock().empty()) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "primary_lock is empty");
  }

  if (request->start_ts() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "start_ts is 0");
  }

  if (request->lock_ttl() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "lock_ttl is 0");
  }

  if (request->txn_size() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "txn_size is 0");
  }

  std::vector<std::string_view> keys;
  for (const auto& mutation : request->mutations()) {
    if (BAIDU_UNLIKELY(mutation.key().empty())) {
      return butil::Status(pb::error::EKEY_EMPTY, "key is empty");
    }
    keys.push_back(mutation.key());
  }

  auto status = ServiceHelper::ValidateRegion(region, keys);
  if (!status.ok()) {
    return status;
  }

  if (BAIDU_UNLIKELY(request->mutations_size() > FLAGS_document_max_batch_count)) {
    return butil::Status(pb::error::EDOCUMENT_EXCEED_MAX_BATCH_COUNT,
                         fmt::format("Param documents size {} is exceed max batch count {}", request->mutations_size(),
                                     FLAGS_document_max_batch_count));
  }

  if (BAIDU_UNLIKELY(request->ByteSizeLong() > FLAGS_document_max_request_size)) {
    return butil::Status(pb::error::EDOCUMENT_EXCEED_MAX_REQUEST_SIZE,
                         fmt::format("Param documents size {} is exceed max batch size {}", request->ByteSizeLong(),
                                     FLAGS_document_max_request_size));
  }

  status = storage->ValidateLeader(region);
  if (!status.ok()) {
    return status;
  }

  status = ServiceHelper::ValidateClusterReadOnly();
  if (!status.ok()) {
    return status;
  }

  if (!region->VectorIndexWrapper()->IsReady()) {
    if (region->VectorIndexWrapper()->IsBuildError()) {
      return butil::Status(pb::error::EDOCUMENT_INDEX_BUILD_ERROR,
                           fmt::format("Vector index {} build error, please wait for recover.", region->Id()));
    }
    return butil::Status(pb::error::EDOCUMENT_INDEX_NOT_READY,
                         fmt::format("Vector index {} not ready, please retry.", region->Id()));
  }

  auto vector_index_wrapper = region->VectorIndexWrapper();

  std::vector<int64_t> vector_ids;
  auto dimension = vector_index_wrapper->GetDimension();

  for (const auto& mutation : request->mutations()) {
    // check vector_id is correctly encoded in key of mutation
    int64_t vector_id = DocumentCodec::DecodeDocumentId(mutation.key());

    if (BAIDU_UNLIKELY(!DocumentCodec::IsLegalDocumentId(vector_id))) {
      return butil::Status(pb::error::EILLEGAL_PARAMTETERS,
                           "Param vector id is not allowed to be zero, INT64_MAX or negative, please check the "
                           "vector_id encoded in mutation key");
    }

    vector_ids.push_back(vector_id);

    // check if vector_id is legal
    const auto& vector = mutation.vector();
    if (mutation.op() == pb::store::Op::Put || mutation.op() == pb::store::PutIfAbsent) {
      if (vector_index_wrapper->IsExceedsMaxElements()) {
        return butil::Status(pb::error::EDOCUMENT_INDEX_EXCEED_MAX_ELEMENTS,
                             fmt::format("Vector index {} exceeds max elements.", region->Id()));
      }

      if (BAIDU_UNLIKELY(!DocumentCodec::IsLegalDocumentId(vector_id))) {
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS,
                             "Param  ector id is not allowed to be zero, INT64_MAX or negative, please check the "
                             "vector_id in DocumentWithId");
      }

      if (BAIDU_UNLIKELY(vector.id() != vector_id)) {
        return butil::Status(
            pb::error::EILLEGAL_PARAMTETERS,
            "Param vector id in DocumentWithId is not equal to vector_id in mutation key, please check "
            "the mutation key and DocumentWithId");
      }

      if (BAIDU_UNLIKELY(vector.vector().float_values().empty())) {
        return butil::Status(pb::error::EDOCUMENT_EMPTY, "Vector is empty");
      }

      // check vector dimension
      if (vector_index_wrapper->Type() == pb::common::VectorIndexType::VECTOR_INDEX_TYPE_HNSW ||
          vector_index_wrapper->Type() == pb::common::VectorIndexType::VECTOR_INDEX_TYPE_FLAT ||
          vector_index_wrapper->Type() == pb::common::VectorIndexType::VECTOR_INDEX_TYPE_BRUTEFORCE ||
          vector_index_wrapper->Type() == pb::common::VectorIndexType::VECTOR_INDEX_TYPE_IVF_FLAT ||
          vector_index_wrapper->Type() == pb::common::VectorIndexType::VECTOR_INDEX_TYPE_IVF_PQ) {
        if (BAIDU_UNLIKELY(vector.vector().float_values().size() != dimension)) {
          return butil::Status(
              pb::error::EILLEGAL_PARAMTETERS,
              "Param vector float dimension is error, correct dimension is " + std::to_string(dimension));
        }
      } else {
        if (BAIDU_UNLIKELY(vector.vector().binary_values().size() != dimension)) {
          return butil::Status(
              pb::error::EILLEGAL_PARAMTETERS,
              "Param vector binary dimension is error, correct dimension is " + std::to_string(dimension));
        }
      }

      // TODO: check schema before txn prewrite
      //   auto scalar_schema = region->ScalarSchema();
      //   DINGO_LOG_IF(INFO, FLAGS_dingo_log_switch_scalar_speed_up_detail)
      //       << fmt::format("vector txn prewrite scalar schema: {}", scalar_schema.ShortDebugString());
      //   if (0 != scalar_schema.fields_size()) {
      //     status = VectorIndexUtils::ValidateVectorScalarData(scalar_schema, vector.scalar_data());
      //     if (!status.ok()) {
      //       DINGO_LOG(ERROR) << status.error_cstr();
      //       return status;
      //     }
      //   }

    } else if (mutation.op() == pb::store::Op::Delete || mutation.op() == pb::store::Op::CheckNotExists) {
      if (BAIDU_UNLIKELY(!DocumentCodec::IsLegalDocumentId(vector_id))) {
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS,
                             "Param vector id is not allowed to be zero, INT64_MAX or negative, please check the "
                             "vector_id encoded in mutation key");
      }

      continue;
    } else {
      return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "Param op of mutation is error");
    }
  }

  return ServiceHelper::ValidateIndexRegion(region, vector_ids);
}

void DoTxnPrewriteDocument(StoragePtr storage, google::protobuf::RpcController* controller,
                           const pb::store::TxnPrewriteRequest* request, pb::store::TxnPrewriteResponse* response,
                           TrackClosure* done, bool is_sync) {
  brpc::Controller* cntl = (brpc::Controller*)controller;
  brpc::ClosureGuard done_guard(done);
  auto tracker = done->Tracker();
  tracker->SetServiceQueueWaitTime();

  int64_t region_id = request->context().region_id();
  auto region = Server::GetInstance().GetRegion(region_id);
  if (region == nullptr) {
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREGION_NOT_FOUND,
                            fmt::format("Not found region {} at server {}", region_id, Server::GetInstance().Id()));
    return;
  }

  auto status = ValidateIndexTxnPrewriteRequest(storage, request, region);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());
    ServiceHelper::GetStoreRegionInfo(region, response->mutable_error());
    return;
  }

  // check latches
  auto start_time_us = butil::gettimeofday_us();
  std::vector<std::string> keys_for_lock;
  for (const auto& mutation : request->mutations()) {
    keys_for_lock.push_back(std::to_string(mutation.vector().id()));
  }
  Lock lock(keys_for_lock);
  BthreadCond sync_cond;
  uint64_t cid = (uint64_t)(&sync_cond);

  bool latch_got = false;
  while (!latch_got) {
    latch_got = region->LatchesAcquire(&lock, cid);
    if (!latch_got) {
      sync_cond.IncreaseWait();
    }
  }

  g_txn_latches_recorder << butil::gettimeofday_us() - start_time_us;

  // release latches after done
  DEFER(region->LatchesRelease(&lock, cid));

  auto ctx = std::make_shared<Context>(cntl, is_sync ? nullptr : done_guard.release(), request, response);
  ctx->SetRegionId(request->context().region_id());
  ctx->SetTracker(tracker);
  ctx->SetCfName(Constant::kStoreDataCF);
  ctx->SetRegionEpoch(request->context().region_epoch());
  ctx->SetIsolationLevel(request->context().isolation_level());
  ctx->SetRawEngineType(region->GetRawEngineType());
  ctx->SetStoreEngineType(region->GetStoreEngineType());

  std::vector<pb::store::Mutation> mutations;
  mutations.reserve(request->mutations_size());
  for (const auto& mutation : request->mutations()) {
    pb::store::Mutation store_mutation;
    store_mutation.set_op(mutation.op());
    store_mutation.set_key(mutation.key());
    store_mutation.set_value(mutation.vector().SerializeAsString());
    mutations.push_back(store_mutation);
  }

  std::map<int64_t, int64_t> for_update_ts_checks;
  for (const auto& for_update_ts_check : request->for_update_ts_checks()) {
    for_update_ts_checks.insert_or_assign(for_update_ts_check.index(), for_update_ts_check.expected_for_update_ts());
  }

  std::map<int64_t, std::string> lock_extra_datas;
  for (const auto& lock_extra_data : request->lock_extra_datas()) {
    lock_extra_datas.insert_or_assign(lock_extra_data.index(), lock_extra_data.extra_data());
  }

  std::vector<int64_t> pessimistic_checks;
  pessimistic_checks.reserve(request->pessimistic_checks_size());
  for (const auto& pessimistic_check : request->pessimistic_checks()) {
    pessimistic_checks.push_back(pessimistic_check);
  }
  std::vector<pb::common::KeyValue> kvs;
  status = storage->TxnPrewrite(ctx, mutations, request->primary_lock(), request->start_ts(), request->lock_ttl(),
                                request->txn_size(), request->try_one_pc(), request->max_commit_ts(),
                                pessimistic_checks, for_update_ts_checks, lock_extra_datas);

  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());

    if (!is_sync) done->Run();
  }
}

void DocumentServiceImpl::TxnPrewrite(google::protobuf::RpcController* controller,
                                      const pb::store::TxnPrewriteRequest* request,
                                      pb::store::TxnPrewriteResponse* response, google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  if (IsRaftApplyPendingExceed()) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "Raft apply queue is full, please wait and retry");
    return;
  }

  if (IsBackgroundPendingTaskCountExceed()) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "Background pending task count is full, please wait and retry");
    return;
  }

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoTxnPrewriteDocument(storage_, controller, request, response, svr_done, true);
  });
  bool ret = write_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

static butil::Status ValidateTxnCommitRequest(const pb::store::TxnCommitRequest* request, store::RegionPtr region) {
  // check if region_epoch is match
  auto epoch_ret = ServiceHelper::ValidateRegionEpoch(request->context().region_epoch(), region);
  if (!epoch_ret.ok()) {
    return epoch_ret;
  }

  if (request->start_ts() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "start_ts is 0");
  }

  if (request->commit_ts() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "commit_ts is 0");
  }

  if (request->keys().empty()) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "keys is empty");
  }

  if (request->keys_size() > FLAGS_document_max_batch_count) {
    return butil::Status(pb::error::EDOCUMENT_EXCEED_MAX_BATCH_COUNT,
                         fmt::format("Param documents size {} is exceed max batch count {}", request->keys_size(),
                                     FLAGS_document_max_batch_count));
  }

  if (request->ByteSizeLong() > FLAGS_document_max_request_size) {
    return butil::Status(pb::error::EDOCUMENT_EXCEED_MAX_REQUEST_SIZE,
                         fmt::format("Param documents size {} is exceed max batch size {}", request->ByteSizeLong(),
                                     FLAGS_document_max_request_size));
  }

  if (!region->VectorIndexWrapper()->IsReady()) {
    if (region->VectorIndexWrapper()->IsBuildError()) {
      return butil::Status(pb::error::EDOCUMENT_INDEX_BUILD_ERROR,
                           fmt::format("Vector index {} build error, please wait for recover.", region->Id()));
    }
    return butil::Status(pb::error::EDOCUMENT_INDEX_NOT_READY,
                         fmt::format("Vector index {} not ready, please retry.", region->Id()));
  }

  if (region->VectorIndexWrapper()->IsExceedsMaxElements()) {
    return butil::Status(pb::error::EDOCUMENT_INDEX_EXCEED_MAX_ELEMENTS,
                         fmt::format("Vector index {} exceeds max elements.", region->Id()));
  }

  auto status = ServiceHelper::ValidateClusterReadOnly();
  if (!status.ok()) {
    return status;
  }

  std::vector<int64_t> vector_ids;
  for (const auto& key : request->keys()) {
    int64_t vector_id = DocumentCodec::DecodeDocumentId(key);
    if (vector_id == 0) {
      return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "Param vector id is error");
    }
    vector_ids.push_back(vector_id);
  }

  auto ret1 = ServiceHelper::ValidateIndexRegion(region, vector_ids);
  if (!ret1.ok()) {
    return ret1;
  }

  std::vector<std::string_view> keys;
  for (const auto& key : request->keys()) {
    if (key.empty()) {
      return butil::Status(pb::error::EKEY_EMPTY, "key is empty");
    }
    keys.push_back(key);
  }
  status = ServiceHelper::ValidateRegion(region, keys);
  if (!status.ok()) {
    return status;
  }

  return butil::Status::OK();
}

void DoTxnCommit(StoragePtr storage, google::protobuf::RpcController* controller,
                 const pb::store::TxnCommitRequest* request, pb::store::TxnCommitResponse* response, TrackClosure* done,
                 bool is_sync);

void DocumentServiceImpl::TxnCommit(google::protobuf::RpcController* controller,
                                    const pb::store::TxnCommitRequest* request, pb::store::TxnCommitResponse* response,
                                    google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  if (IsRaftApplyPendingExceed()) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "Raft apply queue is full, please wait and retry");
    return;
  }

  if (IsBackgroundPendingTaskCountExceed()) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "Background pending task count is full, please wait and retry");
    return;
  }

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoTxnCommit(storage_, controller, request, response, svr_done, true);
  });
  bool ret = write_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

static butil::Status VectorValidateTxnCheckTxnStatusRequest(const pb::store::TxnCheckTxnStatusRequest* request,
                                                            store::RegionPtr region) {
  // check if region_epoch is match
  auto status = ServiceHelper::ValidateRegionEpoch(request->context().region_epoch(), region);
  if (!status.ok()) {
    return status;
  }

  if (request->primary_key().empty()) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "primary_key is empty");
  }

  if (request->lock_ts() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "lock_ts is 0");
  }

  if (request->caller_start_ts() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "caller_start_ts is 0");
  }

  if (request->current_ts() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "current_ts is 0");
  }

  status = ServiceHelper::ValidateClusterReadOnly();
  if (!status.ok()) {
    return status;
  }

  std::vector<std::string_view> keys;
  keys.push_back(request->primary_key());
  status = ServiceHelper::ValidateRegion(region, keys);
  if (!status.ok()) {
    return status;
  }

  if (!region->VectorIndexWrapper()->IsReady()) {
    if (region->VectorIndexWrapper()->IsBuildError()) {
      return butil::Status(pb::error::EDOCUMENT_INDEX_BUILD_ERROR,
                           fmt::format("Vector index {} build error, please wait for recover.", region->Id()));
    }
    return butil::Status(pb::error::EDOCUMENT_INDEX_NOT_READY,
                         fmt::format("Vector index {} not ready, please retry.", region->Id()));
  }

  return butil::Status();
}

void DoTxnCheckTxnStatus(StoragePtr storage, google::protobuf::RpcController* controller,
                         const pb::store::TxnCheckTxnStatusRequest* request,
                         pb::store::TxnCheckTxnStatusResponse* response, TrackClosure* done, bool is_sync);

void DocumentServiceImpl::TxnCheckTxnStatus(google::protobuf::RpcController* controller,
                                            const pb::store::TxnCheckTxnStatusRequest* request,
                                            pb::store::TxnCheckTxnStatusResponse* response,
                                            google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  if (IsRaftApplyPendingExceed()) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "Raft apply queue is full, please wait and retry");
    return;
  }

  if (IsBackgroundPendingTaskCountExceed()) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "Background pending task count is full, please wait and retry");
    return;
  }

  int64_t region_id = request->context().region_id();
  auto region = Server::GetInstance().GetRegion(region_id);
  if (region == nullptr) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREGION_NOT_FOUND,
                            fmt::format("Not found region {} at server {}", region_id, Server::GetInstance().Id()));
    return;
  }

  auto status = VectorValidateTxnCheckTxnStatusRequest(request, region);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());
    return;
  }

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoTxnCheckTxnStatus(storage_, controller, request, response, svr_done, true);
  });
  bool ret = write_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

static butil::Status VectorValidateTxnResolveLockRequest(const pb::store::TxnResolveLockRequest* request,
                                                         store::RegionPtr region) {
  // check if region_epoch is match
  auto epoch_ret = ServiceHelper::ValidateRegionEpoch(request->context().region_epoch(), region);
  if (!epoch_ret.ok()) {
    return epoch_ret;
  }

  if (request->start_ts() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "start_ts is 0, it's illegal");
  }

  if (request->commit_ts() < 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "commit_ts < 0, it's illegal");
  }

  if (request->commit_ts() > 0 && request->commit_ts() < request->start_ts()) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "commit_ts < start_ts, it's illegal");
  }

  if (request->keys_size() > 0) {
    for (const auto& key : request->keys()) {
      if (key.empty()) {
        return butil::Status(pb::error::EKEY_EMPTY, "key is empty");
      }
      std::vector<std::string_view> keys;
      keys.push_back(key);
      auto status = ServiceHelper::ValidateRegion(region, keys);
      if (!status.ok()) {
        return status;
      }
    }
  }

  if (!region->VectorIndexWrapper()->IsReady()) {
    if (region->VectorIndexWrapper()->IsBuildError()) {
      return butil::Status(pb::error::EDOCUMENT_INDEX_BUILD_ERROR,
                           fmt::format("Vector index {} build error, please wait for recover.", region->Id()));
    }
    return butil::Status(pb::error::EDOCUMENT_INDEX_NOT_READY,
                         fmt::format("Vector index {} not ready, please retry.", region->Id()));
  }

  auto status = ServiceHelper::ValidateClusterReadOnly();
  if (!status.ok()) {
    return status;
  }

  return butil::Status();
}

void DoTxnResolveLock(StoragePtr storage, google::protobuf::RpcController* controller,
                      const pb::store::TxnResolveLockRequest* request, pb::store::TxnResolveLockResponse* response,
                      TrackClosure* done, bool is_sync);

void DocumentServiceImpl::TxnResolveLock(google::protobuf::RpcController* controller,
                                         const pb::store::TxnResolveLockRequest* request,
                                         pb::store::TxnResolveLockResponse* response, google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  if (IsRaftApplyPendingExceed()) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "Raft apply queue is full, please wait and retry");
    return;
  }

  if (IsBackgroundPendingTaskCountExceed()) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "Background pending task count is full, please wait and retry");
    return;
  }

  int64_t region_id = request->context().region_id();
  auto region = Server::GetInstance().GetRegion(region_id);
  if (region == nullptr) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREGION_NOT_FOUND,
                            fmt::format("Not found region {} at server {}", region_id, Server::GetInstance().Id()));
    return;
  }

  auto status = VectorValidateTxnResolveLockRequest(request, region);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());
    return;
  }

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoTxnResolveLock(storage_, controller, request, response, svr_done, true);
  });
  bool ret = write_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

static butil::Status ValidateTxnBatchGetRequest(const pb::store::TxnBatchGetRequest* request, store::RegionPtr region) {
  // check if region_epoch is match
  auto epoch_ret = ServiceHelper::ValidateRegionEpoch(request->context().region_epoch(), region);
  if (!epoch_ret.ok()) {
    return epoch_ret;
  }

  if (request->keys_size() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "Keys is empty");
  }

  if (request->start_ts() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "start_ts is 0");
  }

  std::vector<std::string_view> keys;
  for (const auto& key : request->keys()) {
    if (key.empty()) {
      return butil::Status(pb::error::EKEY_EMPTY, "key is empty");
    }
    keys.push_back(key);
  }
  auto status = ServiceHelper::ValidateRegion(region, keys);
  if (!status.ok()) {
    return status;
  }

  return butil::Status();
}

void DoTxnBatchGetDocument(StoragePtr storage, google::protobuf::RpcController* controller,
                           const pb::store::TxnBatchGetRequest* request, pb::store::TxnBatchGetResponse* response,
                           TrackClosure* done) {
  brpc::Controller* cntl = (brpc::Controller*)controller;
  brpc::ClosureGuard done_guard(done);
  auto tracker = done->Tracker();
  tracker->SetServiceQueueWaitTime();

  int64_t region_id = request->context().region_id();
  auto region = Server::GetInstance().GetRegion(region_id);
  if (region == nullptr) {
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREGION_NOT_FOUND,
                            fmt::format("Not found region {} at server {}", region_id, Server::GetInstance().Id()));
    return;
  }

  butil::Status status = ValidateTxnBatchGetRequest(request, region);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());
    ServiceHelper::GetStoreRegionInfo(region, response->mutable_error());
    return;
  }

  auto ctx = std::make_shared<Context>();
  ctx->SetRegionId(request->context().region_id());
  ctx->SetTracker(tracker);
  ctx->SetCfName(Constant::kStoreDataCF);
  ctx->SetRegionEpoch(request->context().region_epoch());
  ctx->SetIsolationLevel(request->context().isolation_level());
  ctx->SetRawEngineType(region->GetRawEngineType());
  ctx->SetStoreEngineType(region->GetStoreEngineType());

  std::vector<std::string> keys;
  for (const auto& key : request->keys()) {
    keys.emplace_back(key);
  }

  std::set<int64_t> resolved_locks;
  for (const auto& lock : request->context().resolved_locks()) {
    resolved_locks.insert(lock);
  }

  pb::store::TxnResultInfo txn_result_info;

  std::vector<pb::common::KeyValue> kvs;
  status = storage->TxnBatchGet(ctx, request->start_ts(), keys, resolved_locks, txn_result_info, kvs);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());

    return;
  }

  if (!kvs.empty()) {
    for (auto& kv : kvs) {
      pb::common::DocumentWithId document_with_id;

      if (!kv.value().empty()) {
        auto parse_ret = document_with_id.ParseFromString(kv.value());
        if (!parse_ret) {
          auto* err = response->mutable_error();
          err->set_errcode(static_cast<Errno>(pb::error::EINTERNAL));
          err->set_errmsg("parse document_with_id failed");
          return;
        }
      }

      response->add_documents()->Swap(&document_with_id);
    }
  }
  *response->mutable_txn_result() = txn_result_info;
}

void DocumentServiceImpl::TxnBatchGet(google::protobuf::RpcController* controller,
                                      const pb::store::TxnBatchGetRequest* request,
                                      pb::store::TxnBatchGetResponse* response, google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoTxnBatchGetDocument(storage_, controller, request, response, svr_done);
  });
  bool ret = read_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

static butil::Status ValidateTxnBatchRollbackRequest(const pb::store::TxnBatchRollbackRequest* request,
                                                     store::RegionPtr region) {
  // check if region_epoch is match
  auto epoch_ret = ServiceHelper::ValidateRegionEpoch(request->context().region_epoch(), region);
  if (!epoch_ret.ok()) {
    return epoch_ret;
  }

  if (request->keys_size() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "Keys is empty");
  }

  if (request->start_ts() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "start_ts is 0");
  }

  std::vector<std::string_view> keys;
  for (const auto& key : request->keys()) {
    if (key.empty()) {
      return butil::Status(pb::error::EKEY_EMPTY, "key is empty");
    }
    keys.push_back(key);
  }
  auto status = ServiceHelper::ValidateRegion(region, keys);
  if (!status.ok()) {
    return status;
  }

  status = ServiceHelper::ValidateClusterReadOnly();
  if (!status.ok()) {
    return status;
  }

  return butil::Status();
}

void DoTxnBatchRollback(StoragePtr storage, google::protobuf::RpcController* controller,
                        const pb::store::TxnBatchRollbackRequest* request,
                        pb::store::TxnBatchRollbackResponse* response, TrackClosure* done, bool is_sync);

void DocumentServiceImpl::TxnBatchRollback(google::protobuf::RpcController* controller,
                                           const pb::store::TxnBatchRollbackRequest* request,
                                           pb::store::TxnBatchRollbackResponse* response,
                                           google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  if (IsRaftApplyPendingExceed()) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "Raft apply queue is full, please wait and retry");
    return;
  }

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoTxnBatchRollback(storage_, controller, request, response, svr_done, true);
  });
  bool ret = write_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

static butil::Status ValidateTxnScanLockRequest(const pb::store::TxnScanLockRequest* request, store::RegionPtr region) {
  // check if region_epoch is match
  auto epoch_ret = ServiceHelper::ValidateRegionEpoch(request->context().region_epoch(), region);
  if (!epoch_ret.ok()) {
    return epoch_ret;
  }

  if (request->max_ts() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "max_ts is 0");
  }

  if (request->limit() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "limit is 0");
  }

  if (request->limit() > FLAGS_max_scan_lock_limit) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "limit is too large, max=1024");
  }

  if (request->start_key().empty()) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "start_key is empty");
  }

  if (request->end_key().empty()) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "end_key is empty");
  }

  if (request->start_key() >= request->end_key()) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "start_key >= end_key");
  }

  std::vector<std::string_view> keys;
  keys.push_back(request->start_key());
  keys.push_back(request->end_key());

  auto status = ServiceHelper::ValidateRegion(region, keys);
  if (!status.ok()) {
    return status;
  }

  return butil::Status();
}

void DoTxnScanLock(StoragePtr storage, google::protobuf::RpcController* controller,
                   const pb::store::TxnScanLockRequest* request, pb::store::TxnScanLockResponse* response,
                   TrackClosure* done);

void DocumentServiceImpl::TxnScanLock(google::protobuf::RpcController* controller,
                                      const pb::store::TxnScanLockRequest* request,
                                      pb::store::TxnScanLockResponse* response, google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoTxnScanLock(storage_, controller, request, response, svr_done);
  });
  bool ret = read_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

static butil::Status ValidateTxnHeartBeatRequest(const pb::store::TxnHeartBeatRequest* request,
                                                 store::RegionPtr region) {
  // check if region_epoch is match
  auto epoch_ret = ServiceHelper::ValidateRegionEpoch(request->context().region_epoch(), region);
  if (!epoch_ret.ok()) {
    return epoch_ret;
  }

  if (request->primary_lock().empty()) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "primary_lock is empty");
  }

  if (request->start_ts() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "start_ts is 0");
  }

  if (request->advise_lock_ttl() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "advise_lock_ttl is 0");
  }

  std::vector<std::string_view> keys;
  keys.push_back(request->primary_lock());

  auto status = ServiceHelper::ValidateRegion(region, keys);
  if (!status.ok()) {
    return status;
  }

  return butil::Status();
}

void DoTxnHeartBeat(StoragePtr storage, google::protobuf::RpcController* controller,
                    const pb::store::TxnHeartBeatRequest* request, pb::store::TxnHeartBeatResponse* response,
                    TrackClosure* done, bool is_sync);

void DocumentServiceImpl::TxnHeartBeat(google::protobuf::RpcController* controller,
                                       const pb::store::TxnHeartBeatRequest* request,
                                       pb::store::TxnHeartBeatResponse* response, google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  if (IsRaftApplyPendingExceed()) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "Raft apply queue is full, please wait and retry");
    return;
  }

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoTxnHeartBeat(storage_, controller, request, response, svr_done, true);
  });
  bool ret = write_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

static butil::Status DocumentValidateTxnGcRequest(const pb::store::TxnGcRequest* request, store::RegionPtr region) {
  // check if region_epoch is match
  auto epoch_ret = ServiceHelper::ValidateRegionEpoch(request->context().region_epoch(), region);
  if (!epoch_ret.ok()) {
    return epoch_ret;
  }

  if (request->safe_point_ts() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "safe_point_ts is 0");
  }

  return butil::Status();
}

void DoTxnGc(StoragePtr storage, google::protobuf::RpcController* controller, const pb::store::TxnGcRequest* request,
             pb::store::TxnGcResponse* response, TrackClosure* done, bool is_sync);

void DocumentServiceImpl::TxnGc(google::protobuf::RpcController* controller, const pb::store::TxnGcRequest* request,
                                pb::store::TxnGcResponse* response, google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  if (IsRaftApplyPendingExceed()) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "Raft apply queue is full, please wait and retry");
    return;
  }

  int64_t region_id = request->context().region_id();
  auto region = Server::GetInstance().GetRegion(region_id);
  if (region == nullptr) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREGION_NOT_FOUND,
                            fmt::format("Not found region {} at server {}", region_id, Server::GetInstance().Id()));
    return;
  }

  auto status = DocumentValidateTxnGcRequest(request, region);
  if (!status.ok()) {
    ServiceHelper::SetError(response->mutable_error(), status.error_code(), status.error_str());
    return;
  }

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoTxnGc(storage_, controller, request, response, svr_done, true);
  });
  bool ret = write_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

static butil::Status ValidateTxnDeleteRangeRequest(const pb::store::TxnDeleteRangeRequest* request,
                                                   store::RegionPtr region) {
  // check if region_epoch is match
  auto epoch_ret = ServiceHelper::ValidateRegionEpoch(request->context().region_epoch(), region);
  if (!epoch_ret.ok()) {
    return epoch_ret;
  }

  if (request->start_key().empty()) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "start_key is empty");
  }

  if (request->end_key().empty()) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "end_key is empty");
  }

  if (request->start_key() == request->end_key()) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "start_key is equal to end_key");
  }

  if (request->start_key().compare(request->end_key()) > 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "start_key is greater than end_key");
  }

  return butil::Status();
}

void DoTxnDeleteRange(StoragePtr storage, google::protobuf::RpcController* controller,
                      const pb::store::TxnDeleteRangeRequest* request, pb::store::TxnDeleteRangeResponse* response,
                      TrackClosure* done, bool is_sync);

void DocumentServiceImpl::TxnDeleteRange(google::protobuf::RpcController* controller,
                                         const pb::store::TxnDeleteRangeRequest* request,
                                         pb::store::TxnDeleteRangeResponse* response, google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  if (IsRaftApplyPendingExceed()) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "Raft apply queue is full, please wait and retry");
    return;
  }

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoTxnDeleteRange(storage_, controller, request, response, svr_done, true);
  });
  bool ret = write_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

static butil::Status ValidateTxnDumpRequest(const pb::store::TxnDumpRequest* request, store::RegionPtr region) {
  // check if region_epoch is match
  auto epoch_ret = ServiceHelper::ValidateRegionEpoch(request->context().region_epoch(), region);
  if (!epoch_ret.ok()) {
    return epoch_ret;
  }

  if (request->start_key().empty()) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "start_key is empty");
  }

  if (request->end_key().empty()) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "end_key is empty");
  }

  if (request->start_key() == request->end_key()) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "start_key is equal to end_key");
  }

  if (request->start_key().compare(request->end_key()) > 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "start_key is greater than end_key");
  }

  if (request->end_ts() == 0) {
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, "end_ts is 0");
  }

  return butil::Status();
}

void DoTxnDump(StoragePtr storage, google::protobuf::RpcController* controller,
               const pb::store::TxnDumpRequest* request, pb::store::TxnDumpResponse* response, TrackClosure* done);

void DocumentServiceImpl::TxnDump(google::protobuf::RpcController* controller, const pb::store::TxnDumpRequest* request,
                                  pb::store::TxnDumpResponse* response, google::protobuf::Closure* done) {
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  // Run in queue.
  auto task = std::make_shared<ServiceTask>([this, controller, request, response, svr_done]() {
    DoTxnDump(storage_, controller, request, response, svr_done);
  });
  bool ret = read_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

void DoHello(google::protobuf::RpcController* controller, const dingodb::pb::document::HelloRequest* request,
             dingodb::pb::document::HelloResponse* response, TrackClosure* done, bool is_get_memory_info = false) {
  brpc::Controller* cntl = (brpc::Controller*)controller;
  brpc::ClosureGuard done_guard(done);

  *response->mutable_version_info() = GetVersionInfo();
  if (request->is_just_version_info() && !is_get_memory_info) {
    return;
  }

  auto raft_engine = Server::GetInstance().GetRaftStoreEngine();
  if (raft_engine == nullptr) {
    return;
  }

  auto regions = Server::GetInstance().GetAllAliveRegion();
  response->set_region_count(regions.size());

  int64_t leader_count = 0;
  for (const auto& region : regions) {
    if (raft_engine->IsLeader(region->Id())) {
      leader_count++;
    }
  }
  response->set_region_leader_count(leader_count);

  if (request->get_region_metrics() || is_get_memory_info) {
    auto store_metrics_manager = Server::GetInstance().GetStoreMetricsManager();
    if (store_metrics_manager == nullptr) {
      return;
    }

    auto store_region_metrics = store_metrics_manager->GetStoreRegionMetrics();
    if (store_region_metrics == nullptr) {
      return;
    }

    auto region_metrics = store_region_metrics->GetAllMetrics();
    for (const auto& region_metrics : region_metrics) {
      auto* new_region_metrics = response->add_region_metrics();
      *new_region_metrics = region_metrics->InnerRegionMetrics();
    }

    auto store_metrics_ptr = store_metrics_manager->GetStoreMetrics();
    if (store_metrics_ptr == nullptr) {
      return;
    }

    auto store_own_metrics = store_metrics_ptr->Metrics();
    *(response->mutable_store_own_metrics()) = store_own_metrics.store_own_metrics();
  }
}

void DocumentServiceImpl::Hello(google::protobuf::RpcController* controller, const pb::document::HelloRequest* request,
                                pb::document::HelloResponse* response, google::protobuf::Closure* done) {
  // Run in queue.
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  auto task = std::make_shared<ServiceTask>([=]() { DoHello(controller, request, response, svr_done); });

  bool ret = read_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

void DocumentServiceImpl::GetMemoryInfo(google::protobuf::RpcController* controller,
                                        const pb::document::HelloRequest* request,
                                        pb::document::HelloResponse* response, google::protobuf::Closure* done) {
  // Run in queue.
  auto* svr_done = new ServiceClosure(__func__, done, request, response);

  auto task = std::make_shared<ServiceTask>([=]() { DoHello(controller, request, response, svr_done, true); });

  bool ret = read_worker_set_->ExecuteRR(task);
  if (!ret) {
    brpc::ClosureGuard done_guard(svr_done);
    ServiceHelper::SetError(response->mutable_error(), pb::error::EREQUEST_FULL,
                            "WorkerSet queue is full, please wait and retry");
  }
}

}  // namespace dingodb
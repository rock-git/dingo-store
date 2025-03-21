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

#ifndef DINGODB_SERVER_SERVICE_HELPER_H_
#define DINGODB_SERVER_SERVICE_HELPER_H_

#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>

#include "butil/compiler_specific.h"
#include "butil/endpoint.h"
#include "common/constant.h"
#include "common/helper.h"
#include "common/logging.h"
#include "common/tracker.h"
#include "fmt/core.h"
#include "glog/logging.h"
#include "meta/store_meta_manager.h"
#include "proto/error.pb.h"
#include "server/server.h"
namespace dingodb {

DECLARE_int64(service_log_threshold_time_ns);
DECLARE_int32(log_print_max_length);
DECLARE_bool(enable_dump_service_message);

struct LatchContext;
using LatchContextPtr = std::shared_ptr<LatchContext>;

class LatchContext {
 public:
  LatchContext(store::RegionPtr region, const std::vector<std::string>& keys) : region_(region), lock_(keys) {}

  static LatchContextPtr New(store::RegionPtr region, const std::vector<std::string>& keys) {
    return std::make_shared<LatchContext>(region, keys);
  }

  store::RegionPtr GetRegion() const { return region_; }

  uint64_t Cid() const { return (uint64_t)&sync_cond_; }
  Lock* GetLock() { return &lock_; };

  BthreadCond& SyncCond() { return sync_cond_; }

 private:
  store::RegionPtr region_;
  Lock lock_;
  BthreadCond sync_cond_;
};

class ServiceHelper {
 public:
  template <typename T>
  static void RedirectLeader(std::string addr, T* response);

  template <typename T>
  static pb::node::NodeInfo RedirectLeader(std::string addr);

  static void SetError(pb::error::Error* error, int errcode, const std::string& errmsg);
  static void SetError(pb::error::Error* error, const std::string& errmsg);

  static butil::Status ValidateRegionEpoch(const pb::common::RegionEpoch& req_epoch, store::RegionPtr region);
  static butil::Status GetStoreRegionInfo(store::RegionPtr region, pb::error::Error* error);
  static butil::Status ValidateRegionState(store::RegionPtr region);
  static butil::Status ValidateRange(const pb::common::Range& range);
  static butil::Status ValidateKeyInRange(const pb::common::Range& range, const std::vector<std::string_view>& keys);
  static butil::Status ValidateRangeInRange(const pb::common::Range& region_range, const pb::common::Range& req_range);
  static butil::Status ValidateRegion(store::RegionPtr region, const std::vector<std::string_view>& keys);
  static butil::Status ValidateIndexRegion(store::RegionPtr region, const std::vector<int64_t>& vector_ids);
  static butil::Status ValidateDocumentRegion(store::RegionPtr region, const std::vector<int64_t>& document_ids);
  static butil::Status ValidateClusterReadOnly();
  static butil::Status ValidSstMetas(const dingodb::pb::common::StorageBackend& storage_backend,
                                     const dingodb::pb::common::BackupDataFileValueSstMetaGroup& sst_metas,
                                     store::RegionPtr region);

  static void LatchesAcquire(LatchContext& latch_ctx, bool is_txn);
  static void LatchesRelease(LatchContext& latch_ctx);

  static void DumpRequest(const std::string& name, const google::protobuf::Message* request);
  static void DumpResponse(const std::string& name, const google::protobuf::Message* response);
};

template <typename T>
pb::node::NodeInfo ServiceHelper::RedirectLeader(std::string addr) {
  auto raft_endpoint = Helper::StringToEndPoint(addr);
  if (raft_endpoint.port == 0) {
    DINGO_LOG(WARNING) << fmt::format("[redirect][addr({})] invalid addr.", addr);
    return {};
  }

  // From local store map query.
  auto node_info =
      Server::GetInstance().GetStoreMetaManager()->GetStoreServerMeta()->GetNodeInfoByRaftEndPoint(raft_endpoint);
  if (node_info.id() == 0) {
    // From remote node query.
    Helper::GetNodeInfoByRaftLocation(Helper::EndPointToLocation(raft_endpoint), node_info);
  }

  if (!node_info.server_location().host().empty()) {
    // transform ip to hostname
    Server::GetInstance().Ip2Hostname(*node_info.mutable_server_location()->mutable_host());
  }

  DINGO_LOG(INFO) << fmt::format("[redirect][addr({})] redirect leader, node_info: {}", addr,
                                 node_info.ShortDebugString());

  return node_info;
}

template <typename T>
void ServiceHelper::RedirectLeader(std::string addr, T* response) {
  auto node_info = RedirectLeader<T>(addr);
  if (node_info.id() != 0) {
    Helper::SetPbMessageErrorLeader(node_info, response);
  } else {
    response->mutable_error()->set_store_id(Server::GetInstance().Id());
  }
}

// Handle service request in execute queue.
class ServiceTask : public TaskRunnable {
 public:
  using Handler = std::function<void(void)>;
  ServiceTask(Handler handle) : handle_(handle) {}
  ~ServiceTask() override = default;

  std::string Type() override { return "SERVICE_TASK"; }

  void Run() override { handle_(); }

 private:
  Handler handle_;
};

class TrackClosure : public google::protobuf::Closure {
 public:
  TrackClosure(TrackerPtr tracker) : tracker(tracker) {}
  ~TrackClosure() override = default;

  TrackerPtr Tracker() { return tracker; };

  store::RegionPtr GetRegion() { return region; }

 protected:
  std::string dump_name;
  TrackerPtr tracker;
  store::RegionPtr region;
};

// Wrapper brpc service closure for log.
template <typename T, typename U, bool need_region = true>
class ServiceClosure : public TrackClosure {
 public:
  ServiceClosure(const std::string& method_name, google::protobuf::Closure* done, const T* request, U* response)
      : TrackClosure(Tracker::New(request->request_info())),
        method_name_(method_name),
        done_(done),
        request_(request),
        response_(response) {
    DINGO_LOG(DEBUG) << fmt::format("[service.{}] Receive request: {}", method_name_,
                                    request_->ShortDebugString().substr(0, FLAGS_log_print_max_length));
    if (BAIDU_LIKELY(need_region)) {
      int64_t region_id = request->context().region_id();
      region = Server::GetInstance().GetRegion(region_id);
      if (BAIDU_UNLIKELY(region == nullptr)) {
        ServiceHelper::SetError(response->mutable_error(), pb::error::EREGION_NOT_FOUND,
                                fmt::format("Not found region {} at server {}", region_id, Server::GetInstance().Id()));
      } else {
        region->IncServingRequestCount();
      }
    }

    // dump request
    if (BAIDU_UNLIKELY(FLAGS_enable_dump_service_message)) {
      dump_name = fmt::format("{}_{}_{}", method_name_, region ? region->Id() : 0, Helper::TimestampNs());
      ServiceHelper::DumpRequest(dump_name, request_);
    }
  }

  ~ServiceClosure() override = default;

  void Run() override;

 private:
  std::string method_name_;

  google::protobuf::Closure* done_;
  const T* request_;
  U* response_;
};

inline void SetPbMessageResponseInfo(google::protobuf::Message* message, TrackerPtr tracker) {
  if (BAIDU_UNLIKELY(message == nullptr || tracker == nullptr)) {
    return;
  }
  const google::protobuf::Reflection* reflection = message->GetReflection();
  const google::protobuf::Descriptor* desc = message->GetDescriptor();

  const google::protobuf::FieldDescriptor* response_info_field = desc->FindFieldByName("response_info");
  if (BAIDU_UNLIKELY(response_info_field == nullptr)) {
    DINGO_LOG(ERROR) << "SetPbMessageError error_field is nullptr";
    return;
  }
  if (BAIDU_UNLIKELY(response_info_field->message_type()->full_name() != "dingodb.pb.common.ResponseInfo")) {
    DINGO_LOG(ERROR)
        << "SetPbMessageError field->message_type()->full_name() is not pb::common::ResponseInfo, its_type="
        << response_info_field->message_type()->full_name();
    return;
  }

  pb::common::ResponseInfo* response_info =
      dynamic_cast<pb::common::ResponseInfo*>(reflection->MutableMessage(message, response_info_field));
  auto* time_info = response_info->mutable_time_info();
  time_info->set_total_rpc_time_ns(tracker->TotalRpcTime());
  time_info->set_service_queue_wait_time_ns(tracker->ServiceQueueWaitTime());
  time_info->set_prepair_commit_time_ns(tracker->PrepairCommitTime());
  time_info->set_raft_commit_time_ns(tracker->RaftCommitTime());
  time_info->set_raft_queue_wait_time_ns(tracker->RaftQueueWaitTime());
  time_info->set_raft_apply_time_ns(tracker->RaftApplyTime());
  time_info->set_store_write_time_ns(tracker->StoreWriteTime());
  time_info->set_vector_index_write_time_ns(tracker->VectorIndexwriteTime());
  time_info->set_document_index_write_time_ns(tracker->DocumentIndexwriteTime());
}

template <typename T, typename U, bool need_region>
void ServiceClosure<T, U, need_region>::Run() {
  std::unique_ptr<ServiceClosure<T, U, need_region>> self_guard(this);
  brpc::ClosureGuard done_guard(done_);

  tracker->SetTotalRpcTime();
  uint64_t elapsed_time = tracker->TotalRpcTime();
  SetPbMessageResponseInfo(response_, tracker);

  if (response_->error().errcode() != 0) {
    // Set leader redirect info(pb.Error.leader_location).
    if (response_->error().errcode() == pb::error::ERAFT_NOTLEADER) {
      ServiceHelper::RedirectLeader(response_->error().errmsg(), response_);
      response_->mutable_error()->set_errmsg(fmt::format("Not leader({}) on region {}, please redirect leader({}).",
                                                         Server::GetInstance().ServerAddr(),
                                                         request_->context().region_id(), response_->error().errmsg()));
    } else if (response_->error().errcode() == pb::error::EREQUEST_FULL) {
      DINGO_LOG(WARNING) << fmt::format("Worker set pending task count, {}",
                                        Server::GetInstance().GetAllWorkSetPendingTaskCount());
    }

    DINGO_LOG(ERROR) << fmt::format(
        "[service.{}][request_id({})][elapsed(ns)({})] Request failed, response: {} request: {}", method_name_,
        request_->request_info().request_id(), elapsed_time,
        response_->ShortDebugString().substr(0, FLAGS_log_print_max_length),
        request_->ShortDebugString().substr(0, FLAGS_log_print_max_length));
  } else {
    if (BAIDU_UNLIKELY(elapsed_time >= FLAGS_service_log_threshold_time_ns)) {
      DINGO_LOG(INFO) << fmt::format(
          "[service.{}][request_id({})][elapsed(ns)({})] Request finish, response: {} request: {}", method_name_,
          request_->request_info().request_id(), elapsed_time,
          response_->ShortDebugString().substr(0, FLAGS_log_print_max_length),
          request_->ShortDebugString().substr(0, FLAGS_log_print_max_length));
    } else {
      DINGO_LOG(DEBUG) << fmt::format(
          "[service.{}][request_id({})][elapsed(ns)({})] Request finish, response: {} request: {}", method_name_,
          request_->request_info().request_id(), elapsed_time,
          response_->ShortDebugString().substr(0, FLAGS_log_print_max_length),
          request_->ShortDebugString().substr(0, FLAGS_log_print_max_length));
    }
  }

  if (BAIDU_UNLIKELY(FLAGS_enable_dump_service_message)) {
    ServiceHelper::DumpResponse(dump_name, response_);
  }

  if (region) {
    region->DecServingRequestCount();
    region->UpdateLastServingTime();
  }
}

template <>
inline void ServiceClosure<pb::index::VectorCalcDistanceRequest, pb::index::VectorCalcDistanceResponse>::Run() {
  std::unique_ptr<ServiceClosure<pb::index::VectorCalcDistanceRequest, pb::index::VectorCalcDistanceResponse>>
      self_guard(this);
  brpc::ClosureGuard done_guard(done_);

  tracker->SetTotalRpcTime();
  uint64_t elapsed_time = tracker->TotalRpcTime();
  SetPbMessageResponseInfo(response_, tracker);

  if (response_->error().errcode() != 0) {
    DINGO_LOG(ERROR) << fmt::format(
        "[service.{}][request_id({})][elapsed(ns)({})] Request failed, response: {} request: {}", method_name_,
        request_->request_info().request_id(), elapsed_time,
        response_->ShortDebugString().substr(0, FLAGS_log_print_max_length),
        request_->ShortDebugString().substr(0, FLAGS_log_print_max_length));
  } else {
    if (BAIDU_UNLIKELY(elapsed_time >= FLAGS_service_log_threshold_time_ns)) {
      DINGO_LOG(INFO) << fmt::format(
          "[service.{}][request_id({})][elapsed(ns)({})] Request finish, response: {} request: {}", method_name_,
          request_->request_info().request_id(), elapsed_time,
          response_->ShortDebugString().substr(0, FLAGS_log_print_max_length),
          request_->ShortDebugString().substr(0, FLAGS_log_print_max_length));
    } else {
      DINGO_LOG(DEBUG) << fmt::format(
          "[service.{}][request_id({})][elapsed(ns)({})] Request finish, response: {} request: {}", method_name_,
          request_->request_info().request_id(), elapsed_time,
          response_->ShortDebugString().substr(0, FLAGS_log_print_max_length),
          request_->ShortDebugString().substr(0, FLAGS_log_print_max_length));
    }
  }

  if (BAIDU_UNLIKELY(FLAGS_enable_dump_service_message)) {
    ServiceHelper::DumpResponse(dump_name, response_);
  }
}

// Wrapper brpc service closure for log.
template <typename T, typename U>
class CoordinatorServiceClosure : public TrackClosure {
 public:
  CoordinatorServiceClosure(const std::string& method_name, google::protobuf::Closure* done, const T* request,
                            U* response)
      : TrackClosure(Tracker::New(request->request_info())),
        method_name_(method_name),
        done_(done),
        request_(request),
        response_(response) {
    DINGO_LOG(DEBUG) << fmt::format("[service.{}] Receive request: {}", method_name_,
                                    request_->ShortDebugString().substr(0, FLAGS_log_print_max_length));

    // dump request
    if (BAIDU_UNLIKELY(FLAGS_enable_dump_service_message)) {
      dump_name = fmt::format("{}_{}_{}", method_name_, region ? region->Id() : 0, Helper::TimestampNs());
      ServiceHelper::DumpRequest(dump_name, request_);
    }
  }
  ~CoordinatorServiceClosure() override = default;

  void Run() override;

 private:
  std::string method_name_;

  google::protobuf::Closure* done_;
  const T* request_;
  U* response_;
};

template <typename T, typename U>
void CoordinatorServiceClosure<T, U>::Run() {
  std::unique_ptr<CoordinatorServiceClosure<T, U>> self_guard(this);
  brpc::ClosureGuard done_guard(done_);

  tracker->SetTotalRpcTime();
  uint64_t elapsed_time = tracker->TotalRpcTime();
  SetPbMessageResponseInfo(response_, tracker);

  if (response_->error().errcode() != 0) {
    // Set leader redirect info(pb.Error.leader_location).
    if (response_->error().errcode() == pb::error::ERAFT_NOTLEADER) {
      response_->mutable_error()->set_errmsg(fmt::format("Not leader({}), please redirect leader({}).",
                                                         Server::GetInstance().ServerAddr(),
                                                         response_->error().errmsg()));
    }

    DINGO_LOG(ERROR) << fmt::format(
        "[service.{}][request_id({})][elapsed(ns)({})] Request failed, response: {} request: {}", method_name_,
        request_->request_info().request_id(), elapsed_time,
        response_->ShortDebugString().substr(0, FLAGS_log_print_max_length),
        request_->ShortDebugString().substr(0, FLAGS_log_print_max_length));
  } else {
    if (BAIDU_UNLIKELY(elapsed_time >= FLAGS_service_log_threshold_time_ns)) {
      DINGO_LOG(INFO) << fmt::format(
          "[service.{}][request_id({})][elapsed(ns)({})] Request finish, response: {} request: {}", method_name_,
          request_->request_info().request_id(), elapsed_time,
          response_->ShortDebugString().substr(0, FLAGS_log_print_max_length),
          request_->ShortDebugString().substr(0, FLAGS_log_print_max_length));
    } else {
      DINGO_LOG(DEBUG) << fmt::format(
          "[service.{}][request_id({})][elapsed(ns)({})] Request finish, response: {} request: {}", method_name_,
          request_->request_info().request_id(), elapsed_time,
          response_->ShortDebugString().substr(0, FLAGS_log_print_max_length),
          request_->ShortDebugString().substr(0, FLAGS_log_print_max_length));
    }
  }

  if (BAIDU_UNLIKELY(FLAGS_enable_dump_service_message)) {
    ServiceHelper::DumpResponse(dump_name, response_);
  }
}

template <typename T, typename U>
class NoContextServiceClosure : public TrackClosure {
 public:
  NoContextServiceClosure(const std::string& method_name, google::protobuf::Closure* done, const T* request,
                          U* response)
      : TrackClosure(Tracker::New(request->request_info())),
        method_name_(method_name),
        done_(done),
        request_(request),
        response_(response) {
    DINGO_LOG(DEBUG) << fmt::format("[service.{}] Receive request: {}", method_name_,
                                    request_->ShortDebugString().substr(0, FLAGS_log_print_max_length));

    // dump request
    if (BAIDU_UNLIKELY(FLAGS_enable_dump_service_message)) {
      dump_name = fmt::format("{}_{}_{}", method_name_, region ? region->Id() : 0, Helper::TimestampNs());
      ServiceHelper::DumpRequest(dump_name, request_);
    }
  }
  ~NoContextServiceClosure() override = default;

  void Run() override;

 private:
  std::string method_name_;

  google::protobuf::Closure* done_;
  const T* request_;
  U* response_;
};

template <typename T, typename U>
void NoContextServiceClosure<T, U>::Run() {
  std::unique_ptr<NoContextServiceClosure<T, U>> self_guard(this);
  brpc::ClosureGuard done_guard(done_);

  tracker->SetTotalRpcTime();
  uint64_t elapsed_time = tracker->TotalRpcTime();
  SetPbMessageResponseInfo(response_, tracker);

  if (response_->error().errcode() != 0) {
    // Set leader redirect info(pb.Error.leader_location).
    if (response_->error().errcode() == pb::error::ERAFT_NOTLEADER) {
      ServiceHelper::RedirectLeader(response_->error().errmsg(), response_);
      response_->mutable_error()->set_errmsg(fmt::format("Not leader({}), please redirect leader({}).",
                                                         Server::GetInstance().ServerAddr(),
                                                         response_->error().errmsg()));
    }

    DINGO_LOG(ERROR) << fmt::format(
        "[service.{}][request_id({})][elapsed(ns)({})] Request failed, response: {} request: {}", method_name_,
        request_->request_info().request_id(), elapsed_time,
        response_->ShortDebugString().substr(0, FLAGS_log_print_max_length),
        request_->ShortDebugString().substr(0, FLAGS_log_print_max_length));
  } else {
    DINGO_LOG(DEBUG) << fmt::format(
        "[service.{}][request_id({})][elapsed(ns)({})] Request finish, response: {} request: {}", method_name_,
        request_->request_info().request_id(), elapsed_time,
        response_->ShortDebugString().substr(0, FLAGS_log_print_max_length),
        request_->ShortDebugString().substr(0, FLAGS_log_print_max_length));
  }

  if (BAIDU_UNLIKELY(FLAGS_enable_dump_service_message)) {
    ServiceHelper::DumpResponse(dump_name, response_);
  }
}

}  // namespace dingodb

#endif  // DINGODB_SERVER_SERVICE_HELPER_H_
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

#ifndef DINGODB_INDEX_SERVICE_H_
#define DINGODB_INDEX_SERVICE_H_

#include "engine/storage.h"
#include "proto/index.pb.h"
#include "vector/vector_index_manager.h"

namespace dingodb {

class IndexServiceImpl : public pb::index::IndexService {
 public:
  IndexServiceImpl();

  void Hello(google::protobuf::RpcController* controller, const pb::index::HelloRequest* request,
             pb::index::HelloResponse* response, google::protobuf::Closure* done) override;
  void GetMemoryInfo(google::protobuf::RpcController* controller, const pb::index::HelloRequest* request,
                     pb::index::HelloResponse* response, google::protobuf::Closure* done) override;

  // vector read
  void VectorBatchQuery(google::protobuf::RpcController* controller, const pb::index::VectorBatchQueryRequest* request,
                        pb::index::VectorBatchQueryResponse* response, google::protobuf::Closure* done) override;
  void VectorSearch(google::protobuf::RpcController* controller, const pb::index::VectorSearchRequest* request,
                    pb::index::VectorSearchResponse* response, google::protobuf::Closure* done) override;
  void VectorGetBorderId(google::protobuf::RpcController* controller,
                         const pb::index::VectorGetBorderIdRequest* request,
                         pb::index::VectorGetBorderIdResponse* response, google::protobuf::Closure* done) override;
  void VectorScanQuery(google::protobuf::RpcController* controller, const pb::index::VectorScanQueryRequest* request,
                       pb::index::VectorScanQueryResponse* response, google::protobuf::Closure* done) override;
  void VectorGetRegionMetrics(google::protobuf::RpcController* controller,
                              const pb::index::VectorGetRegionMetricsRequest* request,
                              pb::index::VectorGetRegionMetricsResponse* response,
                              google::protobuf::Closure* done) override;
  void VectorCount(google::protobuf::RpcController* controller, const pb::index::VectorCountRequest* request,
                   pb::index::VectorCountResponse* response, ::google::protobuf::Closure* done) override;

  void VectorCountMemory(google::protobuf::RpcController* controller,
                         const pb::index::VectorCountMemoryRequest* request,
                         pb::index::VectorCountMemoryResponse* response, ::google::protobuf::Closure* done) override;

  void VectorImport(google::protobuf::RpcController* controller, const pb::index::VectorImportRequest* request,
                    pb::index::VectorImportResponse* response, ::google::protobuf::Closure* done) override;

  void VectorBuild(google::protobuf::RpcController* controller, const pb::index::VectorBuildRequest* request,
                   pb::index::VectorBuildResponse* response, ::google::protobuf::Closure* done) override;

  void VectorLoad(google::protobuf::RpcController* controller, const pb::index::VectorLoadRequest* request,
                  pb::index::VectorLoadResponse* response, ::google::protobuf::Closure* done) override;

  void VectorStatus(google::protobuf::RpcController* controller, const pb::index::VectorStatusRequest* request,
                    pb::index::VectorStatusResponse* response, ::google::protobuf::Closure* done) override;

  void VectorReset(google::protobuf::RpcController* controller, const pb::index::VectorResetRequest* request,
                   pb::index::VectorResetResponse* response, ::google::protobuf::Closure* done) override;

  void VectorDump(google::protobuf::RpcController* controller, const pb::index::VectorDumpRequest* request,
                  pb::index::VectorDumpResponse* response, ::google::protobuf::Closure* done) override;

  // for debug
  void VectorSearchDebug(google::protobuf::RpcController* controller,
                         const pb::index::VectorSearchDebugRequest* request,
                         pb::index::VectorSearchDebugResponse* response, google::protobuf::Closure* done) override;

  // vector write
  void VectorAdd(google::protobuf::RpcController* controller, const pb::index::VectorAddRequest* request,
                 pb::index::VectorAddResponse* response, google::protobuf::Closure* done) override;
  void VectorDelete(google::protobuf::RpcController* controller, const pb::index::VectorDeleteRequest* request,
                    pb::index::VectorDeleteResponse* response, google::protobuf::Closure* done) override;

  // txn read
  void TxnGet(google::protobuf::RpcController* controller, const pb::store::TxnGetRequest* request,
              pb::store::TxnGetResponse* response, google::protobuf::Closure* done) override;
  void TxnScan(google::protobuf::RpcController* controller, const pb::store::TxnScanRequest* request,
               pb::store::TxnScanResponse* response, google::protobuf::Closure* done) override;
  void TxnBatchGet(google::protobuf::RpcController* controller, const pb::store::TxnBatchGetRequest* request,
                   pb::store::TxnBatchGetResponse* response, google::protobuf::Closure* done) override;
  void TxnScanLock(google::protobuf::RpcController* controller, const pb::store::TxnScanLockRequest* request,
                   pb::store::TxnScanLockResponse* response, google::protobuf::Closure* done) override;
  void TxnDump(google::protobuf::RpcController* controller, const pb::store::TxnDumpRequest* request,
               pb::store::TxnDumpResponse* response, google::protobuf::Closure* done) override;

  // txn write
  void TxnPessimisticLock(google::protobuf::RpcController* controller,
                          const pb::store::TxnPessimisticLockRequest* request,
                          pb::store::TxnPessimisticLockResponse* response, google::protobuf::Closure* done) override;
  void TxnPessimisticRollback(google::protobuf::RpcController* controller,
                              const pb::store::TxnPessimisticRollbackRequest* request,
                              pb::store::TxnPessimisticRollbackResponse* response,
                              google::protobuf::Closure* done) override;
  void TxnPrewrite(google::protobuf::RpcController* controller, const pb::store::TxnPrewriteRequest* request,
                   pb::store::TxnPrewriteResponse* response, google::protobuf::Closure* done) override;
  void TxnCommit(google::protobuf::RpcController* controller, const pb::store::TxnCommitRequest* request,
                 pb::store::TxnCommitResponse* response, google::protobuf::Closure* done) override;
  void TxnCheckTxnStatus(google::protobuf::RpcController* controller,
                         const pb::store::TxnCheckTxnStatusRequest* request,
                         pb::store::TxnCheckTxnStatusResponse* response, google::protobuf::Closure* done) override;
  void TxnResolveLock(google::protobuf::RpcController* controller, const pb::store::TxnResolveLockRequest* request,
                      pb::store::TxnResolveLockResponse* response, google::protobuf::Closure* done) override;
  void TxnBatchRollback(google::protobuf::RpcController* controller, const pb::store::TxnBatchRollbackRequest* request,
                        pb::store::TxnBatchRollbackResponse* response, google::protobuf::Closure* done) override;
  void TxnHeartBeat(google::protobuf::RpcController* controller, const pb::store::TxnHeartBeatRequest* request,
                    pb::store::TxnHeartBeatResponse* response, google::protobuf::Closure* done) override;
  void TxnGc(google::protobuf::RpcController* controller, const pb::store::TxnGcRequest* request,
             pb::store::TxnGcResponse* response, google::protobuf::Closure* done) override;
  void TxnDeleteRange(google::protobuf::RpcController* controller, const pb::store::TxnDeleteRangeRequest* request,
                      pb::store::TxnDeleteRangeResponse* response, google::protobuf::Closure* done) override;

  // backup & restore
  void BackupData(google::protobuf::RpcController* controller, const dingodb::pb::store::BackupDataRequest* request,
                  dingodb::pb::store::BackupDataResponse* response, google::protobuf::Closure* done) override;

  void ControlConfig(google::protobuf::RpcController* controller, const pb::store::ControlConfigRequest* request,
                     pb::store::ControlConfigResponse* response, google::protobuf::Closure* done) override;

  void RestoreData(google::protobuf::RpcController* controller, const dingodb::pb::store::RestoreDataRequest* request,
                   dingodb::pb::store::RestoreDataResponse* response, google::protobuf::Closure* done) override;

  void SetStorage(StoragePtr storage) { storage_ = storage; }
  void SetReadWorkSet(WorkerSetPtr worker_set) { read_worker_set_ = worker_set; }
  void SetWriteWorkSet(WorkerSetPtr worker_set) { write_worker_set_ = worker_set; }
  void SetVectorIndexManager(VectorIndexManagerPtr vector_index_manager) {
    vector_index_manager_ = vector_index_manager;
  }

  bool IsBackgroundPendingTaskCountExceed();

 private:
  StoragePtr storage_;
  // Run service request.
  WorkerSetPtr read_worker_set_;
  WorkerSetPtr write_worker_set_;
  VectorIndexManagerPtr vector_index_manager_;
};

}  // namespace dingodb

#endif  // DINGODB_INDEx_SERVICE_H_

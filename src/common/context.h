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

#ifndef DINGODB_COMMON_CONTEXT_H_
#define DINGODB_COMMON_CONTEXT_H_

#include <cstdint>
#include <memory>
#include <string>

#include "brpc/controller.h"
#include "common/stream.h"
#include "common/synchronization.h"
#include "common/tracker.h"
#include "diskann/diskann_utils.h"
#include "proto/common.pb.h"
#include "proto/store.pb.h"

#ifndef ENABLE_GC_MOCK
#define ENABLE_GC_MOCK
#endif

#undef ENABLE_GC_MOCK

#if defined(ENABLE_GC_MOCK)
#include "engine/raw_engine.h"
#endif

namespace dingodb {

class Context;

using WriteCbFunc = std::function<void(std::shared_ptr<Context>, butil::Status)>;

class Context {
 public:
  Context() { bthread_mutex_init(&cond_mutex_, nullptr); }
  Context(brpc::Controller* cntl, google::protobuf::Closure* done) : cntl_(cntl), done_(done) {
    bthread_mutex_init(&cond_mutex_, nullptr);
  }
  Context(brpc::Controller* cntl, google::protobuf::Closure* done, google::protobuf::Message* response)
      : cntl_(cntl), done_(done), response_(response) {
    bthread_mutex_init(&cond_mutex_, nullptr);
  }
  Context(brpc::Controller* cntl, google::protobuf::Closure* done, const google::protobuf::Message* request,
          google::protobuf::Message* response)
      : cntl_(cntl), done_(done), request_(request), response_(response) {
    bthread_mutex_init(&cond_mutex_, nullptr);
  }
  ~Context() { bthread_mutex_destroy(&cond_mutex_); }

  brpc::Controller* Cntl() { return cntl_; }
  Context& SetCntl(brpc::Controller* cntl) {
    cntl_ = cntl;
    return *this;
  }

  google::protobuf::Closure* Done() { return done_; }
  Context& SetDone(google::protobuf::Closure* done) {
    done_ = done;
    return *this;
  }

  const google::protobuf::Message* Request() { return request_; }

  google::protobuf::Message* Response() { return response_; }
  Context& SetResponse(google::protobuf::Message* response) {
    response_ = response;
    return *this;
  }

  int64_t RegionId() const { return region_id_; }
  Context& SetRegionId(int64_t region_id) {
    region_id_ = region_id;
    return *this;
  }

  TrackerPtr Tracker() { return tracker_; }
  void SetTracker(TrackerPtr tracker) { tracker_ = tracker; }

  pb::common::RegionEpoch RegionEpoch() const { return region_epoch_; }
  Context& SetRegionEpoch(const pb::common::RegionEpoch& region_epoch) {
    region_epoch_ = region_epoch;
    return *this;
  }

  pb::store::IsolationLevel IsolationLevel() const { return isolation_level_; }
  Context& SetIsolationLevel(const pb::store::IsolationLevel& isolation_level) {
    isolation_level_ = isolation_level;
    return *this;
  }

  void SetRawEngineType(pb::common::RawEngine raw_engine_type) { raw_engine_type_ = raw_engine_type; }
  pb::common::RawEngine RawEngineType() { return raw_engine_type_; }

  void SetStoreEngineType(pb::common::StorageEngine store_engine_type) { store_engine_type_ = store_engine_type; }
  pb::common::StorageEngine StoreEngineType() { return store_engine_type_; }

  Context& SetCfName(const std::string& cf_name) {
    cf_name_ = cf_name;
    return *this;
  }
  const std::string& CfName() const { return cf_name_; }

  void SetTs(int64_t ts) { ts_ = ts; }
  int64_t Ts() const { return ts_; }
  void SetTtl(int64_t ttl) { ttl_ = ttl; }
  int64_t Ttl() const { return ttl_; }

  bool DeleteFilesInRange() const { return delete_files_in_range_; }
  void SetDeleteFilesInRange(bool delete_files_in_range) { delete_files_in_range_ = delete_files_in_range; }

  bool Flush() const { return flush_; }
  void SetFlush(bool flush) { flush_ = flush; }

  BthreadCondPtr CreateSyncModeCond() {
    BAIDU_SCOPED_LOCK(cond_mutex_);
    cond_ = std::make_shared<BthreadCond>();
    return cond_;
  }
  BthreadCondPtr SyncModeCond() {
    BAIDU_SCOPED_LOCK(cond_mutex_);
    return cond_;
  }

  butil::Status Status() { return status_; }
  void SetStatus(butil::Status& status) { status_ = status; }
  void SetStatus(butil::Status&& status) { status_ = status; }  // NOLINT

  WriteCbFunc WriteCb() { return write_cb_; }
  void SetWriteCb(WriteCbFunc write_cb) { write_cb_ = write_cb; }

  StreamPtr Stream() { return stream_; }
  void SetStream(StreamPtr stream) { stream_ = stream; }
  DiskANNCoreState DiskANNCoreStateX() { return diskann_core_state_; };
  void SetDiskANNCoreStateX(enum DiskANNCoreState state) { diskann_core_state_ = state; }

#if defined(ENABLE_GC_MOCK)
  RawEngine::WriterPtr Writer() { return writer_; }
  void SetWriter(RawEngine::WriterPtr writer) { writer_ = writer; }
#endif

 private:
  // brpc framework free resource
  brpc::Controller* cntl_{nullptr};
  google::protobuf::Closure* done_{nullptr};
  const google::protobuf::Message* request_{nullptr};
  google::protobuf::Message* response_{nullptr};

  int64_t region_id_{0};
  // RawEngine type
  pb::common::RawEngine raw_engine_type_{pb::common::RAW_ENG_ROCKSDB};
  // StorageEngine type
  pb::common::StorageEngine store_engine_type_{pb::common::STORE_ENG_RAFT_STORE};
  // Column family name
  std::string cf_name_{};
  // Region epoch
  pb::common::RegionEpoch region_epoch_{};
  // Transaction isolation level
  pb::store::IsolationLevel isolation_level_{};

  int64_t ts_{0};
  int64_t ttl_{0};

  // Rocksdb delete range in files
  bool delete_files_in_range_{false};
  // Flush data to persistence.
  bool flush_{false};

  BthreadCondPtr cond_{nullptr};
  bthread_mutex_t cond_mutex_;
  butil::Status status_{};

  WriteCbFunc write_cb_{};

  StreamPtr stream_;

  TrackerPtr tracker_;

  enum DiskANNCoreState diskann_core_state_ {};

#if defined(ENABLE_GC_MOCK)
  RawEngine::WriterPtr writer_;
#endif
};

using ContextPtr = std::shared_ptr<Context>;

}  // namespace dingodb

#endif  // DINGODB_COMMON_CONTEXT_H_

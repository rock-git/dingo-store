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

#ifndef DINGODB_COMMON_CONSTANT_H_
#define DINGODB_COMMON_CONSTANT_H_

#include <cstdint>
#include <limits>
#include <string>

namespace dingodb {

class Constant {
 public:
  // Define Global Region Id for Coordinator(As only One)
  static const int64_t kMetaRegionId = 0;
  // Define Global Region Id for Kv
  static const int64_t kKvRegionId = 1;
  // Define Global Region Id for tso
  static const int64_t kTsoRegionId = 2;
  // Define Global Region Id for auto increment
  static const int64_t kAutoIncrementRegionId = 3;

  // Define Global TableID for Coordinator(As only one)
  static const int64_t kCoordinatorTableId = 0;

  // Define Global SchemaId for Coordinator(As only one)
  static const int64_t kCoordinatorSchemaId = 0;

  // Define Store data column family.
  // table and vector_index's vector use "default"
  inline static const std::string kStoreDataCF = "default";
  // Define Store meta column family.
  inline static const std::string kStoreMetaCF = "meta";
  // transaction cf names
  inline static const std::string kTxnDataCF = "data";
  inline static const std::string kTxnLockCF = "lock";
  inline static const std::string kTxnWriteCF = "write";

  // vector cf names
  inline static const std::string kVectorDataCF = "default";
  inline static const std::string kVectorScalarCF = "vector_scalar";
  inline static const std::string kVectorScalarKeySpeedUpCF = "vector_scalar_key_speed_up";
  inline static const std::string kVectorTableCF = "vector_table";

  // region range prefix
  inline static const char kExecutorRaw = 'r';
  inline static const char kExecutorTxn = 't';
  inline static const char kClientRaw = 'w';
  inline static const char kClientTxn = 'x';

  // vector key len
  inline static const uint32_t kVectorKeyMinLenWithPrefix = 9;
  inline static const uint32_t kVectorKeyMaxLenWithPrefix = 17;

  // document key len
  inline static const uint32_t kDocumentKeyMinLenWithPrefix = 9;
  inline static const uint32_t kDocumentKeyMaxLenWithPrefix = 17;

  // Define store meta prefix.
  inline static const std::string kStoreRegionMetaPrefix = "META_REGION";
  // Define region change record.
  inline static const std::string kStoreRegionChangeRecordPrefix = "META_CHANGE";
  // Define store raft prefix.
  inline static const std::string kStoreRaftMetaPrefix = "META_RAFT";
  // Define store region metrics prefix.
  inline static const std::string kStoreRegionMetricsPrefix = "METRICS_REGION";
  // Define region controller prefix.
  inline static const std::string kStoreRegionControlCommandPrefix = "CONTROL_CMD";
  // Define vector index apply max log prefix.
  inline static const std::string kVectorIndexApplyLogIdPrefix = "VECTOR_INDEX_APPLY_LOG";
  // Define vector index snapshot max log prefix.
  inline static const std::string kVectorIndexSnapshotLogIdPrefix = "VECTOR_INDEX_SNAPSHOT_LOG";

  // Define document index apply max log prefix.
  inline static const std::string kDocumentIndexApplyLogIdPrefix = "DOCUMENT_INDEX_APPLY_LOG";
  // Define document index snapshot max log prefix.
  inline static const std::string kDocumentIndexSnapshotLogIdPrefix = "DOCUMENT_INDEX_SNAPSHOT_LOG";

  // Define default raft snapshot policy
  inline static const std::string kDefaultRaftSnapshotPolicy = "checkpoint";

  // flat map init capacity
  static const int64_t kStoreRegionMetaInitCapacity = 1024;

  // internal schema name
  inline static const std::string kRootSchemaName = "ROOT";
  inline static const std::string kMetaSchemaName = "META";
  inline static const std::string kDingoSchemaName = "DINGO";
  inline static const std::string kMySQLSchemaName = "MYSQL";
  inline static const std::string kInformationSchemaName = "INFORMATION_SCHEMA";

  // rocksdb config
  inline static const std::string kStorePathConfigName = "store.path";
  inline static const std::string kColumnFamilies = "store.column_families";
  inline static const std::string kBaseColumnFamily = "store.base";

  inline static const std::string kBlockSize = "block_size";
  inline static const std::string kBlockSizeDefaultValue = "131072";  // 128KB
  inline static const std::string kBlockCache = "block_cache";
  inline static const std::string kBlockCacheDefaultValue = "1073741824";  // 1GB
  inline static const std::string kArenaBlockSize = "arena_block_size";
  inline static const std::string kArenaBlockSizeDefaultValue = "67108864";  // 64MB
  inline static const std::string kMinWriteBufferNumberToMerge = "min_write_buffer_number_to_merge";
  inline static const std::string kMinWriteBufferNumberToMergeDefaultValue = "2";
  inline static const std::string kMaxWriteBufferNumber = "max_write_buffer_number";
  inline static const std::string kMaxWriteBufferNumberDefaultValue = "5";
  inline static const std::string kMaxCompactionBytes = "max_compaction_bytes";
  inline static const std::string kMaxCompactionBytesDefaultValue = "1073741824";  // 1GB
  inline static const std::string kWriteBufferSize = "write_buffer_size";
  inline static const std::string kWriteBufferSizeDefaultValue = "67108864";  // 64MB
  inline static const std::string kPrefixExtractor = "prefix_extractor";
  inline static const std::string kPrefixExtractorDefaultValue = "24";
  inline static const std::string kMaxBytesForLevelBase = "max_bytes_for_level_base";
  inline static const std::string kMaxBytesForLevelBaseDefaultValue = "134217728";  // 128MB
  inline static const std::string kTargetFileSizeBase = "target_file_size_base";
  inline static const std::string kTargetFileSizeBaseDefaultValue = "67108864";  // 64MB
  inline static const std::string kMaxBytesForLevelMultiplier = "max_bytes_for_level_multiplier";
  inline static const std::string kMaxBytesForLevelMultiplierDefaultValue = "10";

  static const int kRocksdbBackgroundThreadNumDefault = 16;
  static const int kStatsDumpPeriodSecDefault = 600;

  // scan config
  inline static const std::string kStoreScan = "store.scan";
  inline static const std::string kStoreScanV2 = "store.scan_v2";
  inline static const std::string kStoreScanTimeoutS = "timeout_s";
  inline static const std::string kStoreScanMaxBytesRpc = "max_bytes_rpc";
  inline static const std::string kStoreScanMaxFetchCntByServer = "max_fetch_cnt_by_server";
  inline static const std::string kStoreScanScanIntervalS = "scan_interval_s";

  inline static const std::string kMetaRegionName = "0-META";
  inline static const std::string kKvRegionName = "1-KV";
  inline static const std::string kTsoRegionName = "2-TSO";
  inline static const std::string kAutoIncrementRegionName = "3-AUTO_INCREMENT";

  // segment log
  static const uint32_t kSegmentLogDefaultMaxSegmentSize = 8 * 1024 * 1024;  // 8M
  static constexpr bool kSegmentLogSync = true;
  static const uint32_t kSegmentLogSyncPerBytes = INT32_MAX;

  // vector key prefix
  static const uint8_t kVectorDataPrefix = 0x01;
  static const uint8_t kVectorScalarPrefix = 0x02;
  static const uint8_t kVectorTablePrefix = 0x03;

  // File transport chunk size
  static const uint32_t kFileTransportChunkSize = 1024 * 1024;  // 1M

  // vector limitations
  static const uint32_t kVectorMaxDimension = 32768;
  static constexpr int64_t kVectorIndexSaveSnapshotThresholdWriteKeyNum = 100000;

  // document limitations
  static constexpr int64_t kDocumentIndexSaveSnapshotThresholdWriteKeyNum = 1000;

  static const uint32_t kLoadOrBuildVectorIndexConcurrency = 5;

  static const uint32_t kBuildVectorIndexBatchSize = 32768;

  static const uint32_t kBuildDocumentIndexBatchSize = 32768;

  static constexpr int32_t kCreateIvfFlatParamNcentroids = 2048;
  static constexpr int32_t kSearchIvfFlatParamNprobe = 80;

  static constexpr int32_t kCreateBinaryIvfFlatParamNcentroids = 2048;
  static constexpr int32_t kSearchBinaryIvfFlatParamNprobe = 80;

  static constexpr int32_t kCreateIvfPqParamNcentroids = 2048;
  static constexpr int32_t kCreateIvfPqParamNsubvector = 64;
  static constexpr int32_t kCreateIvfPqParamNbitsPerIdx = 8;
  static constexpr int32_t kCreateIvfPqParamNbitsPerIdxMaxWarning = 16;

  static constexpr int32_t kSearchIvfPqParamNprobe = 80;

  // split region
  static constexpr int kSplitDoSnapshotRetryTimes = 5;
  inline static const std::string kSplitStrategy = "PRE_CREATE_REGION";
  static constexpr uint32_t kDefaultSplitCheckConcurrency = 3;
  inline static const std::string kDefaultSplitPolicy = "HALF";
  static constexpr uint32_t kRegionMaxSizeDefaultValue = 67108864;  // 64M
  static constexpr float kDefaultSplitCheckApproximateSizeRatio = 0.8;
  static constexpr uint32_t kSplitChunkSizeDefaultValue = 1048576;  // 1M
  static constexpr float kSplitRatioDefaultValue = 0.5;
  static constexpr uint32_t kSplitKeysNumberDefaultValue = 100000;
  static constexpr float kSplitKeysRatioDefaultValue = 0.5;

  // merge region
  static constexpr uint32_t kAutoMergeRegionMaxSizeDefaultValue = 1048576;  // 1M
  static constexpr uint32_t kAutoMergeRegionMaxKeysCountDefaultValue = 10000;
  static constexpr uint32_t kSplitMergeIntervalDefaultValue = 3600;  // 1h
  static constexpr uint32_t kDefaultMergeCheckConcurrency = 3;
  static constexpr int64_t kRegionMetricsUpdateSecondDefaultValue = 60;  // 60s
  static constexpr float kMergeRatioDefaultValue = 0.2;
  static constexpr float kMergeKeysRatioDefaultValue = 0.2;

  static const int32_t kRaftLogFallBehindThreshold = 1000;
  static const int32_t kTransferLeaderRaftLogFallBehindThreshold = 16;

  static constexpr int64_t kLockVer = INT64_MAX;
  static constexpr int64_t kMaxVer = INT64_MAX;

  // hnsw max elements expand number
  static const uint32_t kHnswMaxElementsExpandNum = 10000;

  // raft snapshot
  inline static const std::string kRaftSnapshotRegionMetaFileName = "region_meta";
  inline static const std::string kRaftSnapshotRegionDateFileNameSuffix = ".dingo_sst";

  static constexpr uint32_t kCollectApproximateSizeBatchSize = 1024;
  static constexpr int64_t kVectorIndexSnapshotCatchupMargin = 4000;

  static constexpr uint32_t kRaftElectionTimeoutSDefaultValue = 6;

  static constexpr int32_t kVectorIndexTaskRunningNumExpectValue = 6;

  static constexpr uint32_t kLogPrintMaxLength = 256;

  // raft snapshot policy string
  inline static const std::string kRaftSnapshotPolicyDingo = "dingo";
  inline static const std::string kRaftSnapshotPolicyCheckpoint = "checkpoint";
  inline static const std::string kRaftSnapshotPolicyScan = "scan";

  // gc stop
  inline static const std::string kGcStopKey = "GC_STOP";
  inline static const std::string kGcStopValueTrue = "GC_STOP_TRUE";
  inline static const std::string kGcStopValueFalse = "GC_STOP_FALSE";

  // balance region
  inline static const float kBalanceRegionDefaultRegionCountRatio = 0.8f;
  inline static const int32_t kBalanceRegionDefaultIndexRegionSize = 536870912;  // 512M
  inline static const int32_t kBalanceRegionDefaultStoreRegionSize = 268435456;  // 256M

  // force_read_only
  inline static const std::string kForceReadOnlyKey = "FORCE_READ_ONLY";
  inline static const std::string kForceReadOnlyReason = "FORCE_READ_REASON";
  inline static const std::string kForceReadOnlyValueTrue = "TRUE";
  inline static const std::string kForceReadOnlyValueFalse = "FALSE";

  static const uint32_t kRandomElectionTimeoutMinDeltaMs = 2000;
  static const uint32_t kRandomElectionTimeoutMaxDeltaMs = 7000;

  static const uint32_t kLeaderNumWeightDefaultValue = 1;

  static const uint32_t kReserveJobRecentDayDefaultValue = 7;

  // diskann
  // min count of vectors for diskann
  // do not change this parameter or it will crash.
  inline static const int64_t kDiskannMinCount = 2;
  // max count of vectors for diskann
  inline static const int64_t kDiskannMaxCount = std::numeric_limits<int32_t>::max();
  inline static const std::string kDiskannStore = "store";
  inline static const std::string kDiskannPathConfigName = "path";
  inline static const std::string kDiskannNumThreadsConfigName = "num_threads";
  inline static const std::string kDiskannSearchDramBudgetGbConfigName = "search_dram_budget_gb";
  inline static const std::string kDiskannBuildDramBudgetGbConfigName = "build_dram_budget_gb";
  inline static const std::string kDiskannImportTimeoutSecondConfigName = "import_timeout_s";
  inline static const uint32_t kDiskannNumThreadsDefaultValue = 64;
  inline static const float kDiskannSearchDramBudgetGbDefaultValue = 1.0f;
  inline static const float kDiskannBuildDramBudgetGbDefaultValue = 10.0f;
  inline static const int64_t kDiskannImportTimeoutSecondDefaultValue = 30;

  // tenant
  inline static const int64_t kDefaultTenantId = 0;

  // backup & restore
  inline static const std::string kStoreRegionName = "store";
  inline static const std::string kIndexRegionName = "index";
  inline static const std::string kDocumentRegionName = "document";
  inline static const std::string kCoordinatorRegionName = "coordinator";
  inline static const std::string kBackupRegionName = "backup";

  inline static const std::string kSdkData = "sdk";
  inline static const std::string kSqlData = "sql";

  inline static const std::string kCoordinatorSdkMetaKeyName = "SDK_META";
  inline static const std::string kCoordinatorSdkMetaSstName = "coordinator_sdk_meta.sst";

  inline static const std::string kStoreRegionSqlMetaSstName = "store_region_sql_meta.sst";
  inline static const std::string kStoreCfSstMetaSqlMetaSstName = "store_cf_sst_meta_sql_meta.sst";

  inline static const std::string kRestoreData = "RESTORE_DATA";
  inline static const std::string kRestoreMeta = "RESTORE_META";

  // sdk data
  inline static const std::string kStoreRegionSdkDataSstName = "store_region_sdk_data.sst";
  inline static const std::string kStoreCfSstMetaSdkDataSstName = "store_cf_sst_meta_sdk_data.sst";
  inline static const std::string kIndexRegionSdkDataSstName = "index_region_sdk_data.sst";
  inline static const std::string kIndexCfSstMetaSdkDataSstName = "index_cf_sst_meta_sdk_data.sst";
  inline static const std::string kDocumentRegionSdkDataSstName = "document_region_sdk_data.sst";
  inline static const std::string kDocumentCfSstMetaSdkDataSstName = "document_cf_sst_meta_sdk_data.sst";

  // sql data
  inline static const std::string kStoreRegionSqlDataSstName = "store_region_sql_data.sst";
  inline static const std::string kStoreCfSstMetaSqlDataSstName = "store_cf_sst_meta_sql_data.sst";
  inline static const std::string kIndexRegionSqlDataSstName = "index_region_sql_data.sst";
  inline static const std::string kIndexCfSstMetaSqlDataSstName = "index_cf_sst_meta_sql_data.sst";
  inline static const std::string kDocumentRegionSqlDataSstName = "document_region_sql_data.sst";
  inline static const std::string kDocumentCfSstMetaSqlDataSstName = "document_cf_sst_meta_sql_data.sst";

  inline static const std::string kBackupMetaDataFileName = "backupmeta.datafile";
  inline static const std::string kBackupMetaSchemaName = "backupmeta.schema";
  inline static const std::string kBackupMetaName = "backupmeta";
  inline static const std::string kBackupMetaEncryptionName = "backupmeta.encryption";
  inline static const std::string kBackupMetaDebugName = "backupmeta.debug";

  inline static const std::string kIdEpochTypeAndValueKey = "dingodb::pb::meta::IdEpochTypeAndValue";
  inline static const std::string kTableIncrementKey = "dingodb::pb::meta::TableIncrementGroup";

  inline static const std::string kBackupVersionKey = "pb::common::VersionInfo";
  inline static const std::string kBackupBackupParamKey = "pb::common::BackupParam";
};

}  // namespace dingodb

#endif  // DINGODB_COMMON_CONSTANT_H_

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

#ifndef DINGODB_VECTOR_INDEX_UTILS_H_
#define DINGODB_VECTOR_INDEX_UTILS_H_

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "butil/status.h"
#include "common/threadpool.h"
#include "faiss/Index.h"
#include "faiss/IndexBinary.h"
#include "faiss/impl/AuxIndexStructures.h"
#include "proto/common.pb.h"
#include "proto/index.pb.h"

namespace dingodb {

class VectorIndexUtils {
 public:
  VectorIndexUtils() = delete;
  ~VectorIndexUtils() = delete;

  VectorIndexUtils(const VectorIndexUtils& rhs) = delete;
  VectorIndexUtils& operator=(const VectorIndexUtils& rhs) = delete;
  VectorIndexUtils(VectorIndexUtils&& rhs) = delete;
  VectorIndexUtils& operator=(VectorIndexUtils&& rhs) = delete;

  static butil::Status CalcDistanceEntry(const ::dingodb::pb::index::VectorCalcDistanceRequest& request,
                                         std::vector<std::vector<float>>& distances,
                                         std::vector<::dingodb::pb::common::Vector>& result_op_left_vectors,
                                         std::vector<::dingodb::pb::common::Vector>& result_op_right_vectors);

  using DoCalcDistanceFunc =
      std::function<butil::Status(const ::dingodb::pb::common::Vector&, const ::dingodb::pb::common::Vector&, bool,
                                  float&, dingodb::pb::common::Vector&, dingodb::pb::common::Vector&)>;
  static butil::Status CalcDistanceCore(
      const google::protobuf::RepeatedPtrField<::dingodb::pb::common::Vector>& op_left_vectors,
      const google::protobuf::RepeatedPtrField<::dingodb::pb::common::Vector>& op_right_vectors,
      bool is_return_normlize, std::vector<std::vector<float>>& distances,
      std::vector<::dingodb::pb::common::Vector>& result_op_left_vectors,
      std::vector<::dingodb::pb::common::Vector>& result_op_right_vectors, DoCalcDistanceFunc do_calc_distance_func);

  static butil::Status CalcDistanceByFaiss(
      pb::common::MetricType metric_type,
      const google::protobuf::RepeatedPtrField<::dingodb::pb::common::Vector>& op_left_vectors,
      const google::protobuf::RepeatedPtrField<::dingodb::pb::common::Vector>& op_right_vectors,
      bool is_return_normlize, std::vector<std::vector<float>>& distances,
      std::vector<::dingodb::pb::common::Vector>& result_op_left_vectors,
      std::vector<::dingodb::pb::common::Vector>& result_op_right_vectors);

  static butil::Status CalcDistanceByHnswlib(
      pb::common::MetricType metric_type,
      const google::protobuf::RepeatedPtrField<::dingodb::pb::common::Vector>& op_left_vectors,
      const google::protobuf::RepeatedPtrField<::dingodb::pb::common::Vector>& op_right_vectors,
      bool is_return_normlize, std::vector<std::vector<float>>& distances,
      std::vector<::dingodb::pb::common::Vector>& result_op_left_vectors,
      std::vector<::dingodb::pb::common::Vector>& result_op_right_vectors);

  static butil::Status CalcL2DistanceByFaiss(
      const google::protobuf::RepeatedPtrField<::dingodb::pb::common::Vector>& op_left_vectors,
      const google::protobuf::RepeatedPtrField<::dingodb::pb::common::Vector>& op_right_vectors,
      bool is_return_normlize, std::vector<std::vector<float>>& distances,
      std::vector<::dingodb::pb::common::Vector>& result_op_left_vectors,
      std::vector<::dingodb::pb::common::Vector>& result_op_right_vectors);

  static butil::Status CalcIpDistanceByFaiss(
      const google::protobuf::RepeatedPtrField<::dingodb::pb::common::Vector>& op_left_vectors,
      const google::protobuf::RepeatedPtrField<::dingodb::pb::common::Vector>& op_right_vectors,
      bool is_return_normlize, std::vector<std::vector<float>>& distances,
      std::vector<::dingodb::pb::common::Vector>& result_op_left_vectors,
      std::vector<::dingodb::pb::common::Vector>& result_op_right_vectors);

  static butil::Status CalcCosineDistanceByFaiss(
      const google::protobuf::RepeatedPtrField<::dingodb::pb::common::Vector>& op_left_vectors,
      const google::protobuf::RepeatedPtrField<::dingodb::pb::common::Vector>& op_right_vectors,
      bool is_return_normlize, std::vector<std::vector<float>>& distances,
      std::vector<::dingodb::pb::common::Vector>& result_op_left_vectors,
      std::vector<::dingodb::pb::common::Vector>& result_op_right_vectors);

  static butil::Status CalcHammingDistanceByFaiss(
      const google::protobuf::RepeatedPtrField<::dingodb::pb::common::Vector>& op_left_vectors,
      const google::protobuf::RepeatedPtrField<::dingodb::pb::common::Vector>& op_right_vectors,
      bool is_return_normlize, std::vector<std::vector<float>>& distances,
      std::vector<::dingodb::pb::common::Vector>& result_op_left_vectors,
      std::vector<::dingodb::pb::common::Vector>& result_op_right_vectors);

  static butil::Status CalcL2DistanceByHnswlib(
      const google::protobuf::RepeatedPtrField<::dingodb::pb::common::Vector>& op_left_vectors,
      const google::protobuf::RepeatedPtrField<::dingodb::pb::common::Vector>& op_right_vectors,
      bool is_return_normlize, std::vector<std::vector<float>>& distances,
      std::vector<::dingodb::pb::common::Vector>& result_op_left_vectors,
      std::vector<::dingodb::pb::common::Vector>& result_op_right_vectors);

  static butil::Status CalcIpDistanceByHnswlib(
      const google::protobuf::RepeatedPtrField<::dingodb::pb::common::Vector>& op_left_vectors,
      const google::protobuf::RepeatedPtrField<::dingodb::pb::common::Vector>& op_right_vectors,
      bool is_return_normlize, std::vector<std::vector<float>>& distances,
      std::vector<::dingodb::pb::common::Vector>& result_op_left_vectors,
      std::vector<::dingodb::pb::common::Vector>& result_op_right_vectors);

  static butil::Status CalcCosineDistanceByHnswlib(
      const google::protobuf::RepeatedPtrField<::dingodb::pb::common::Vector>& op_left_vectors,
      const google::protobuf::RepeatedPtrField<::dingodb::pb::common::Vector>& op_right_vectors,
      bool is_return_normlize, std::vector<std::vector<float>>& distances,
      std::vector<::dingodb::pb::common::Vector>& result_op_left_vectors,
      std::vector<::dingodb::pb::common::Vector>& result_op_right_vectors);

  // internal api

  static butil::Status DoCalcL2DistanceByFaiss(const ::dingodb::pb::common::Vector& op_left_vectors,
                                               const ::dingodb::pb::common::Vector& op_right_vectors,
                                               bool is_return_normlize, float& distance,
                                               dingodb::pb::common::Vector& result_op_left_vectors,
                                               dingodb::pb::common::Vector& result_op_right_vectors);

  static butil::Status DoCalcIpDistanceByFaiss(const ::dingodb::pb::common::Vector& op_left_vectors,
                                               const ::dingodb::pb::common::Vector& op_right_vectors,
                                               bool is_return_normlize, float& distance,
                                               dingodb::pb::common::Vector& result_op_left_vectors,
                                               dingodb::pb::common::Vector& result_op_right_vectors);
  static butil::Status DoCalcCosineDistanceByFaiss(const ::dingodb::pb::common::Vector& op_left_vectors,
                                                   const ::dingodb::pb::common::Vector& op_right_vectors,
                                                   bool is_return_normlize, float& distance,
                                                   dingodb::pb::common::Vector& result_op_left_vectors,
                                                   dingodb::pb::common::Vector& result_op_right_vectors);

  static butil::Status DoCalcHammingDistanceByFaiss(const ::dingodb::pb::common::Vector& op_left_vectors,
                                                    const ::dingodb::pb::common::Vector& op_right_vectors,
                                                    bool is_return_normlize, float& distance,
                                                    dingodb::pb::common::Vector& result_op_left_vectors,
                                                    dingodb::pb::common::Vector& result_op_right_vectors);

  static butil::Status DoCalcL2DistanceByHnswlib(const ::dingodb::pb::common::Vector& op_left_vectors,
                                                 const ::dingodb::pb::common::Vector& op_right_vectors,
                                                 bool is_return_normlize, float& distance,
                                                 dingodb::pb::common::Vector& result_op_left_vectors,
                                                 dingodb::pb::common::Vector& result_op_right_vectors);

  static butil::Status DoCalcIpDistanceByHnswlib(const ::dingodb::pb::common::Vector& op_left_vectors,
                                                 const ::dingodb::pb::common::Vector& op_right_vectors,
                                                 bool is_return_normlize, float& distance,
                                                 dingodb::pb::common::Vector& result_op_left_vectors,
                                                 dingodb::pb::common::Vector& result_op_right_vectors);

  static butil::Status DoCalcCosineDistanceByHnswlib(const ::dingodb::pb::common::Vector& op_left_vectors,
                                                     const ::dingodb::pb::common::Vector& op_right_vectors,
                                                     bool is_return_normlize, float& distance,
                                                     dingodb::pb::common::Vector& result_op_left_vectors,
                                                     dingodb::pb::common::Vector& result_op_right_vectors);

  static void ResultOpVectorAssignment(dingodb::pb::common::Vector& result_op_vectors,
                                       const ::dingodb::pb::common::Vector& op_vectors);
  static void ResultOpBinaryVectorAssignment(dingodb::pb::common::Vector& result_op_vectors,
                                             const ::dingodb::pb::common::Vector& op_vectors);

  static void ResultOpVectorAssignmentWrapper(const ::dingodb::pb::common::Vector& op_left_vectors,
                                              const ::dingodb::pb::common::Vector& op_right_vectors,
                                              bool is_return_normlize,
                                              dingodb::pb::common::Vector& result_op_left_vectors,
                                              dingodb::pb::common::Vector& result_op_right_vectors);

  static void ResultOpBinaryVectorAssignmentWrapper(const ::dingodb::pb::common::Vector& op_left_vectors,
                                                    const ::dingodb::pb::common::Vector& op_right_vectors,
                                                    bool is_return_normlize,
                                                    dingodb::pb::common::Vector& result_op_left_vectors,
                                                    dingodb::pb::common::Vector& result_op_right_vectors);

  static void NormalizeVectorForFaiss(float* x, int32_t d);
  static void NormalizeVectorForHnsw(const float* data, uint32_t dimension, float* norm_array);

  static butil::Status CheckVectorDimension(const std::vector<pb::common::VectorWithId>& vector_with_ids,
                                            int dimension);

  static std::unique_ptr<faiss::idx_t[]> CastVectorId(const std::vector<int64_t>& delete_ids);

  static std::unique_ptr<faiss::idx_t[]> ExtractVectorId(const std::vector<pb::common::VectorWithId>& vector_with_ids);
  static butil::Status CheckVectorIdDuplicated(const std::unique_ptr<faiss::idx_t[]>& ids, size_t size);

  template <typename T>
  static std::unique_ptr<T[]> ExtractVectorValue(const std::vector<pb::common::VectorWithId>& vector_with_ids,
                                                 faiss::idx_t dimension, bool normalize = false);

  template <typename T>
  static butil::Status FillSearchResult(const std::vector<pb::common::VectorWithId>& vector_with_ids, uint32_t topk,
                                        const std::vector<T>& distances, const std::vector<faiss::idx_t>& labels,
                                        pb::common::MetricType metric_type, faiss::idx_t dimension,
                                        std::vector<pb::index::VectorWithDistanceResult>& results);

  static butil::Status FillRangeSearchResult(const std::unique_ptr<faiss::RangeSearchResult>& range_search_result,
                                             pb::common::MetricType metric_type, faiss::idx_t dimension,
                                             std::vector<pb::index::VectorWithDistanceResult>& results);
  static butil::Status CheckVectorIndexParameterCompatibility(const pb::common::VectorIndexParameter& source,
                                                              const pb::common::VectorIndexParameter& target);
  static butil::Status ValidateVectorIndexParameter(const pb::common::VectorIndexParameter& vector_index_parameter);
  static butil::Status ValidateDiskannParameter(const pb::common::VectorIndexParameter& vector_index_parameter);

  static butil::Status ValidateVectorScalarSchema(const pb::common::ScalarSchema& scalar_schema);

  static butil::Status ValidateVectorScalarData(const pb::common::ScalarSchema& scalar_schema,
                                                const pb::common::VectorScalardata& vector_scalar_data);

  static butil::Status SplitVectorScalarData(
      const pb::common::ScalarSchema& scalar_schema, const pb::common::VectorScalardata& vector_scalar_data,
      std::vector<std::pair<std::string, pb::common::ScalarValue>>& scalar_key_value_pairs);  // NOLINT

  static butil::Status IsNeedToScanKeySpeedUpCF(const pb::common::ScalarSchema& scalar_schema,
                                                const pb::common::CoprocessorV2& coprocessor_v2,
                                                bool& is_need);  // NOLINT
  static butil::Status IsNeedToScanKeySpeedUpCF(const pb::common::ScalarSchema& scalar_schema,
                                                const pb::common::VectorScalardata& vector_scalar_data,
                                                bool& is_need);  // NOLINT
};

}  // namespace dingodb

#endif  // DINGODB_VECTOR_INDEX_UTILS_H_

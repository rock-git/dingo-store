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

#include "coprocessor/rel_expr_helper.h"

#include <cstdint>
#include <vector>

#include "common/logging.h"
#include "fmt/core.h"
#include "proto/error.pb.h"
#include "serial/schema/base_schema.h"
#include "serial/record/V2/common.h"

namespace dingodb {

template <typename T>
expr::Operand ToOperandV2(const std::any& v) {
  if (v.has_value()) {
    return std::any_cast<T>(v);
  }
  return nullptr;
}

template <typename T>
std::any FromOperandV2(const expr::Operand& v) {
  if (v != nullptr) {
    auto opt = v.GetValue<T>();
    return std::make_any<T>(opt);
  } else {
    return std::any();
  }
}

butil::Status RelExprHelper::TransToOperand(
    BaseSchema::Type type, const std::any& column,
    std::unique_ptr<std::vector<expr::Operand>>& operand_ptr) {
  if (!operand_ptr) {
    std::string s = fmt::format("operand_ptr is nullptr. not support");
    DINGO_LOG(ERROR) << s;
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
  }

  switch (type) {
    case BaseSchema::Type::kBool: {
      try {
        operand_ptr->emplace_back(
            expr::any_optional_data_adaptor::ToOperand<bool>(column));
      } catch (const std::bad_any_cast& bad) {
        std::string s =
            fmt::format("{}  any_cast std::optional<bool> failed", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kInteger: {
      try {
        operand_ptr->emplace_back(
            expr::any_optional_data_adaptor::ToOperand<int32_t>(column));
      } catch (const std::bad_any_cast& bad) {
        std::string s = fmt::format(
            "{}  any_cast std::optional<int32_t> failed", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kFloat: {
      try {
        operand_ptr->emplace_back(
            expr::any_optional_data_adaptor::ToOperand<float>(column));
      } catch (const std::bad_any_cast& bad) {
        std::string s =
            fmt::format("{}  any_cast std::optional<float> failed", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kLong: {
      try {
        operand_ptr->emplace_back(
            expr::any_optional_data_adaptor::ToOperand<int64_t>(column));

      } catch (const std::bad_any_cast& bad) {
        std::string s = fmt::format(
            "{}  any_cast std::optional<int64_t> failed", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kDouble: {
      try {
        operand_ptr->emplace_back(
            expr::any_optional_data_adaptor::ToOperand<double>(column));
      } catch (const std::bad_any_cast& bad) {
        std::string s = fmt::format("{}  any_cast std::optional<double> failed",
                                    bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kString: {
      try {
        operand_ptr->emplace_back(expr::any_optional_data_adaptor::ToOperand<
                                  std::shared_ptr<std::string>>(column));
      } catch (const std::bad_any_cast& bad) {
        std::string s = fmt::format(
            "{}  any_cast std::optional<std::shared_ptr<std::string>> failed",
            bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kBoolList: {
      try {
        operand_ptr->emplace_back(expr::any_optional_data_adaptor::ToOperand<
                                  std::shared_ptr<std::vector<bool>>>(column));
      } catch (const std::bad_any_cast& bad) {
        std::string s = fmt::format("Trans to Operand failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kIntegerList: {
      try {
        operand_ptr->emplace_back(
            expr::any_optional_data_adaptor::ToOperand<
                std::shared_ptr<std::vector<int32_t>>>(column));
      } catch (const std::bad_any_cast& bad) {
        std::string s = fmt::format("Trans to Operand failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kFloatList: {
      try {
        operand_ptr->emplace_back(expr::any_optional_data_adaptor::ToOperand<
                                  std::shared_ptr<std::vector<float>>>(column));
      } catch (const std::bad_any_cast& bad) {
        std::string s = fmt::format("Trans to Operand failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kLongList: {
      try {
        operand_ptr->emplace_back(
            expr::any_optional_data_adaptor::ToOperand<
                std::shared_ptr<std::vector<int64_t>>>(column));
      } catch (const std::bad_any_cast& bad) {
        std::string s = fmt::format("Trans to Operand failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kDoubleList: {
      try {
        operand_ptr->emplace_back(
            expr::any_optional_data_adaptor::ToOperand<
                std::shared_ptr<std::vector<double>>>(column));
      } catch (const std::bad_any_cast& bad) {
        std::string s = fmt::format("Trans to Operand failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kStringList: {
      try {
        operand_ptr->emplace_back(
            expr::any_optional_data_adaptor::ToOperand<
                std::shared_ptr<std::vector<std::string>>>(column));
      } catch (const std::bad_any_cast& bad) {
        std::string s = fmt::format("Trans to Operand failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    default: {
      std::string s = fmt::format("CloneColumn unsupported type  {}",
                                  BaseSchema::GetTypeString(type));
      DINGO_LOG(ERROR) << s;
      return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
    }
  }

  return butil::Status();
}

butil::Status RelExprHelper::TransToOperandV2(
    BaseSchema::Type type, const std::any& column,
    std::unique_ptr<std::vector<expr::Operand>>& operand_ptr) {
  if (!operand_ptr) {
    std::string s = fmt::format("operand_ptr is nullptr. not support");
    DINGO_LOG(ERROR) << s;
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
  }

  const std::any emptyOperand = std::any();

  switch (type) {
    case BaseSchema::Type::kBool: {
      try {
        operand_ptr->emplace_back(ToOperandV2<bool>(column));
      } catch (const std::bad_any_cast& bad) {
        std::string s =
            fmt::format("{}  any_cast std::optional<bool> failed", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kInteger: {
      try {
        operand_ptr->emplace_back(ToOperandV2<int32_t>(column));
      } catch (const std::bad_any_cast& bad) {
        std::string s = fmt::format(
            "{}  any_cast std::optional<int32_t> failed", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kFloat: {
      try {
        operand_ptr->emplace_back(ToOperandV2<float>(column));
      } catch (const std::bad_any_cast& bad) {
        std::string s =
            fmt::format("{}  any_cast std::optional<float> failed", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kLong: {
      try {
        operand_ptr->emplace_back(ToOperandV2<int64_t>(column));
      } catch (const std::bad_any_cast& bad) {
        std::string s = fmt::format(
            "{}  any_cast std::optional<int64_t> failed", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kDouble: {
      try {
        operand_ptr->emplace_back(ToOperandV2<double>(column));
      } catch (const std::bad_any_cast& bad) {
        std::string s = fmt::format("{}  any_cast std::optional<double> failed",
                                    bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kString: {
      try {
        if (column.has_value()) {
          auto col_value = std::make_shared<std::string>(
              std::any_cast<std::string>(column));
          operand_ptr->emplace_back(col_value);
        } else {
          operand_ptr->emplace_back(nullptr);
        }
      } catch (const std::bad_any_cast& bad) {
        std::string s = fmt::format(
            "{}  any_cast std::optional<std::shared_ptr<std::string>> failed",
            bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kBoolList: {
      try {
        if (column.has_value()) {
          auto col_value = std::make_shared<std::vector<bool>>(
              std::any_cast<std::vector<bool>>(column));
          operand_ptr->emplace_back(col_value);
        } else {
          operand_ptr->emplace_back(nullptr);
        }

      } catch (const std::bad_any_cast& bad) {
        std::string s = fmt::format("Trans to Operand failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kIntegerList: {
      try {
        if (column.has_value()) {
          auto col_value = std::make_shared<std::vector<int32_t>>(
              std::any_cast<std::vector<int32_t>>(column));
          operand_ptr->emplace_back(col_value);
        } else {
          operand_ptr->emplace_back(nullptr);
        }

      } catch (const std::bad_any_cast& bad) {
        std::string s = fmt::format("Trans to Operand failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kFloatList: {
      try {
        if (column.has_value()) {
          auto col_value = std::make_shared<std::vector<float>>(
              std::any_cast<std::vector<float>>(column));
          operand_ptr->emplace_back(col_value);
        } else {
          operand_ptr->emplace_back(nullptr);
        }

      } catch (const std::bad_any_cast& bad) {
        std::string s = fmt::format("Trans to Operand failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kLongList: {
      try {
        if (column.has_value()) {
          auto col_value = std::make_shared<std::vector<int64_t>>(
              std::any_cast<std::vector<int64_t>>(column));
          operand_ptr->emplace_back(col_value);
        } else {
          operand_ptr->emplace_back(nullptr);
        }

      } catch (const std::bad_any_cast& bad) {
        std::string s = fmt::format("Trans to Operand failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kDoubleList: {
      try {
        if (column.has_value()) {
          auto col_value = std::make_shared<std::vector<double>>(
              std::any_cast<std::vector<double>>(column));
          operand_ptr->emplace_back(col_value);
        } else {
          operand_ptr->emplace_back(nullptr);
        }

      } catch (const std::bad_any_cast& bad) {
        std::string s = fmt::format("Trans to Operand failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kStringList: {
      try {
        if (column.has_value()) {
          auto col_value = std::make_shared<std::vector<std::string>>(
              std::any_cast<std::vector<std::string>>(column));
          operand_ptr->emplace_back(col_value);
        } else {
          operand_ptr->emplace_back(nullptr);
        }

      } catch (const std::bad_any_cast& bad) {
        std::string s = fmt::format("Trans to Operand failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    default: {
      std::string s = fmt::format("CloneColumn unsupported type  {}",
                                  BaseSchema::GetTypeString(type));
      DINGO_LOG(ERROR) << s;
      return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
    }
  }

  return butil::Status();
}

butil::Status RelExprHelper::TransFromOperand(
    BaseSchema::Type type,
    const std::unique_ptr<std::vector<expr::Operand>>& operand_ptr,
    size_t index, std::vector<std::any>& columns) {
  if (!operand_ptr) {
    std::string s = fmt::format("operand_ptr is nullptr. not support");
    DINGO_LOG(ERROR) << s;
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
  }

  switch (type) {
    case BaseSchema::Type::kBool: {
      try {
        columns.emplace_back(expr::any_optional_data_adaptor::FromOperand<bool>(
            (*operand_ptr)[index]));

      } catch (const std::bad_variant_access& bad) {
        std::string s = fmt::format(
            "Operand to std::any<std::optional<bool>> failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kInteger: {
      try {
        columns.emplace_back(
            expr::any_optional_data_adaptor::FromOperand<int32_t>(
                (*operand_ptr)[index]));
      } catch (const std::bad_variant_access& bad) {
        std::string s = fmt::format(
            "Operand to std::any<std::optional<int32_t>> failed, {}",
            bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kFloat: {
      try {
        columns.emplace_back(
            expr::any_optional_data_adaptor::FromOperand<float>(
                (*operand_ptr)[index]));
      } catch (const std::bad_variant_access& bad) {
        std::string s = fmt::format(
            "Operand to std::any<std::optional<float>> failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kLong: {
      try {
        columns.emplace_back(
            expr::any_optional_data_adaptor::FromOperand<int64_t>(
                (*operand_ptr)[index]));
      } catch (const std::bad_variant_access& bad) {
        std::string s = fmt::format(
            "Operand to std::any<std::optional<int64_t>> failed, {}",
            bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kDouble: {
      try {
        columns.emplace_back(
            expr::any_optional_data_adaptor::FromOperand<double>(
                (*operand_ptr)[index]));
      } catch (const std::bad_variant_access& bad) {
        std::string s =
            fmt::format("Operand to std::any<std::optional<double>> failed, {}",
                        bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kString: {
      try {
        columns.emplace_back(
            expr::any_optional_data_adaptor::FromOperand<
                std::shared_ptr<std::string>>((*operand_ptr)[index]));
      } catch (const std::bad_variant_access& bad) {
        std::string s = fmt::format(
            "Operand to std::any<std::shared_ptr<std::string>> failed, {}",
            bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kBoolList: {
      try {
        columns.emplace_back(
            expr::any_optional_data_adaptor::FromOperand<
                std::shared_ptr<std::vector<bool>>>((*operand_ptr)[index]));
      } catch (const std::bad_variant_access& bad) {
        std::string s =
            fmt::format("Trans from operand failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kIntegerList: {
      try {
        columns.emplace_back(
            expr::any_optional_data_adaptor::FromOperand<
                std::shared_ptr<std::vector<int32_t>>>((*operand_ptr)[index]));
      } catch (const std::bad_variant_access& bad) {
        std::string s =
            fmt::format("Trans from operand failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kFloatList: {
      try {
        columns.emplace_back(
            expr::any_optional_data_adaptor::FromOperand<
                std::shared_ptr<std::vector<float>>>((*operand_ptr)[index]));
      } catch (const std::bad_variant_access& bad) {
        std::string s =
            fmt::format("Trans from operand failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kLongList: {
      try {
        columns.emplace_back(
            expr::any_optional_data_adaptor::FromOperand<
                std::shared_ptr<std::vector<int64_t>>>((*operand_ptr)[index]));
      } catch (const std::bad_variant_access& bad) {
        std::string s =
            fmt::format("Trans from operand failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kDoubleList: {
      try {
        columns.emplace_back(
            expr::any_optional_data_adaptor::FromOperand<
                std::shared_ptr<std::vector<double>>>((*operand_ptr)[index]));
      } catch (const std::bad_variant_access& bad) {
        std::string s =
            fmt::format("Trans from operand failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kStringList: {
      try {
        columns.emplace_back(expr::any_optional_data_adaptor::FromOperand<
                             std::shared_ptr<std::vector<std::string>>>(
            (*operand_ptr)[index]));
      } catch (const std::bad_variant_access& bad) {
        std::string s =
            fmt::format("Trans from operand failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    default: {
      std::string s = fmt::format("CloneColumn unsupported type  {}",
                                  BaseSchema::GetTypeString(type));
      DINGO_LOG(ERROR) << s;
      return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
    }
  }

  return butil::Status();
}

butil::Status RelExprHelper::TransFromOperandV2(
    BaseSchema::Type type,
    const std::unique_ptr<std::vector<expr::Operand>>& operand_ptr,
    size_t index, std::vector<std::any>& columns) {
  if (!operand_ptr) {
    std::string s = fmt::format("operand_ptr is nullptr. not support");
    DINGO_LOG(ERROR) << s;
    return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
  }

  switch (type) {
    case BaseSchema::Type::kBool: {
      try {
        columns.emplace_back(FromOperandV2<bool>((*operand_ptr)[index]));
      } catch (const std::bad_variant_access& bad) {
        std::string s =
            fmt::format("Operand V2 to bool failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kInteger: {
      try {
        columns.emplace_back(FromOperandV2<int32_t>((*operand_ptr)[index]));
      } catch (const std::bad_variant_access& bad) {
        std::string s =
            fmt::format("Operand to int32_t failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kFloat: {
      try {
        columns.emplace_back(FromOperandV2<float>((*operand_ptr)[index]));
      } catch (const std::bad_variant_access& bad) {
        std::string s = fmt::format("Operand to float failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kLong: {
      try {
        columns.emplace_back(FromOperandV2<int64_t>((*operand_ptr)[index]));
      } catch (const std::bad_variant_access& bad) {
        std::string s =
            fmt::format("Operand to int64_t failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kDouble: {
      try {
        columns.emplace_back(FromOperandV2<double>((*operand_ptr)[index]));
      } catch (const std::bad_variant_access& bad) {
        std::string s = fmt::format("Operand to double failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kString: {
      try {
        std::shared_ptr<std::string> operand_value =
            expr::any_optional_data_adaptor::FromOperandV2<
                std::shared_ptr<std::string>>((*operand_ptr)[index]);
        if (operand_value) {
          columns.emplace_back(std::any(*operand_value));
        } else {
          columns.emplace_back(std::any());
        }
      } catch (const std::bad_variant_access& bad) {
        std::string s =
            fmt::format("Operand to std::string failed, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kBoolList: {
      try {
        std::shared_ptr<std::vector<bool>> operand_value =
            expr::any_optional_data_adaptor::FromOperandV2<
                std::shared_ptr<std::vector<bool>>>((*operand_ptr)[index]);
        if (operand_value) {
          columns.emplace_back(std::any(std::move(*operand_value)));
        } else {
          columns.emplace_back(std::any());
        }
      } catch (const std::bad_variant_access& bad) {
        std::string s = fmt::format(
            "Trans from operand failed for bool list, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kIntegerList: {
      try {
        std::shared_ptr<std::vector<int32_t>> operand_value =
            expr::any_optional_data_adaptor::FromOperandV2<
                std::shared_ptr<std::vector<int32_t>>>((*operand_ptr)[index]);
        if (operand_value) {
          columns.emplace_back(std::any(std::move(*operand_value)));
        } else {
          columns.emplace_back(std::any());
        }
      } catch (const std::bad_variant_access& bad) {
        std::string s = fmt::format(
            "Trans from operand failedfor integer list, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kFloatList: {
      try {
        std::shared_ptr<std::vector<float>> operand_value =
            expr::any_optional_data_adaptor::FromOperandV2<
                std::shared_ptr<std::vector<float>>>((*operand_ptr)[index]);
        if (operand_value) {
          columns.emplace_back(std::any(std::move(*operand_value)));
        } else {
          columns.emplace_back(std::any());
        }
      } catch (const std::bad_variant_access& bad) {
        std::string s = fmt::format(
            "Trans from operand failed for float list, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kLongList: {
      try {
        std::shared_ptr<std::vector<int64_t>> operand_value =
            expr::any_optional_data_adaptor::FromOperandV2<
                std::shared_ptr<std::vector<int64_t>>>((*operand_ptr)[index]);
        if (operand_value) {
          columns.emplace_back(std::any(std::move(*operand_value)));
        } else {
          columns.emplace_back(std::any());
        }
      } catch (const std::bad_variant_access& bad) {
        std::string s = fmt::format(
            "Trans from operand failed for long list, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kDoubleList: {
      try {
        std::shared_ptr<std::vector<double>> operand_value =
            expr::any_optional_data_adaptor::FromOperandV2<
                std::shared_ptr<std::vector<double>>>((*operand_ptr)[index]);
        if (operand_value) {
          columns.emplace_back(std::any(std::move(*operand_value)));
        } else {
          columns.emplace_back(std::any());
        }
      } catch (const std::bad_variant_access& bad) {
        std::string s = fmt::format(
            "Trans from operand failed for double list, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    case BaseSchema::Type::kStringList: {
      try {
        std::shared_ptr<std::vector<std::string>> operand_value =
            expr::any_optional_data_adaptor::FromOperandV2<
                std::shared_ptr<std::vector<std::string>>>(
                (*operand_ptr)[index]);
        if (operand_value) {
          columns.emplace_back(std::any(std::move(*operand_value)));
        } else {
          columns.emplace_back(std::any());
        }
      } catch (const std::bad_variant_access& bad) {
        std::string s = fmt::format(
            "Trans from operand failed for string list, {}", bad.what());
        DINGO_LOG(ERROR) << s;
        return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
      }
      break;
    }
    default: {
      std::string s = fmt::format("CloneColumn unsupported type  {}",
                                  BaseSchema::GetTypeString(type));
      DINGO_LOG(ERROR) << s;
      return butil::Status(pb::error::EILLEGAL_PARAMTETERS, s);
    }
  }

  return butil::Status();
}

butil::Status RelExprHelper::TransToOperandWrapper(
    const int codec_version,
    const std::shared_ptr<std::vector<std::shared_ptr<BaseSchema>>>&
        original_serial_schemas,
    const std::vector<int>& selection_column_indexes,
    const std::vector<std::any>& original_record,
    std::unique_ptr<std::vector<expr::Operand>>& operand_ptr) {
  butil::Status status;
  size_t i = 0;

  if (codec_version <= dingodb::serialV2::CODEC_VERSION_V1) {
    for (const auto& record : original_record) {
      BaseSchema::Type type =
          (*original_serial_schemas)[selection_column_indexes[i++]]->GetType();

      status = RelExprHelper::TransToOperand(type, record, operand_ptr);
      if (!status.ok()) {
        DINGO_LOG(ERROR) << status.error_cstr();
        return status;
      }
    }
  } else {
    for (const auto& record : original_record) {
      BaseSchema::Type type =
          (*original_serial_schemas)[selection_column_indexes[i++]]->GetType();

      status = RelExprHelper::TransToOperandV2(type, record, operand_ptr);
      if (!status.ok()) {
        DINGO_LOG(ERROR) << status.error_cstr();
        return status;
      }
    }
  }
  return butil::Status();
}

butil::Status RelExprHelper::TransFromOperandWrapper(
    const int codec_version,
    const std::unique_ptr<std::vector<expr::Operand>>& operand_ptr,
    const std::shared_ptr<std::vector<std::shared_ptr<BaseSchema>>>&
        result_serial_schemas,
    const std::vector<int>& result_column_indexes,
    std::vector<std::any>& result_record) {
  butil::Status status;

  size_t i = 0;

  if (codec_version <= dingodb::serialV2::CODEC_VERSION_V1) {  // codec v1 for 0 or 1.
    for (const auto& tuple : *operand_ptr) {
      BaseSchema::Type type =
          (*result_serial_schemas)[result_column_indexes[i]]->GetType();
      status =
          RelExprHelper::TransFromOperand(type, operand_ptr, i, result_record);
      if (!status.ok()) {
        DINGO_LOG(ERROR) << status.error_cstr();
        return status;
      }
      i++;
    }
  } else {  // codec v2.
    for (const auto& tuple : *operand_ptr) {
      BaseSchema::Type type =
          (*result_serial_schemas)[result_column_indexes[i]]->GetType();
      status = RelExprHelper::TransFromOperandV2(type, operand_ptr, i,
                                                 result_record);
      if (!status.ok()) {
        DINGO_LOG(ERROR) << status.error_cstr();
        return status;
      }
      i++;
    }
  }

  return butil::Status();
}

}  // namespace dingodb

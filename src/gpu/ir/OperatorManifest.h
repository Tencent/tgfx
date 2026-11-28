/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <vector>
#include "GeneratedOperatorIDs.h"
#include "gpu/SLType.h"

namespace tgfx {

struct OperatorParam {
  SLType type = SLType::Void;
  bool isArray = false;
  uint32_t arrayCount = 0;

  OperatorParam() = default;

  OperatorParam(SLType type) : type(type) {
  }

  OperatorParam(SLType type, bool isArray, uint32_t arrayCount)
      : type(type), isArray(isArray), arrayCount(arrayCount) {
  }
};

struct OperatorDesc {
  OperatorID id = INVALID_OPERATOR_ID;
  const char* debugName = nullptr;
  const char* glslSymbol = nullptr;
  SLType returnType = SLType::Void;
  std::vector<OperatorParam> params = {};
  std::vector<const char*> includes = {};
};

/// Returns the manifest entry for an operator, or null when the id is unknown.
const OperatorDesc* FindOperator(OperatorID id);

/// Iterates the manifest without exposing its storage. The build tool walks this list to emit one
/// SPIR-V module per operator, keyed by the operator id.
size_t OperatorCount();
const OperatorDesc* OperatorAt(size_t index);

}  // namespace tgfx

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "gpu/aot/chain/SPIRVWriter.h"
#include "gpu/ir/ProgramIR.h"

namespace tgfx {

/// The SPIR-V counterpart of FragmentShaderBuilder. It owns types, constants, the entry point,
/// the output variable and the emitted value ids. It does not own operator bodies; those come
/// from OperatorBundle in a later phase.
class SPIRVShaderBuilder {
 public:
  explicit SPIRVShaderBuilder(ShaderStage stage);
  ~SPIRVShaderBuilder() = default;

  bool Begin(std::string* error = nullptr);

  /// Emits the defining expression of value and returns its SPIR-V result id.
  bool EmitValue(const ProgramIR& program, ValueID value, uint32_t* resultId,
                 std::string* error = nullptr);

  bool StoreOutput(const ProgramIR& program, ValueID value, std::string* error = nullptr);

  std::vector<uint32_t> Finish(std::string* error = nullptr);

 private:
  uint32_t TypeId(SLType type, std::string* error);
  uint32_t ConstantId(const IRExpr& expr, std::string* error);
  uint32_t EmitExpr(const ProgramIR& program, const IRExpr& expr, std::string* error);
  uint32_t PointerTypeId(SLType type, uint32_t storageClass, std::string* error);

  ShaderStage stage = ShaderStage::Fragment;
  spirv::Writer writer = {};
  std::unordered_map<uint32_t, uint32_t> typeIds = {};
  std::unordered_map<uint64_t, uint32_t> constantIds = {};
  std::unordered_map<uint64_t, uint32_t> pointerTypeIds = {};
  std::unordered_map<uint32_t, uint32_t> valueIds = {};
  uint32_t mainId = 0;
  uint32_t labelId = 0;
  uint32_t outputPointerId = 0;
  uint32_t outputId = 0;
  bool begun = false;
  bool finished = false;
};

}  // namespace tgfx

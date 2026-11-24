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
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <unordered_map>
#include "gpu/ir/ProgramIR.h"

namespace tgfx {

/// Linear SSA builder for the backend-neutral program IR. Every expression creates its result
/// value; callers cannot mutate an existing value's type or defining expression.
class ProgramIRBuilder {
 public:
  ProgramIRBuilder() = default;

  ValueID CreateValue(SLType type, Precision precision, IRValueKind kind,
                      std::string debugName = {});

  /// Returns the existing value for a name, or creates one. Resource and local names are unique
  /// after processor mangling, so this keeps the interface and the dual-write path on one ID.
  ValueID InternValue(const std::string& name, SLType type, Precision precision, IRValueKind kind);

  ValueID AddAttribute(AttributeIR attribute);

  ValueID AddVarying(VaryingIR varying);

  ValueID AddUniform(UniformIR uniform);

  ValueID AddSampler(SamplerIR sampler);

  const InterfaceIR& Interface() const {
    return ir.interface;
  }

  ValueID CreateConstant(float value, Precision precision = Precision::High);

  ValueID CreateIntConstant(int32_t value);

  ValueID CreateLoad(ValueID source, SLType type, Precision precision = Precision::Default);

  ValueID CreateSwizzle(ValueID source, SLType type, std::string swizzle);

  ValueID CreateConstruct(SLType type, const std::vector<ValueID>& args);

  ValueID CreateBinary(IRBinaryOp op, SLType type, ValueID left, ValueID right);

  ValueID CreateCall(OperatorID op, SLType type, const std::vector<ValueID>& args);

  ValueID CreateTextureSample(ValueID sampler, ValueID coordinate, SLType type);

  void Assign(LValue dst, ValueID src);

  void StoreOutput(ValueID dst, ValueID src);

  const ProgramIR& IR() const {
    return ir;
  }

 private:
  ValueID AddExpr(IRExpr expr);

  ProgramIR ir = {};
  std::unordered_map<std::string, ValueID> valuesByName = {};
};

}  // namespace tgfx

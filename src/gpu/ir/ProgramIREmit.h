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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>

#include "gpu/ShaderVar.h"
#include "gpu/VertexShaderBuilder.h"
#include "gpu/ir/ProgramIRBuilder.h"

namespace tgfx {

/// The shared IR emission templates: the statement shapes every processor used to copy by hand.
/// One place per template, so a change (a new uniform, a type fix) lands once instead of N times.

/// Emits the whole vertex position path:
///   position3 = vec3(position.xy, 1);  transformed = matrixUniform * position3;
///   ndc = transformed.xy (+ offsetAttribute) * RTAdjust.xz + RTAdjust.yw;
///   gl_Position = vec4(ndc, 0, 1);
/// Pass offsetAttribute for the instanced-offset variant; pass a null matrixUniformName for the
/// pass-through variant (the position goes straight to the RTAdjust step).
inline void emitPositionTransformIR(ProgramIRBuilder* ir, const std::string& positionName,
                                    SLType positionType, const std::string& matrixUniformName,
                                    const std::string* offsetName = nullptr,
                                    SLType offsetType = SLType::Float2) {
  if (ir == nullptr) {
    return;
  }
  const auto one = ir->CreateConstant(1.0f);
  const auto zero = ir->CreateConstant(0.0f);
  ValueID positionValue =
      ir->InternValue(positionName, positionType, Precision::High, IRValueKind::Attribute);
  if (positionType != SLType::Float2) {
    positionValue = ir->CreateSwizzle(positionValue, SLType::Float2, "xy");
  }
  ValueID ndc = INVALID_VALUE_ID;
  if (!matrixUniformName.empty()) {
    const auto matrixValue = ir->InternValue(matrixUniformName, SLType::Float3x3, Precision::High,
                                             IRValueKind::Uniform);
    const auto position3 = ir->CreateConstruct(SLType::Float3, {positionValue, one});
    const auto transformed =
        ir->CreateBinary(IRBinaryOp::Mul, SLType::Float3, matrixValue, position3);
    ndc = ir->CreateSwizzle(transformed, SLType::Float2, "xy");
  } else {
    ndc = positionValue;
  }
  if (offsetName != nullptr) {
    const auto offsetValue =
        ir->InternValue(*offsetName, offsetType, Precision::High, IRValueKind::Attribute);
    ndc = ir->CreateBinary(IRBinaryOp::Add, SLType::Float2, ndc, offsetValue);
  }
  const auto rtAdjust =
      ir->InternValue(RTAdjustName, SLType::Float4, Precision::High, IRValueKind::Uniform);
  const auto adjustXZ = ir->CreateSwizzle(rtAdjust, SLType::Float2, "xz");
  const auto adjustYW = ir->CreateSwizzle(rtAdjust, SLType::Float2, "yw");
  const auto scaled = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float2, ndc, adjustXZ);
  const auto shifted = ir->CreateBinary(IRBinaryOp::Add, SLType::Float2, scaled, adjustYW);
  const auto position4 = ir->CreateConstruct(SLType::Float4, {shifted, zero, one});
  const auto output =
      ir->CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "gl_Position");
  ir->StoreOutput(output, position4);
}

/// Emits the fragment color output: the source (a uniform or a varying name) becomes the stage's
/// output color. The caller selects the source kind; the shape is identical either way.
inline void emitOutputColorIR(ProgramIRBuilder* ir, const std::string& sourceName, bool isVarying,
                              const std::string& outputColorName) {
  if (ir == nullptr) {
    return;
  }
  const auto sourceValue =
      ir->InternValue(sourceName, SLType::Float4, Precision::High,
                      isVarying ? IRValueKind::Varying : IRValueKind::Uniform);
  const auto output =
      ir->InternValue(outputColorName, SLType::Float4, Precision::High, IRValueKind::Local);
  ir->StoreOutput(output, sourceValue);
}

/// Emits the fragment coverage output as vec4: a scalar source is splatted, a vec4 passes through.
inline void emitOutputCoverageIR(ProgramIRBuilder* ir, ValueID value, bool valueIsFloat4,
                                 const std::string& outputCoverageName) {
  if (ir == nullptr || value == INVALID_VALUE_ID) {
    return;
  }
  ValueID coverage4 = value;
  if (!valueIsFloat4) {
    coverage4 = ir->CreateConstruct(SLType::Float4, {value, value, value, value});
  }
  const auto output =
      ir->InternValue(outputCoverageName, SLType::Float4, Precision::High, IRValueKind::Local);
  ir->StoreOutput(output, coverage4);
}

}  // namespace tgfx

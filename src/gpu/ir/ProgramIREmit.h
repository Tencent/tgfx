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

#include <functional>

#include "GeneratedOperators.h"
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

// The weighted tap loop: the gaussian blur family's slow path, expressed as a runtime loop over
// one child evaluation. The loop body holds a single child emission (the child evaluator
// callback fires once at build time inside the loop), so the IR stays as compact as the text
// route's for loop.
inline ValueID emitWeightedTapLoop(
    ProgramIRBuilder* ir, UniformHandler* uniformHandler, ValueID baseCoord, ValueID tapStep,
    ValueID radiusValue, const std::string& baseCoordName,
    const std::function<ValueID(ValueID tapCoordValue, const std::string& tapCoordName)>&
        childEvaluator) {
  if (ir == nullptr) {
    return INVALID_VALUE_ID;
  }
  // sum: the accumulator (a local, Load/Store semantics on both backends).
  const auto sumVar = ir->CreateValue(SLType::Float4, Precision::High, IRValueKind::Local,
                                      "blurTapSum");
  const auto zero = ir->CreateConstant(0.0f);
  const auto zero4 = ir->CreateConstruct(SLType::Float4, {zero, zero, zero, zero});
  ir->Assign({sumVar, ""}, zero4);
  // i runs from -radius to +radius; the bound (radius uniform) is a float, the counter is int.
  const auto radiusInt = ir->CreateConvert(radiusValue, SLType::Int);
  const auto intZero = ir->CreateConstantInt(0);
  const auto initValue = ir->CreateBinary(IRBinaryOp::Sub, SLType::Int, intZero, radiusInt);
  ir->BeginForLoop("blurTapIndex", initValue, radiusInt, intZero);
  const auto indexValue = ir->InternValue("blurTapIndex", SLType::Int, Precision::High,
                                          IRValueKind::Local);
  // tapCoord = baseCoord + tapStep * float(i) — updated inside the loop body each iteration.
  const auto indexFloat = ir->CreateConvert(indexValue, SLType::Float);
  const auto scaled = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float2, tapStep, indexFloat);
  const auto tapCoordValue =
      ir->CreateBinary(IRBinaryOp::Add, SLType::Float2, baseCoord, scaled);
  const std::string tapCoordName = baseCoordName + "Tap";
  const auto tapCoordVar = ir->CreateValue(SLType::Float2, Precision::High, IRValueKind::Local,
                                           tapCoordName);
  ir->Assign({tapCoordVar, ""}, tapCoordValue);
  // The child evaluates once, at the tap coordinate (the name bridges into the child's
  // coordinate lookup).
  const auto childValue = childEvaluator(tapCoordValue, tapCoordName);
  // weight: kernelWeight(i) splatted to vec4.
  KernelWeightOp::UniformNames kernelNames = {};
  KernelWeightOp::EnsureUniforms(ir, uniformHandler, &kernelNames);
  const auto weight = KernelWeightOp::Emit(ir, uniformHandler, &kernelNames, indexValue);
  const auto weight4 = ir->CreateConstruct(SLType::Float4, {weight, weight, weight, weight});
  const auto weighted = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4, childValue, weight4);
  const auto newSum = ir->CreateBinary(IRBinaryOp::Add, SLType::Float4, sumVar, weighted);
  ir->Assign({sumVar, ""}, newSum);
  ir->EndForLoop();
  return sumVar;
}


/// The gaussian blur 2D slow path: two nested runtime loops over one child evaluation. The child
/// fires once inside the double loop body (its coordinate is the tap the loops update); the
/// weights come from the caller's kernel axis uniforms, so the call is built directly rather than
/// through the generated Emit (which would declare its own slot uniforms).
inline ValueID emitWeightedTapLoop2D(
    ProgramIRBuilder* ir, ValueID radiusXValue, ValueID radiusYValue, ValueID stepXValue,
    ValueID stepYValue, ValueID originValue, ValueID kernelX0, ValueID kernelX1, ValueID kernelY0,
    ValueID kernelY1,
    const std::function<ValueID(ValueID tapCoordValue, const std::string& tapCoordName)>&
        childEvaluator) {
  if (ir == nullptr) {
    return INVALID_VALUE_ID;
  }
  const auto sumVar =
      ir->CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "blur2DTapSum");
  const auto zero = ir->CreateConstant(0.0f);
  const auto zero4 = ir->CreateConstruct(SLType::Float4, {zero, zero, zero, zero});
  ir->Assign({sumVar, ""}, zero4);
  const auto intZero = ir->CreateConstantInt(0);
  // The radii are int uniforms already; the counters run i in [-radius, radius] (the text loop's
  // j form plus early break folds to the same taps: out-of-range weights read as zero).
  const auto initY = ir->CreateBinary(IRBinaryOp::Sub, SLType::Int, intZero, radiusYValue);
  ir->BeginForLoop("blur2DY", initY, radiusYValue, intZero);
  const auto yIndex = ir->InternValue("blur2DY", SLType::Int, Precision::High, IRValueKind::Local);
  const auto yFloat = ir->CreateConvert(yIndex, SLType::Float);
  const auto weightY =
      ir->CreateCall(OperatorID::KernelWeight2, SLType::Float, {kernelY0, kernelY1, yFloat});
  const auto initX = ir->CreateBinary(IRBinaryOp::Sub, SLType::Int, intZero, radiusXValue);
  ir->BeginForLoop("blur2DX", initX, radiusXValue, intZero);
  const auto xIndex = ir->InternValue("blur2DX", SLType::Int, Precision::High, IRValueKind::Local);
  const auto xFloat = ir->CreateConvert(xIndex, SLType::Float);
  const auto weightX =
      ir->CreateCall(OperatorID::KernelWeight2, SLType::Float, {kernelX0, kernelX1, xFloat});
  // tapCoord = origin + stepX * float(x) + stepY * float(y) — updated each iteration.
  const auto xStep = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float2, stepXValue, xFloat);
  const auto yStep = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float2, stepYValue, yFloat);
  const auto stepped = ir->CreateBinary(IRBinaryOp::Add, SLType::Float2, xStep, yStep);
  const auto tapCoordValue = ir->CreateBinary(IRBinaryOp::Add, SLType::Float2, originValue, stepped);
  const std::string tapCoordName = "blur2DTapCoord";
  const auto tapCoordVar =
      ir->CreateValue(SLType::Float2, Precision::High, IRValueKind::Local, tapCoordName);
  ir->Assign({tapCoordVar, ""}, tapCoordValue);
  const auto childValue = childEvaluator(tapCoordValue, tapCoordName);
  const auto weight = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float, weightX, weightY);
  const auto weight4 = ir->CreateConstruct(SLType::Float4, {weight, weight, weight, weight});
  const auto weighted = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4, childValue, weight4);
  const auto newSum = ir->CreateBinary(IRBinaryOp::Add, SLType::Float4, sumVar, weighted);
  ir->Assign({sumVar, ""}, newSum);
  ir->EndForLoop();
  ir->EndForLoop();
  return sumVar;
}

}  // namespace tgfx

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
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

#include "GLSLClampedGradientEffect.h"
#include "GeneratedOperators.h"
#include "gpu/ProgramBuilder.h"
#include "gpu/ShaderCaps.h"

namespace tgfx {
PlacementPtr<ClampedGradientEffect> ClampedGradientEffect::Make(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> colorizer,
    PlacementPtr<FragmentProcessor> gradLayout, Color leftBorderColor, Color rightBorderColor) {
  return allocator->make<GLSLClampedGradientEffect>(std::move(colorizer), std::move(gradLayout),
                                                    leftBorderColor, rightBorderColor);
}

GLSLClampedGradientEffect::GLSLClampedGradientEffect(PlacementPtr<FragmentProcessor> colorizer,
                                                     PlacementPtr<FragmentProcessor> gradLayout,
                                                     Color leftBorderColor, Color rightBorderColor)
    : ClampedGradientEffect(std::move(colorizer), std::move(gradLayout), leftBorderColor,
                            rightBorderColor) {
}

void GLSLClampedGradientEffect::emitCode(EmitArgs& args) const {
  auto uniformHandler = args.uniformHandler;
  auto leftBorderColorName =
      uniformHandler->addUniform("leftBorderColor", UniformFormat::Float4, ShaderStage::Fragment);
  auto rightBorderColorName = uniformHandler->addUniform("rightBorderColor", UniformFormat::Float4,
                                                          ShaderStage::Fragment);
  // Both children emit once (their declarations and IR blocks); the boundary selection, the
  // premultiply and the input modulation all live in the shared function the call below records.
  // Note: the IR route evaluates the colorizer on border fragments too (the call's arguments are
  // values, not lazily-evaluated branches) — the colorizer's own subset clamp keeps the read in
  // bounds. The WebGPU uniform-control-flow form is a phase-4 concern.
  std::string _child1 = "_child1";
  const auto tValue = emitChildIR(gradLayoutIndex, "", &_child1, args, INVALID_VALUE_ID);
  std::string _child0 = "_child0";
  const auto colorizedValue = emitChildIR(colorizerIndex, "t", &_child0, args, tValue);
  if (args.irBuilder != nullptr && tValue != INVALID_VALUE_ID &&
      colorizedValue != INVALID_VALUE_ID && args.inputValue != INVALID_VALUE_ID &&
      args.outputValue != INVALID_VALUE_ID) {
    // The bounds come from this processor's own uniforms: the operator's declared pair would be a
    // second set nothing uploads, and the call would read zeros.
    auto ir = args.irBuilder;
    const auto leftBorderValue = ir->InternValue(leftBorderColorName, SLType::Float4,
                                                 Precision::High, IRValueKind::Uniform);
    const auto rightBorderValue = ir->InternValue(rightBorderColorName, SLType::Float4,
                                                  Precision::High, IRValueKind::Uniform);
    const auto result =
        ir->CreateCall(OperatorID::ClampedGradient, SLType::Float4,
                       {tValue, colorizedValue, args.inputValue, leftBorderValue, rightBorderValue});
    if (result == INVALID_VALUE_ID) {
      ir->MarkFailed("the clamped gradient could not be recorded");
      return;
    }
    ir->StoreOutput(args.outputValue, result);
  }
}

void GLSLClampedGradientEffect::onSetData(UniformData* /*vertexUniformData*/,
                                          UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("leftBorderColor", leftBorderColor);
  fragmentUniformData->setData("rightBorderColor", rightBorderColor);
}
}  // namespace tgfx

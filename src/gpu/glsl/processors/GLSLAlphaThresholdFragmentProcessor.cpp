/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "GLSLAlphaThresholdFragmentProcessor.h"
#include "gpu/glsl/GLSLOperatorEmitter.h"

namespace tgfx {
PlacementPtr<AlphaThresholdFragmentProcessor> AlphaThresholdFragmentProcessor::Make(
    BlockAllocator* allocator, float threshold) {
  return allocator->make<GLSLAlphaThresholdFragmentProcessor>(threshold);
}

void GLSLAlphaThresholdFragmentProcessor::emitCode(EmitArgs& args) const {
  auto uniformHandler = args.uniformHandler;
  auto thresholdUniformName =
      uniformHandler->addUniform("Threshold", UniformFormat::Float, ShaderStage::Fragment);

  auto fragBuilder = args.fragBuilder;
  auto entry = fragBuilder->appendFunction("pmApplyAlphaThreshold");
  if (entry.empty()) {
    return;
  }
  if (args.irBuilder == nullptr || args.inputValue == INVALID_VALUE_ID ||
      args.outputValue == INVALID_VALUE_ID) {
    return;
  }
  const auto thresholdValue = args.irBuilder->InternValue(thresholdUniformName, SLType::Float,
                                                          Precision::High, IRValueKind::Uniform);
  const auto result = args.irBuilder->CreateCall(OperatorID::AlphaThreshold, SLType::Float4,
                                                 {args.inputValue, thresholdValue});
  args.irBuilder->StoreOutput(args.outputValue, result);
  std::string expression = {};
  std::string error = {};
  if (result == INVALID_VALUE_ID ||
      !GLSLOperatorEmitter::EmitCall(args.irBuilder->IR(), result, &expression, &error)) {
    args.irBuilder->MarkFailed(error.empty() ? "the alpha-threshold operator could not be emitted"
                                             : error);
    return;
  }
  fragBuilder->appendCall(
      args.outputColor, entry,
      {args.inputColorValue, ValueRef(thresholdUniformName, SLType::Float, ValueOrigin::Uniform)},
      "", expression);
}

void GLSLAlphaThresholdFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                                    UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("Threshold", threshold);
}

}  // namespace tgfx

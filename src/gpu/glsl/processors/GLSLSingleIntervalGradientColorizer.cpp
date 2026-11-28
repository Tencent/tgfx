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

#include "GLSLSingleIntervalGradientColorizer.h"
#include "GeneratedOperators.h"
#include "gpu/glsl/GLSLOperatorEmitter.h"

namespace tgfx {
PlacementPtr<SingleIntervalGradientColorizer> SingleIntervalGradientColorizer::Make(
    BlockAllocator* allocator, Color start, Color end) {
  return allocator->make<GLSLSingleIntervalGradientColorizer>(start, end);
}

GLSLSingleIntervalGradientColorizer::GLSLSingleIntervalGradientColorizer(Color start, Color end)
    : SingleIntervalGradientColorizer(start, end) {
}

void GLSLSingleIntervalGradientColorizer::emitCode(EmitArgs& args) const {
  if (args.irBuilder == nullptr || args.inputValue == INVALID_VALUE_ID ||
      args.outputValue == INVALID_VALUE_ID) {
    return;
  }
  SingleGradientColorOp::UniformNames names = {};
  const auto t = args.irBuilder->CreateSwizzle(args.inputValue, SLType::Float, "x");
  const auto result =
      SingleGradientColorOp::Emit(args.irBuilder, args.uniformHandler, &names, t);
  args.irBuilder->StoreOutput(args.outputValue, result);
  std::string expression = {};
  std::string error = {};
  if (result == INVALID_VALUE_ID ||
      !GLSLOperatorEmitter::EmitCall(args.irBuilder->IR(), result, &expression, &error)) {
    args.irBuilder->MarkFailed(error.empty() ? "the single-interval colorizer could not be emitted"
                                             : error);
    return;
  }
}

void GLSLSingleIntervalGradientColorizer::onSetData(UniformData* /*vertexUniformData*/,
                                                    UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("start", start);
  fragmentUniformData->setData("end", end);
}
}  // namespace tgfx

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

#include "GLSLConstColorProcessor.h"
#include "GeneratedOperators.h"
#include "gpu/glsl/GLSLOperatorEmitter.h"

namespace tgfx {
PlacementPtr<ConstColorProcessor> ConstColorProcessor::Make(BlockAllocator* allocator,
                                                            PMColor color, InputMode mode) {
  return allocator->make<GLSLConstColorProcessor>(color, mode);
}

GLSLConstColorProcessor::GLSLConstColorProcessor(PMColor color, InputMode mode)
    : ConstColorProcessor(color, mode) {
}

void GLSLConstColorProcessor::emitCode(EmitArgs& args) const {
  if (args.irBuilder == nullptr || args.outputValue == INVALID_VALUE_ID) {
    return;
  }
  // The xfer's input is the stage's geometry color, which a draw that never set one leaves
  // invalid; the chain head is white.
  ValueID inputValue = args.inputValue;
  if (inputValue == INVALID_VALUE_ID) {
    const auto one = args.irBuilder->CreateConstant(1.0f);
    inputValue = args.irBuilder->CreateConstruct(SLType::Float4, {one, one, one, one});
  }
  ConstColorOp::UniformNames names = {};
  const auto result = ConstColorOp::Emit(args.irBuilder, args.uniformHandler, &names, inputValue,
                                         static_cast<int32_t>(inputMode));
  args.irBuilder->StoreOutput(args.outputValue, result);
}

void GLSLConstColorProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                        UniformData* fragmentUniformData) const {
  ConstColorUniforms uniforms = {};
  uniforms.constColor = {color.red, color.green, color.blue, color.alpha};
  uniforms.upload(fragmentUniformData);
}
}  // namespace tgfx

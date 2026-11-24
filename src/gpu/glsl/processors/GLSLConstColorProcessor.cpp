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

namespace tgfx {
PlacementPtr<ConstColorProcessor> ConstColorProcessor::Make(BlockAllocator* allocator,
                                                            PMColor color, InputMode mode) {
  return allocator->make<GLSLConstColorProcessor>(color, mode);
}

GLSLConstColorProcessor::GLSLConstColorProcessor(PMColor color, InputMode mode)
    : ConstColorProcessor(color, mode) {
}

void GLSLConstColorProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto colorName =
      args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
  auto entry = fragBuilder->appendFunction("pmApplyConstColor");
  if (entry.empty()) {
    return;
  }
  fragBuilder->appendCall(
      args.outputColor, entry,
      {ValueRef(colorName, SLType::Float4, ValueOrigin::Uniform), args.inputColorValue,
       ValueRef(std::to_string(static_cast<int>(inputMode)), SLType::Int, ValueOrigin::Literal)});
  if (args.irBuilder != nullptr) {
    const auto colorValue = args.irBuilder->CreateValue(SLType::Float4, Precision::High,
                                                        IRValueKind::Uniform, colorName);
    const auto modeValue = args.irBuilder->CreateIntConstant(static_cast<int32_t>(inputMode));
    const auto result = args.irBuilder->CreateCall(OperatorID::ConstColor, SLType::Float4,
                                                   {colorValue, args.inputValue, modeValue});
    args.irBuilder->StoreOutput(args.outputValue, result);
  }
}

void GLSLConstColorProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                        UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("Color", color);
}
}  // namespace tgfx

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
PlacementPtr<ConstColorProcessor> ConstColorProcessor::Make(BlockBuffer* buffer, Color color,
                                                            InputMode mode) {
  return buffer->make<GLSLConstColorProcessor>(color, mode);
}

GLSLConstColorProcessor::GLSLConstColorProcessor(Color color, InputMode mode)
    : ConstColorProcessor(color, mode) {
}

void GLSLConstColorProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto colorName =
      args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
  fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
  switch (inputMode) {
    case InputMode::Ignore:
      break;
    case InputMode::ModulateRGBA:
      fragBuilder->codeAppendf("%s *= %s;", args.outputColor.c_str(), args.inputColor.c_str());
      break;
    case InputMode::ModulateA:
      fragBuilder->codeAppendf("%s *= %s.a;", args.outputColor.c_str(), args.inputColor.c_str());
      break;
  }
}

void GLSLConstColorProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                        UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("Color", color);
}
}  // namespace tgfx

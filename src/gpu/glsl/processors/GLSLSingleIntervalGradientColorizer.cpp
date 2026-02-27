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

namespace tgfx {
PlacementPtr<SingleIntervalGradientColorizer> SingleIntervalGradientColorizer::Make(
    BlockAllocator* allocator, Color start, Color end) {
  return allocator->make<GLSLSingleIntervalGradientColorizer>(start, end);
}

GLSLSingleIntervalGradientColorizer::GLSLSingleIntervalGradientColorizer(Color start, Color end)
    : SingleIntervalGradientColorizer(start, end) {
}

void GLSLSingleIntervalGradientColorizer::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto startName =
      args.uniformHandler->addUniform("start", UniformFormat::Float4, ShaderStage::Fragment);
  auto endName =
      args.uniformHandler->addUniform("end", UniformFormat::Float4, ShaderStage::Fragment);
  fragBuilder->codeAppendf("float t = %s.x;", args.inputColor.c_str());
  fragBuilder->codeAppendf("%s = (1.0 - t) * %s + t * %s;", args.outputColor.c_str(),
                           startName.c_str(), endName.c_str());
}

void GLSLSingleIntervalGradientColorizer::onSetData(UniformData* /*vertexUniformData*/,
                                                    UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("start", start);
  fragmentUniformData->setData("end", end);
}
}  // namespace tgfx

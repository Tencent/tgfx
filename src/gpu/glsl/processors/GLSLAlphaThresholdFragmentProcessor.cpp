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
  fragBuilder->codeAppendf("%s = vec4(0.0);", args.outputColor.c_str());
  fragBuilder->codeAppendf("if (%s.a > 0.0) {", args.inputColor.c_str());
  fragBuilder->codeAppendf("  %s.rgb = %s.rgb / %s.a;", args.outputColor.c_str(),
                           args.inputColor.c_str(), args.inputColor.c_str());
  fragBuilder->codeAppendf("  %s.a = step(%s, %s.a);", args.outputColor.c_str(),
                           thresholdUniformName.c_str(), args.inputColor.c_str());
  fragBuilder->codeAppendf("  %s = clamp(%s, 0.0, 1.0);", args.outputColor.c_str(),
                           args.outputColor.c_str());
  fragBuilder->codeAppend("}");
}

void GLSLAlphaThresholdFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                                    UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("Threshold", threshold);
}

}  // namespace tgfx

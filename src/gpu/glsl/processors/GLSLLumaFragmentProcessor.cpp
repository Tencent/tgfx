/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "GLSLLumaFragmentProcessor.h"

namespace tgfx {
PlacementPtr<FragmentProcessor> LumaFragmentProcessor::Make(
    BlockBuffer* buffer, std::shared_ptr<ColorSpace> colorSpace) {
  return buffer->make<GLSLLumaFragmentProcessor>(std::move(colorSpace));
}

void GLSLLumaFragmentProcessor::emitCode(EmitArgs& args) const {
  auto uniformHandler = args.uniformHandler;
  auto kr = uniformHandler->addUniform("Kr", UniformFormat::Float, ShaderStage::Fragment);
  auto kg = uniformHandler->addUniform("Kg", UniformFormat::Float, ShaderStage::Fragment);
  auto kb = uniformHandler->addUniform("Kb", UniformFormat::Float, ShaderStage::Fragment);

  args.fragBuilder->codeAppendf("float luma = dot(%s.rgb, vec3(%s, %s, %s));\n",
                                args.inputColor.c_str(), kr.c_str(), kg.c_str(), kb.c_str());
  args.fragBuilder->codeAppendf("%s = vec4(luma);\n", args.outputColor.c_str());
}

void GLSLLumaFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                          UniformData* fragmentUniformData) const {
  auto lumaFactor = _colorSpace->lumaFactor();
  fragmentUniformData->setData("Kr", lumaFactor.kr);
  fragmentUniformData->setData("Kg", lumaFactor.kg);
  fragmentUniformData->setData("Kb", lumaFactor.kb);
}

}  // namespace tgfx

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

#include "GLSLFragmentShaderBuilder.h"
#include "gpu/glsl/GLSLProgramBuilder.h"

namespace tgfx {
static constexpr char DstColorName[] = "_dstColor";

GLSLFragmentShaderBuilder::GLSLFragmentShaderBuilder(ProgramBuilder* program)
    : FragmentShaderBuilder(program) {
  auto glProgram = static_cast<GLSLProgramBuilder*>(program);
  auto shaderCaps = glProgram->getContext()->shaderCaps();
  if (shaderCaps->usesPrecisionModifiers) {
    setPrecisionQualifier("precision mediump float;");
  }
}

std::string GLSLFragmentShaderBuilder::dstColor() {
  auto shaderCaps = programBuilder->getContext()->shaderCaps();
  if (shaderCaps->frameBufferFetchSupport) {
    addFeature(PrivateFeature::FramebufferFetch, shaderCaps->frameBufferFetchExtensionString);
    if (shaderCaps->frameBufferFetchNeedsCustomOutput) {
      return CUSTOM_COLOR_OUTPUT_NAME;
    }
    return shaderCaps->frameBufferFetchColorName;
  }
  return DstColorName;
}

std::string GLSLFragmentShaderBuilder::colorOutputName() {
  return CUSTOM_COLOR_OUTPUT_NAME;
}
}  // namespace tgfx

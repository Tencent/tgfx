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

#include "GLFragmentShaderBuilder.h"
#include "GLContext.h"
#include "GLProgramBuilder.h"

namespace tgfx {
static constexpr char DstColorName[] = "_dstColor";

GLFragmentShaderBuilder::GLFragmentShaderBuilder(ProgramBuilder* program)
    : FragmentShaderBuilder(program) {
  auto glProgram = static_cast<GLProgramBuilder*>(program);
  if (glProgram->getContext()->caps()->usesPrecisionModifiers) {
    setPrecisionQualifier("precision mediump float;");
  }
}

std::string GLFragmentShaderBuilder::dstColor() {
  auto caps = GLCaps::Get(programBuilder->getContext());
  if (caps->frameBufferFetchSupport) {
    addFeature(PrivateFeature::FramebufferFetch, caps->frameBufferFetchExtensionString);
    return caps->frameBufferFetchColorName;
  }
  return DstColorName;
}

std::string GLFragmentShaderBuilder::colorOutputName() {
  return static_cast<GLProgramBuilder*>(programBuilder)->isDesktopGL() ? CustomColorOutputName()
                                                                       : "gl_FragColor";
}
}  // namespace tgfx

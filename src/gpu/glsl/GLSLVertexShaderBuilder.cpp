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

#include "GLSLVertexShaderBuilder.h"
#include "gpu/ShaderCaps.h"
#include "gpu/glsl/GLSLProgramBuilder.h"

namespace tgfx {
GLSLVertexShaderBuilder::GLSLVertexShaderBuilder(ProgramBuilder* program)
    : VertexShaderBuilder(program) {
  auto glProgram = static_cast<GLSLProgramBuilder*>(program);
  auto shaderCaps = glProgram->getContext()->shaderCaps();
  if (shaderCaps->usesPrecisionModifiers) {
    setPrecisionQualifier("precision mediump float;");
  }
}
}  // namespace tgfx

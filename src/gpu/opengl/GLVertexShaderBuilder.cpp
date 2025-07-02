/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "GLVertexShaderBuilder.h"
#include "GLProgramBuilder.h"
#include "gpu/ProgramBuilder.h"

namespace tgfx {
GLVertexShaderBuilder::GLVertexShaderBuilder(ProgramBuilder* program)
    : VertexShaderBuilder(program) {
  auto glProgram = static_cast<GLProgramBuilder*>(program);
  if (glProgram->getContext()->caps()->usesPrecisionModifiers) {
    setPrecisionQualifier("precision mediump float;");
  }
}

void GLVertexShaderBuilder::emitNormalizedPosition(const std::string& devPos) {
  codeAppendf("gl_Position = vec4(%s.xy * %s.xz + %s.yw, 0, 1);", devPos.c_str(),
              RTAdjustName.c_str(), RTAdjustName.c_str());
}
}  // namespace tgfx

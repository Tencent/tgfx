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

void GLSLVertexShaderBuilder::emitNormalizedPosition(const std::string& devPos) {
  codeAppendf("gl_Position = vec4(%s.xy * %s.xz + %s.yw, 0, 1);", devPos.c_str(),
              RTAdjustName.c_str(), RTAdjustName.c_str());
}

void GLSLVertexShaderBuilder::emitTransformedPoint(const std::string& dstPointName,
                                                   const std::string& srcPointName,
                                                   const std::string& transformName,
                                                   bool hasPerspective) {
  if (hasPerspective) {
    codeAppendf("vec3 %sTemp = %s * vec3(%s, 1.0);", dstPointName.c_str(), transformName.c_str(),
                srcPointName.c_str());
    codeAppendf("highp vec2 %s = %sTemp.xy / %sTemp.z;", dstPointName.c_str(), dstPointName.c_str(),
                dstPointName.c_str());
  } else {
    codeAppendf("highp vec2 %s = (%s * vec3(%s, 1.0)).xy;", dstPointName.c_str(),
                transformName.c_str(), srcPointName.c_str());
  }
}
}  // namespace tgfx

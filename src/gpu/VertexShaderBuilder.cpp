/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "VertexShaderBuilder.h"
#include <utility>

namespace tgfx {
void VertexShaderBuilder::storeVarying(const std::string& dstName, const std::string& srcName) {
  codeAppendf("%s = %s;", dstName.c_str(), srcName.c_str());
}

void VertexShaderBuilder::transformPoint(const std::string& dstPointName,
                                         const std::string& srcPointName,
                                         const std::string& transformName, bool hasPerspective) {
  emitTransformedPoint(dstPointName, srcPointName, transformName, hasPerspective);
}

void VertexShaderBuilder::transformVarying(const std::string& varyingName,
                                           const std::string& srcPointName,
                                           const std::string& transformName, bool hasPerspective) {
  if (hasPerspective) {
    codeAppendf("%s = %s * vec3(%s, 1);", varyingName.c_str(), transformName.c_str(),
                srcPointName.c_str());
  } else {
    codeAppendf("%s = (%s * vec3(%s, 1)).xy;", varyingName.c_str(), transformName.c_str(),
                srcPointName.c_str());
  }
}

void VertexShaderBuilder::transformPointAdd(const std::string& dstPointName,
                                            const std::string& srcPointName,
                                            const std::string& transformName,
                                            const std::string& offsetName) {
  codeAppendf("highp vec2 %s = (%s * vec3(%s, 1.0)).xy + %s;", dstPointName.c_str(),
              transformName.c_str(), srcPointName.c_str(), offsetName.c_str());
}

void VertexShaderBuilder::multiplyVec2(const std::string& dstName, const std::string& first,
                                       const std::string& second, bool declare) {
  if (declare) {
    codeAppendf("highp vec2 %s = %s * %s;", dstName.c_str(), first.c_str(), second.c_str());
  } else {
    codeAppendf("%s = %s * %s;", dstName.c_str(), first.c_str(), second.c_str());
  }
}

void VertexShaderBuilder::swizzleVec2(const std::string& dstName, const std::string& srcName,
                                      const std::string& components) {
  codeAppendf("highp vec2 %s = %s.%s;", dstName.c_str(), srcName.c_str(), components.c_str());
}

void VertexShaderBuilder::composeVec4(const std::string& dstName, const std::string& first,
                                      const std::string& second) {
  codeAppendf("highp vec4 %s = vec4(%s, %s);", dstName.c_str(), first.c_str(), second.c_str());
}

void VertexShaderBuilder::normalizeRect(const std::string& rectName) {
  codeAppendf("%s = vec4(min(%s.xy, %s.zw), max(%s.xy, %s.zw));", rectName.c_str(),
              rectName.c_str(), rectName.c_str(), rectName.c_str(), rectName.c_str());
}

void VertexShaderBuilder::storePosition(const std::string& devPos) {
  emitNormalizedPosition(devPos);
}
}  // namespace tgfx

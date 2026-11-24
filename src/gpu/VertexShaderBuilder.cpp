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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "VertexShaderBuilder.h"
#include <utility>

namespace tgfx {
void VertexShaderBuilder::storeVarying(const std::string& dstName, const std::string& srcName) {
  recordVertexCall(VertexCallKind::StoreVarying, dstName, srcName, "", "", false);
  if (!suppressText) {
    codeAppendf("%s = %s;", dstName.c_str(), srcName.c_str());
  }
}

void VertexShaderBuilder::transformPoint(const std::string& dstPointName,
                                         const std::string& srcPointName,
                                         const std::string& transformName, bool hasPerspective) {
  recordVertexCall(VertexCallKind::TransformPoint, dstPointName, srcPointName, transformName, "",
                   hasPerspective, false, SLType::Float2);
  if (!suppressText) {
    emitTransformedPoint(dstPointName, srcPointName, transformName, hasPerspective);
  }
}

void VertexShaderBuilder::transformVarying(const std::string& varyingName,
                                           const std::string& srcPointName,
                                           const std::string& transformName, bool hasPerspective) {
  recordVertexCall(VertexCallKind::TransformVarying, varyingName, srcPointName, transformName, "",
                   hasPerspective, false, hasPerspective ? SLType::Float3 : SLType::Float2);
  if (suppressText) {
    return;
  }
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
  recordVertexCall(VertexCallKind::TransformPointAdd, dstPointName, srcPointName, transformName,
                   offsetName, false, false, SLType::Float2);
  if (!suppressText) {
    codeAppendf("highp vec2 %s = (%s * vec3(%s, 1.0)).xy + %s;", dstPointName.c_str(),
                transformName.c_str(), srcPointName.c_str(), offsetName.c_str());
  }
}

void VertexShaderBuilder::multiplyVec2(const std::string& dstName, const std::string& first,
                                       const std::string& second, bool declare) {
  recordVertexCall(VertexCallKind::MultiplyVec2, dstName, first, second, "", false, declare,
                   SLType::Float2);
  if (suppressText) {
    return;
  }
  if (declare) {
    codeAppendf("highp vec2 %s = %s * %s;", dstName.c_str(), first.c_str(), second.c_str());
  } else {
    codeAppendf("%s = %s * %s;", dstName.c_str(), first.c_str(), second.c_str());
  }
}

void VertexShaderBuilder::swizzleVec2(const std::string& dstName, const std::string& srcName,
                                      const std::string& components) {
  recordVertexCall(VertexCallKind::SwizzleVec2, dstName, srcName, "", components, false, false,
                   SLType::Float2);
  if (!suppressText) {
    codeAppendf("highp vec2 %s = %s.%s;", dstName.c_str(), srcName.c_str(), components.c_str());
  }
}

void VertexShaderBuilder::composeVec4(const std::string& dstName, const std::string& first,
                                      const std::string& second) {
  recordVertexCall(VertexCallKind::ComposeVec4, dstName, first, second, "", false, false,
                   SLType::Float4);
  if (!suppressText) {
    codeAppendf("highp vec4 %s = vec4(%s, %s);", dstName.c_str(), first.c_str(), second.c_str());
  }
}

void VertexShaderBuilder::normalizeRect(const std::string& rectName) {
  recordVertexCall(VertexCallKind::NormalizeRect, "", rectName, "", "", false);
  if (!suppressText) {
    codeAppendf("%s = vec4(min(%s.xy, %s.zw), max(%s.xy, %s.zw));", rectName.c_str(),
                rectName.c_str(), rectName.c_str(), rectName.c_str(), rectName.c_str());
  }
}

void VertexShaderBuilder::storePosition(const std::string& devPos) {
  recordVertexCall(VertexCallKind::StorePosition, "", devPos, "", "", false);
  if (!suppressText) {
    emitNormalizedPosition(devPos);
  }
}

void VertexShaderBuilder::recordVertexCall(VertexCallKind kind, const std::string& dst,
                                           const std::string& src, const std::string& operand,
                                           const std::string& extra, bool hasPerspective,
                                           bool declaresResult, SLType resultType) {
  if (vertexRecorder == nullptr) {
    return;
  }
  VertexCallRecord record = {};
  record.kind = kind;
  record.dst = dst;
  record.src = src;
  record.operand = operand;
  record.extra = extra;
  record.hasPerspective = hasPerspective;
  record.declaresResult = declaresResult;
  record.resultType = resultType;
  vertexRecorder->push_back(std::move(record));
}
}  // namespace tgfx

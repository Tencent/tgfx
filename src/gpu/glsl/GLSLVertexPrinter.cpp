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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "GLSLVertexPrinter.h"
#include "gpu/VertexShaderBuilder.h"

namespace tgfx {
namespace {

constexpr char Indent[] = "    ";

void AppendTransformPoint(std::string* out, const VertexCall& call) {
  if (call.hasPerspective) {
    *out += Indent;
    *out +=
        "highp vec3 " + call.dst + "Temp = " + call.operand + " * vec3(" + call.src + ", 1.0);\n";
    *out += Indent;
    *out += "highp vec2 " + call.dst + " = " + call.dst + "Temp.xy / " + call.dst + "Temp.z;\n";
    return;
  }
  *out += Indent;
  *out +=
      "highp vec2 " + call.dst + " = (" + call.operand + " * vec3(" + call.src + ", 1.0)).xy;\n";
}

}  // namespace

std::string PrintGLSLVertexProgram(const std::vector<VertexCall>& calls) {
  std::string out = {};
  for (const auto& call : calls) {
    switch (call.kind) {
      case VertexCallKind::StoreVarying:
        out += Indent;
        out += call.dst + " = " + call.src + ";\n";
        break;
      case VertexCallKind::TransformPoint:
        AppendTransformPoint(&out, call);
        break;
      case VertexCallKind::TransformVarying:
        out += Indent;
        if (call.hasPerspective) {
          out += call.dst + " = " + call.operand + " * vec3(" + call.src + ", 1);\n";
        } else {
          out += call.dst + " = (" + call.operand + " * vec3(" + call.src + ", 1)).xy;\n";
        }
        break;
      case VertexCallKind::TransformPointAdd:
        out += Indent;
        out += "highp vec2 " + call.dst + " = (" + call.operand + " * vec3(" + call.src +
               ", 1.0)).xy + " + call.extra + ";\n";
        break;
      case VertexCallKind::MultiplyVec2:
        out += Indent;
        if (call.declaresResult) {
          out += "highp vec2 " + call.dst + " = ";
        } else {
          out += call.dst + " = ";
        }
        out += call.src + " * " + call.operand + ";\n";
        break;
      case VertexCallKind::SwizzleVec2:
        out += Indent;
        out += "highp vec2 " + call.dst + " = " + call.src + "." + call.extra + ";\n";
        break;
      case VertexCallKind::ComposeVec4:
        out += Indent;
        out += "highp vec4 " + call.dst + " = vec4(" + call.src + ", " + call.operand + ");\n";
        break;
      case VertexCallKind::NormalizeRect:
        out += Indent;
        out += call.src + " = vec4(min(" + call.src + ".xy, " + call.src + ".zw), max(" + call.src +
               ".xy, " + call.src + ".zw));\n";
        break;
      case VertexCallKind::StorePosition:
        out += Indent;
        out += "gl_Position = vec4(" + call.src + ".xy * " + RTAdjustName + ".xz + " +
               RTAdjustName + ".yw, 0, 1);\n";
        break;
    }
  }
  out += "}\n";
  return out;
}

}  // namespace tgfx

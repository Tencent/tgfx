////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
////////////////////////////////////////////////////////////////////////////////////////////////

#include "MSLPrinter.h"
#include <cctype>
#include <cstring>
#include "gpu/ProgramBuilder.h"
#include "gpu/SLType.h"
#include "gpu/ShaderBuilder.h"
#include "gpu/ShaderVar.h"
#include "gpu/VertexShaderBuilder.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"

namespace tgfx {
namespace {

constexpr char Indent[] = "    ";

const char* MslTypeName(SLType type) {
  switch (type) {
    case SLType::Float:
      return "float";
    case SLType::Float2:
      return "float2";
    case SLType::Float3:
      return "float3";
    case SLType::Float4:
      return "float4";
    case SLType::Float2x2:
      return "float2x2";
    case SLType::Float3x3:
      return "float3x3";
    case SLType::Float4x4:
      return "float4x4";
    case SLType::Half:
      return "half";
    case SLType::Half2:
      return "half2";
    case SLType::Half3:
      return "half3";
    case SLType::Half4:
      return "half4";
    case SLType::Int:
      return "int";
    case SLType::Int2:
      return "int2";
    case SLType::Int3:
      return "int3";
    case SLType::Int4:
      return "int4";
    case SLType::Texture2DSampler:
    case SLType::Texture2DRectSampler:
    case SLType::TextureExternalSampler:
      return "texture2d<float>";
    default:
      return "float4";
  }
}

bool IsWordChar(char character) {
  return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
}

void ReplaceWord(std::string* text, const char* from, const char* to) {
  const std::string needle(from);
  const std::string replacement(to);
  size_t offset = 0;
  while ((offset = text->find(needle, offset)) != std::string::npos) {
    const bool leftOk = offset == 0 || !IsWordChar((*text)[offset - 1]);
    const size_t end = offset + needle.size();
    const bool rightOk = end >= text->size() || !IsWordChar((*text)[end]);
    if (leftOk && rightOk) {
      text->replace(offset, needle.size(), replacement);
      offset += replacement.size();
    } else {
      offset = end;
    }
  }
}

void DropWordWithSpace(std::string* text, const char* word) {
  const std::string needle(word);
  size_t offset = 0;
  while ((offset = text->find(needle, offset)) != std::string::npos) {
    const bool leftOk = offset == 0 || !IsWordChar((*text)[offset - 1]);
    size_t end = offset + needle.size();
    const bool rightOk = end >= text->size() || !IsWordChar((*text)[end]);
    if (leftOk && rightOk) {
      while (end < text->size() && ((*text)[end] == ' ' || (*text)[end] == '\t')) {
        ++end;
      }
      text->erase(offset, end - offset);
    } else {
      offset = end;
    }
  }
}

// texture(NAME, coord) reads through the combined GLSL sampler; MSL splits the sampler into the
// texture and the sampler state, so every call site names both.
void RewriteTextureCalls(std::string* text) {
  const std::string call = "texture(";
  size_t offset = 0;
  while ((offset = text->find(call, offset)) != std::string::npos) {
    const bool leftOk = offset == 0 || !IsWordChar((*text)[offset - 1]);
    if (!leftOk) {
      offset += call.size();
      continue;
    }
    size_t nameBegin = offset + call.size();
    while (nameBegin < text->size() &&
           std::isspace(static_cast<unsigned char>((*text)[nameBegin]))) {
      ++nameBegin;
    }
    size_t nameEnd = nameBegin;
    while (nameEnd < text->size() && IsWordChar((*text)[nameEnd])) {
      ++nameEnd;
    }
    const std::string name = text->substr(nameBegin, nameEnd - nameBegin);
    if (name.empty()) {
      offset = nameEnd;
      continue;
    }
    size_t comma = nameEnd;
    while (comma < text->size() && std::isspace(static_cast<unsigned char>((*text)[comma]))) {
      ++comma;
    }
    if (comma >= text->size() || (*text)[comma] != ',') {
      offset = nameEnd;
      continue;
    }
    const std::string replacement = name + "_t.sample(" + name + "_s, ";
    text->replace(offset, comma + 1 - offset, replacement);
    offset += replacement.size();
  }
}

// The GLSL statement text the processors emitted, spelled as MSL.
std::string RewriteBody(const std::string& text) {
  std::string out = text;
  DropWordWithSpace(&out, "highp");
  DropWordWithSpace(&out, "mediump");
  DropWordWithSpace(&out, "lowp");
  ReplaceWord(&out, "vec2", "float2");
  ReplaceWord(&out, "vec3", "float3");
  ReplaceWord(&out, "vec4", "float4");
  ReplaceWord(&out, "ivec2", "int2");
  ReplaceWord(&out, "ivec3", "int3");
  ReplaceWord(&out, "ivec4", "int4");
  ReplaceWord(&out, "bvec2", "bool2");
  ReplaceWord(&out, "bvec3", "bool3");
  ReplaceWord(&out, "bvec4", "bool4");
  ReplaceWord(&out, "mat3", "float3x3");
  ReplaceWord(&out, "mat4", "float4x4");
  ReplaceWord(&out, "gl_FragCoord", "in.position");
  ReplaceWord(&out, "mod(", "fmod(");
  ReplaceWord(&out, "discard;", "discard_fragment();");
  RewriteTextureCalls(&out);
  return out;
}

void AppendTransformPoint(std::string* out, const VertexCall& call) {
  if (call.hasPerspective) {
    *out += Indent;
    *out += "float3 " + call.dst.name + "Temp = " + call.operand.name + " * float3(" +
            call.src.name + ", 1.0);\n";
    *out += Indent;
    *out += "float2 " + call.dst.name + " = " + call.dst.name + "Temp.xy / " + call.dst.name +
            "Temp.z;\n";
    return;
  }
  *out += Indent;
  *out += "float2 " + call.dst.name + " = (" + call.operand.name + " * float3(" + call.src.name +
          ", 1.0)).xy;\n";
}

std::string PrefixOf(const FragmentInstruction& instruction) {
  const size_t first = instruction.text.find_first_not_of(" \t");
  if (first == std::string::npos) {
    return Indent;
  }
  return instruction.text.substr(0, first);
}

std::string ArgumentsOf(const std::vector<std::string>& args) {
  std::string out = {};
  for (size_t index = 0; index < args.size(); ++index) {
    if (index > 0) {
      out += ", ";
    }
    out += args[index];
  }
  return out;
}

void AppendCall(std::string* out, const FragmentInstruction& instruction) {
  *out += PrefixOf(instruction);
  if (instruction.declaresResult) {
    *out += instruction.resultType + " ";
  }
  *out += instruction.output + " = ";
  if (!instruction.wrapType.empty()) {
    *out += instruction.wrapType + "(";
  }
  *out += instruction.function + "(" + ArgumentsOf(instruction.args) + ")";
  if (!instruction.wrapType.empty()) {
    *out += ")";
  }
  *out += ";\n";
}

void AppendSection(std::string* out, const std::string& section) {
  if (section.empty()) {
    return;
  }
  *out += section;
  *out += "\n";
}

}  // namespace

std::string MSLPrinter::PrintVertexProgram(const std::vector<VertexCall>& calls) {
  std::string out = {};
  for (const auto& call : calls) {
    switch (call.kind) {
      case VertexCallKind::StoreVarying:
        out += Indent;
        out += call.dst.name + " = " + call.src.name + ";\n";
        break;
      case VertexCallKind::TransformPoint:
        AppendTransformPoint(&out, call);
        break;
      case VertexCallKind::TransformVarying:
        out += Indent;
        if (call.hasPerspective) {
          out +=
              call.dst.name + " = " + call.operand.name + " * float3(" + call.src.name + ", 1);\n";
        } else {
          out += call.dst.name + " = (" + call.operand.name + " * float3(" + call.src.name +
                 ", 1)).xy;\n";
        }
        break;
      case VertexCallKind::TransformPointAdd:
        out += Indent;
        out += "float2 " + call.dst.name + " = (" + call.operand.name + " * float3(" +
               call.src.name + ", 1.0)).xy + " + call.extra.name + ";\n";
        break;
      case VertexCallKind::MultiplyVec2:
        out += Indent;
        if (call.declaresResult) {
          out += "float2 " + call.dst.name + " = ";
        } else {
          out += call.dst.name + " = ";
        }
        out += call.src.name + " * " + call.operand.name + ";\n";
        break;
      case VertexCallKind::SwizzleVec2:
        out += Indent;
        out += "float2 " + call.dst.name + " = " + call.src.name + "." + call.extra.name + ";\n";
        break;
      case VertexCallKind::ComposeVec4:
        out += Indent;
        out += "float4 " + call.dst.name + " = float4(" + call.src.name + ", " + call.operand.name +
               ");\n";
        break;
      case VertexCallKind::NormalizeRect:
        out += Indent;
        out += call.src.name + " = float4(min(" + call.src.name + ".xy, " + call.src.name +
               ".zw), max(" + call.src.name + ".xy, " + call.src.name + ".zw));\n";
        break;
      case VertexCallKind::StorePosition:
        out += Indent;
        out += "out.position = float4(" + call.src.name + ".xy * " + RTAdjustName + ".xz + " +
               RTAdjustName + ".yw, 0, 1);\n";
        break;
    }
  }
  return out;
}

std::string MSLPrinter::PrintFragmentProgram(const FragmentProgram& program) {
  std::string out = {};
  for (const auto& instruction : program.instructions) {
    switch (instruction.kind) {
      case FragmentInstructionKind::RawText:
        out += instruction.text;
        break;
      case FragmentInstructionKind::Call:
        AppendCall(&out, instruction);
        break;
      case FragmentInstructionKind::Alias:
        out += PrefixOf(instruction);
        if (!instruction.declaration.empty()) {
          out += instruction.declaration + " ";
        }
        out += instruction.output + " = " + instruction.src + ";\n";
        break;
      case FragmentInstructionKind::StoreColor:
        out += PrefixOf(instruction);
        out += instruction.output + " = " + instruction.src + ";\n";
        break;
      case FragmentInstructionKind::StoreCoverage:
        out += PrefixOf(instruction);
        out += instruction.output + " = ";
        if (instruction.splat) {
          out += "float4(" + instruction.src + ")";
        } else {
          out += instruction.src;
        }
        out += ";\n";
        break;
      case FragmentInstructionKind::Combine:
        out += PrefixOf(instruction);
        out += instruction.output + " = " + instruction.src + " * " + instruction.operand + ";\n";
        break;
      case FragmentInstructionKind::ReadVarying:
        break;
      case FragmentInstructionKind::Project:
        out += PrefixOf(instruction);
        out += "float2 " + instruction.output + " = " + instruction.src + ".xy / " +
               instruction.src + ".z;\n";
        break;
      case FragmentInstructionKind::Root:
        break;
    }
  }
  return out;
}

std::string MSLPrinter::PrintStage(const ProgramBuilder& programBuilder, ShaderStage stage,
                                   const InterfaceDesc& interface, const ShaderBuilder& builder) {
  (void)programBuilder;
  std::vector<UniformDesc> uniforms = {};
  for (const auto& uniform : interface.uniforms) {
    if (uniform.stage == stage) {
      uniforms.push_back(uniform);
    }
  }
  const bool isVertex = stage == ShaderStage::Vertex;
  const char* blockName = isVertex ? VertexUniformBlockName : FragmentUniformBlockName;

  std::string out = {};
  out += "#include <metal_stdlib>\n#include <simd/simd.h>\n\nusing namespace metal;\n\n";
  if (!uniforms.empty()) {
    out += "struct ";
    out += blockName;
    out += " {\n";
    for (const auto& uniform : uniforms) {
      out += Indent;
      out += MslTypeName(uniform.type);
      out += " ";
      out += uniform.name;
      if (uniform.count > 1) {
        out += "[" + std::to_string(uniform.count) + "]";
      }
      out += ";\n";
    }
    out += "};\n\n";
  }
  if (isVertex) {
    if (!interface.attributes.empty()) {
      out += "struct VertexAttributes {\n";
      for (size_t index = 0; index < interface.attributes.size(); ++index) {
        const auto& attribute = interface.attributes[index];
        out += Indent + std::string(MslTypeName(attribute.type)) + " " + attribute.name +
               " [[attribute(" + std::to_string(index) + ")]];\n";
      }
      out += "};\n\n";
    }
    out += "struct VertexOut {\n";
    out += std::string(Indent) + "float4 position [[position]];\n";
    for (const auto& varying : interface.varyings) {
      out += Indent + std::string(MslTypeName(varying.type)) + " " + varying.name;
      if (varying.flat) {
        out += " [[flat]]";
      }
      out += ";\n";
    }
    out += "};\n\n";
  } else if (!interface.varyings.empty()) {
    out += "struct FragmentIn {\n";
    out += std::string(Indent) + "float4 position [[position]];\n";
    for (const auto& varying : interface.varyings) {
      out += Indent + std::string(MslTypeName(varying.type)) + " " + varying.name;
      if (varying.flat) {
        out += " [[flat]]";
      }
      out += ";\n";
    }
    out += "};\n\n";
  }

  AppendSection(&out, builder.functions());

  std::string body = RewriteBody(builder.code());
  if (isVertex) {
    out += "vertex VertexOut main0(";
    if (!interface.attributes.empty()) {
      out += "VertexAttributes in [[stage_in]]";
    }
    if (!uniforms.empty()) {
      out += std::string(interface.attributes.empty() ? "" : ", ") + "constant " + blockName +
             "& vb [[buffer(0)]]";
    }
    out += ") {\n";
    out += std::string(Indent) + "VertexOut out = {};\n";
    for (const auto& attribute : interface.attributes) {
      out += Indent + std::string(MslTypeName(attribute.type)) + " " + attribute.name + " = in." +
             attribute.name + ";\n";
    }
    for (const auto& uniform : uniforms) {
      out += Indent + std::string(MslTypeName(uniform.type)) + " " + uniform.name + " = vb." +
             uniform.name + ";\n";
    }
    for (const auto& varying : interface.varyings) {
      out += Indent + std::string(MslTypeName(varying.type)) + " " + varying.name + " = {};\n";
    }
    out += body;
    for (const auto& varying : interface.varyings) {
      out += std::string(Indent) + "out." + varying.name + " = " + varying.name + ";\n";
    }
    out += std::string(Indent) + "return out;\n}\n";
    return out;
  }

  out += "fragment float4 main0(";
  if (!interface.varyings.empty()) {
    out += "FragmentIn in [[stage_in]]";
    if (!uniforms.empty() || !interface.samplers.empty()) {
      out += ", ";
    }
  }
  if (!uniforms.empty()) {
    out += std::string("constant ") + blockName + "& fb [[buffer(0)]]";
    if (!interface.samplers.empty()) {
      out += ", ";
    }
  }
  for (size_t index = 0; index < interface.samplers.size(); ++index) {
    const auto& sampler = interface.samplers[index];
    if (index > 0) {
      out += ", ";
    }
    out += "texture2d<float> " + sampler.name + "_t [[texture(" + std::to_string(index) +
           ")]], sampler " + sampler.name + "_s [[sampler(" + std::to_string(index) + ")]]";
  }
  out += ") {\n";
  out += std::string(Indent) + "float4 fragColor = float4(0.0);\n";
  for (const auto& varying : interface.varyings) {
    out += Indent + std::string(MslTypeName(varying.type)) + " " + varying.name + " = in." +
           varying.name + ";\n";
  }
  for (const auto& uniform : uniforms) {
    out += Indent + std::string(MslTypeName(uniform.type)) + " " + uniform.name + " = fb." +
           uniform.name + ";\n";
  }
  out += body;
  out += std::string(Indent) + "return fragColor;\n}\n";
  return out;
}

}  // namespace tgfx

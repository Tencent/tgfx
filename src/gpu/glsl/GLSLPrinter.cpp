/////////////////////////////////////////////////////////////////////////////////////////////////
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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "GLSLPrinter.h"
#include "gpu/ProgramBuilder.h"
#include "gpu/ShaderBuilder.h"
#include "gpu/ShaderVar.h"
#include "gpu/VertexShaderBuilder.h"

namespace tgfx {
namespace {

void AppendSection(std::string* out, const std::string& section) {
  if (section.empty()) {
    return;
  }
  *out += section;
  *out += "\n";
}

constexpr char Indent[] = "    ";

void AppendTransformPoint(std::string* out, const VertexCall& call) {
  if (call.hasPerspective) {
    *out += Indent;
    *out += "highp vec3 " + call.dst.name + "Temp = " + call.operand.name + " * vec3(" +
            call.src.name + ", 1.0);\n";
    *out += Indent;
    *out += "highp vec2 " + call.dst.name + " = " + call.dst.name + "Temp.xy / " + call.dst.name +
            "Temp.z;\n";
    return;
  }
  *out += Indent;
  *out += "highp vec2 " + call.dst.name + " = (" + call.operand.name + " * vec3(" + call.src.name +
          ", 1.0)).xy;\n";
}

std::string PrefixOf(const FragmentInstruction& instruction) {
  const size_t first = instruction.text.find_first_not_of(" \t");
  if (first == std::string::npos) {
    return "    ";
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

}  // namespace

std::string GLSLPrinter::PrintVertexProgram(const std::vector<VertexCall>& calls) {
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
          out += call.dst.name + " = " + call.operand.name + " * vec3(" + call.src.name + ", 1);\n";
        } else {
          out += call.dst.name + " = (" + call.operand.name + " * vec3(" + call.src.name +
                 ", 1)).xy;\n";
        }
        break;
      case VertexCallKind::TransformPointAdd:
        out += Indent;
        out += "highp vec2 " + call.dst.name + " = (" + call.operand.name + " * vec3(" +
               call.src.name + ", 1.0)).xy + " + call.extra.name + ";\n";
        break;
      case VertexCallKind::MultiplyVec2:
        out += Indent;
        if (call.declaresResult) {
          out += "highp vec2 " + call.dst.name + " = ";
        } else {
          out += call.dst.name + " = ";
        }
        out += call.src.name + " * " + call.operand.name + ";\n";
        break;
      case VertexCallKind::SwizzleVec2:
        out += Indent;
        out +=
            "highp vec2 " + call.dst.name + " = " + call.src.name + "." + call.extra.name + ";\n";
        break;
      case VertexCallKind::ComposeVec4:
        out += Indent;
        out += "highp vec4 " + call.dst.name + " = vec4(" + call.src.name + ", " +
               call.operand.name + ");\n";
        break;
      case VertexCallKind::NormalizeRect:
        out += Indent;
        out += call.src.name + " = vec4(min(" + call.src.name + ".xy, " + call.src.name +
               ".zw), max(" + call.src.name + ".xy, " + call.src.name + ".zw));\n";
        break;
      case VertexCallKind::StorePosition:
        out += Indent;
        out += "gl_Position = vec4(" + call.src.name + ".xy * " + RTAdjustName + ".xz + " +
               RTAdjustName + ".yw, 0, 1);\n";
        break;
    }
  }
  out += "}\n";
  return out;
}

std::string GLSLPrinter::PrintFragmentProgram(const FragmentProgram& program) {
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
          out += "vec4(" + instruction.src + ")";
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
        out += "highp vec2 " + instruction.output + " = " + instruction.src + ".xy / " +
               instruction.src + ".z;\n";
        break;
      case FragmentInstructionKind::Root:
        break;
    }
  }
  out += "}\n";
  return out;
}

std::string GLSLPrinter::PrintStage(const ProgramBuilder& programBuilder, ShaderStage stage,
                                    const InterfaceDesc& interface, const ShaderBuilder& builder) {
  std::string uniforms = {};
  std::vector<UniformDesc> uniformDescriptors = {};
  for (const auto& uniform : interface.uniforms) {
    if (uniform.stage == stage) {
      uniformDescriptors.push_back(uniform);
    }
  }
  uniforms += programBuilder.getUniformBlockDeclaration(stage, uniformDescriptors);
  if (stage == ShaderStage::Fragment) {
    for (const auto& sampler : interface.samplers) {
      ShaderVar var(sampler.name, sampler.type, ShaderVar::TypeModifier::Uniform);
      uniforms += programBuilder.getShaderVarDeclarations(var, stage);
      uniforms += ";\n";
    }
  }

  std::string inputs = {};
  std::string outputs = {};
  for (const auto& input : builder.inputVars()) {
    inputs += programBuilder.getShaderVarDeclarations(input, stage);
    inputs += ";\n";
  }
  for (const auto& output : builder.outputVars()) {
    outputs += programBuilder.getShaderVarDeclarations(output, stage);
    outputs += ";\n";
  }
  if (stage == ShaderStage::Vertex) {
    for (const auto& attribute : interface.attributes) {
      ShaderVar var(attribute.name, attribute.type, ShaderVar::TypeModifier::Attribute);
      inputs += programBuilder.getShaderVarDeclarations(var, stage);
      inputs += ";\n";
    }
    for (const auto& varying : interface.varyings) {
      ShaderVar var(
          varying.name, varying.type,
          varying.flat ? ShaderVar::TypeModifier::FlatVarying : ShaderVar::TypeModifier::Varying);
      outputs += programBuilder.getShaderVarDeclarations(var, stage);
      outputs += ";\n";
    }
  } else {
    for (const auto& varying : interface.varyings) {
      ShaderVar var(
          varying.name, varying.type,
          varying.flat ? ShaderVar::TypeModifier::FlatVarying : ShaderVar::TypeModifier::Varying);
      inputs += programBuilder.getShaderVarDeclarations(var, stage);
      inputs += ";\n";
    }
  }

  std::string out = {};
  AppendSection(&out, builder.versionDecl());
  AppendSection(&out, builder.extensions());
  AppendSection(&out, builder.definitions());
  AppendSection(&out, builder.precisionQualifier());
  AppendSection(&out, uniforms);
  AppendSection(&out, inputs);
  AppendSection(&out, outputs);
  AppendSection(&out, builder.functions());
  AppendSection(&out, builder.mainHeader());
  AppendSection(&out, builder.code());
  return out;
}

}  // namespace tgfx

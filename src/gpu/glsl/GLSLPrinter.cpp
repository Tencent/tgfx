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
#include "core/utils/Log.h"
#include "GLSLOperatorEmitter.h"
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


namespace {
/// The GLSL spelling of one IR type (the statement layer's own table: the IR printer route
/// declares locals, so it needs the names directly).
const char* IRTypeGLSLName(SLType type) {
  switch (type) {
    case SLType::Float:
      return "float";
    case SLType::Float2:
      return "vec2";
    case SLType::Float3:
      return "vec3";
    case SLType::Float4:
      return "vec4";
    case SLType::Int:
      return "int";
    case SLType::Int2:
      return "ivec2";
    case SLType::Int3:
      return "ivec3";
    case SLType::Int4:
      return "ivec4";
    case SLType::Float2x2:
      return "mat2";
    case SLType::Float3x3:
      return "mat3";
    case SLType::Float4x4:
      return "mat4";
    default:
      return nullptr;
  }
}

const char* IRPrecisionGLSL(Precision precision) {
  switch (precision) {
    case Precision::High:
      return "highp ";
    case Precision::Medium:
      return "mediump ";
    default:
      return "";
  }
}

/// Prints one statement body line into out. Returns false when the statement cannot be printed
/// yet (the caller treats that as "the IR printer does not cover this program").
bool PrintIRStatement(const ProgramIR& program, const IRStatement& statement,
                       std::set<std::string>* declared, std::string* out) {
  std::string source = {};
  std::string error = {};
  if (statement.src == INVALID_VALUE_ID) {
    LOGE("IRPrinter: a statement has no source value (kind=%d)", static_cast<int>(statement.kind));
    return false;
  }
  if (!GLSLOperatorEmitter::EmitValue(program, statement.src, &source, &error)) {
    LOGE("IRPrinter: a statement source cannot be printed: %s", error.c_str());
    return false;
  }
  if (statement.dst.base == INVALID_VALUE_ID || statement.dst.base >= program.values.size()) {
    LOGE("IRPrinter: a statement has no destination value");
    return false;
  }
  const auto& dstValue = program.values[statement.dst.base];
  if (dstValue.debugName.empty()) {
    LOGE("IRPrinter: a statement destination has no name");
    return false;
  }
  // The xfer route's logical output name maps to the stage's color output (the same mapping the
  // SPIR-V route applies to its output variable).
  const bool isColorOutput = dstValue.debugName == "fragmentOutput";
  std::string dst = isColorOutput ? CUSTOM_COLOR_OUTPUT_NAME : dstValue.debugName;
  if (!statement.dst.swizzle.empty()) {
    dst += "." + statement.dst.swizzle;
  }
  const bool canDeclare = statement.dst.swizzle.empty() && IRTypeGLSLName(dstValue.type) != nullptr;
  const bool needsDeclare =
      canDeclare && !isColorOutput &&
      (dstValue.kind == IRValueKind::Local || dstValue.kind == IRValueKind::Expr) &&
      declared->insert(dstValue.debugName).second;
  if (needsDeclare) {
    *out += "  ";
    *out += IRPrecisionGLSL(dstValue.precision);
    *out += IRTypeGLSLName(dstValue.type);
    *out += " ";
    *out += dst;
    *out += " = ";
    *out += source;
    *out += ";\n";
  } else {
    *out += "  ";
    *out += dst;
    *out += " = ";
    *out += source;
    *out += ";\n";
  }
  return true;
}
}  // namespace

namespace {
/// Whether a printed identifier is resolvable in the printed body: declared by a statement,
/// provided by the interface, a builtin or a function name. Unresolvable names mean the IR route
/// is missing a definition (for example a combinator FP that never recorded one), and the caller
/// falls back to the recorded text.
bool ResolvesIdentifier(const std::string& name, const ProgramIR& program,
                         const std::set<std::string>& declared) {
  if (declared.count(name) != 0) {
    return true;
  }
  for (const auto& varying : program.interface.varyings) {
    if (varying.name == name) {
      return true;
    }
  }
  for (const auto& uniform : program.interface.uniforms) {
    if (uniform.name == name) {
      return true;
    }
  }
  for (const auto& sampler : program.interface.samplers) {
    if (sampler.name == name) {
      return true;
    }
  }
  for (const auto& value : program.values) {
    if (value.kind == IRValueKind::Uniform || value.kind == IRValueKind::Varying ||
        value.kind == IRValueKind::Sampler || value.kind == IRValueKind::Attribute) {
      if (value.debugName == name) {
        return true;
      }
    }
  }
  static const std::set<std::string> builtins = {
      "gl_Position",  "gl_FragCoord", "tgfx_FragColor", "vec2",  "vec3",  "vec4",
      "float",        "int",          "mat2",           "mat3",  "mat4",  "clamp",
      "mix",          "step",         "min",            "max",   "abs",   "texture",
      "highp",        "mediump",      "lowp",           "ivec2", "ivec3", "ivec4",
      "sampler2D",    "sampler2DRect",
      "tgfxTextureSample", "tgfxTextureSampleRect", "tgfxClamp"};
  if (builtins.count(name) != 0) {
    return true;
  }
  // Any declared operator's function symbol resolves (the finalize step pulls its source).
  for (size_t index = 0; index < OperatorCount(); ++index) {
    const auto* desc = OperatorAt(index);
    if (desc != nullptr && desc->glslSymbol != nullptr && name == desc->glslSymbol) {
      return true;
    }
  }
  return false;
}
}  // namespace

namespace {
/// Collects every value an expression tree references, expanding intermediate expr values.
void CollectExprValueRefs(const ProgramIR& program, ExprID exprId, std::set<ValueID>* out) {
  if (exprId == INVALID_EXPR_ID || exprId >= program.exprs.size()) {
    return;
  }
  const auto& expr = program.exprs[exprId];
  for (const auto arg : expr.args) {
    if (arg == INVALID_VALUE_ID || arg >= program.values.size()) {
      continue;
    }
    out->insert(arg);
    const auto& value = program.values[arg];
    if (value.definingExpr != INVALID_EXPR_ID) {
      CollectExprValueRefs(program, value.definingExpr, out);
    }
  }
}

/// The names the printer must never rewrite: builtin outputs and the xfer route's logical output
/// (the printer maps it to the color output).
bool IsReservedName(const std::string& name) {
  return name == "gl_Position" || name == "gl_FragCoord" || name == "fragmentOutput";
}
}  // namespace

void GLSLPrinter::UniquifyValueNames(ProgramIR& program) {
  std::set<ValueID> referenced = {};
  auto collectStatements = [&](const std::vector<IRStatement>& statements) {
    for (const auto& statement : statements) {
      const ValueID ids[] = {statement.dst.base, statement.src, statement.loopInit,
                             statement.loopBound, statement.loopStep};
      for (const auto id : ids) {
        if (id == INVALID_VALUE_ID || id >= program.values.size()) {
          continue;
        }
        referenced.insert(id);
        const auto& value = program.values[id];
        if (value.definingExpr != INVALID_EXPR_ID) {
          CollectExprValueRefs(program, value.definingExpr, &referenced);
        }
      }
    }
  };
  collectStatements(program.statements);
  collectStatements(program.vertexStatements);
  // Count printable names: only local/expr values own renameable names.
  std::map<std::string, size_t> counts = {};
  for (const auto id : referenced) {
    const auto& value = program.values[id];
    if ((value.kind == IRValueKind::Local || value.kind == IRValueKind::Expr) &&
        !IsReservedName(value.debugName)) {
      ++counts[value.debugName];
    }
  }
  size_t counter = 0;
  for (const auto id : referenced) {
    auto& value = program.values[id];
    if (value.kind != IRValueKind::Local && value.kind != IRValueKind::Expr) {
      continue;
    }
    if (IsReservedName(value.debugName)) {
      continue;
    }
    if (value.debugName.empty() || counts[value.debugName] > 1) {
      value.debugName = "v" + std::to_string(counter++);
    }
  }
}

std::string GLSLPrinter::PrintFragmentBodyFromIR(const ProgramIR& program) {
  std::string out = {};
  std::string error = {};
  std::set<std::string> declared = {};
  // The fragment epilogue writes through the stage output; it is declared by the header, and the
  // position output is a builtin.
  declared.insert("gl_Position");
  for (const auto& statement : program.statements) {
    if (statement.kind == IRStatementKind::ForLoopBegin) {
      std::string init = {};
      std::string bound = {};
      if (!GLSLOperatorEmitter::EmitValue(program, statement.loopInit, &init, &error) ||
          !GLSLOperatorEmitter::EmitValue(program, statement.loopBound, &bound, &error)) {
        LOGE("IRPrinter: the loop '%s' bounds cannot be printed: %s", statement.loopVarName.c_str(),
             error.c_str());
        return {};
      }
      // The counter is a loop-local declared by the header itself.
      declared.insert(statement.loopVarName);
      out += "  for (int " + statement.loopVarName + " = " + init + "; " + statement.loopVarName +
             " <= " + bound + "; ++" + statement.loopVarName + ") {\n";
      continue;
    }
    if (statement.kind == IRStatementKind::ForLoopEnd) {
      out += "  }\n";
      continue;
    }
    if (statement.kind != IRStatementKind::Assign && statement.kind != IRStatementKind::StoreOutput) {
      return {};
    }
    const size_t before = out.size();
    if (!PrintIRStatement(program, statement, &declared, &out)) {
      return {};
    }
    // The printed line must only reference resolvable names: a bare identifier that nothing
    // defines means the IR route is incomplete for this program.
    const std::string line = out.substr(before);
    size_t index = 0;
    while (index < line.size()) {
      const char c = line[index];
      // A '.'-suffixed name is a swizzle or member: its spelling is never an identifier to
      // resolve.
      if (c == '.') {
        ++index;
        while (index < line.size() &&
               (std::isalnum(static_cast<unsigned char>(line[index])) || line[index] == '_')) {
          ++index;
        }
        continue;
      }
      if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        size_t end = index + 1;
        while (end < line.size() && (std::isalnum(static_cast<unsigned char>(line[end])) ||
                                     line[end] == '_')) {
          ++end;
        }
        const std::string identifier = line.substr(index, end - index);
        if (!ResolvesIdentifier(identifier, program, declared)) {
          for (const auto& value : program.values) {
            if (value.debugName == identifier) {
              LOGE("IRPrinter: unresolved '%s': kind=%d type=%d defining=%d", identifier.c_str(),
                   static_cast<int>(value.kind), static_cast<int>(value.type),
                   static_cast<int>(value.definingExpr));
            }
          }
          LOGE("IRPrinter: an identifier is unresolved: '%s' in line: %s", identifier.c_str(),
               line.c_str());
          if (std::getenv("TGFX_IR_DIAG") != nullptr) {
            for (const auto& value : program.values) {
              if (!value.debugName.empty()) {
                LOGE("IRDIAG value '%s' id=%u kind=%d type=%d defining=%d",
                     value.debugName.c_str(), value.id, static_cast<int>(value.kind),
                     static_cast<int>(value.type), static_cast<int>(value.definingExpr));
              }
            }
            for (const auto& statement : program.statements) {
              const auto dstName = statement.dst.base < program.values.size()
                                       ? program.values[statement.dst.base].debugName
                                       : std::string();
              LOGE("IRDIAG stmt kind=%d dst='%s' dstId=%u", static_cast<int>(statement.kind),
                   dstName.c_str(), statement.dst.base);
            }
          }
          return {};
        }
        index = end;
      } else {
        ++index;
      }
    }
  }
  return out;
}

std::string GLSLPrinter::PrintVertexBodyFromIR(const ProgramIR& program) {
  std::string out = {};
  std::set<std::string> declared = {};
  declared.insert("gl_Position");
  for (const auto& statement : program.vertexStatements) {
    if (statement.kind == IRStatementKind::StoreVarying) {
      // The varying is declared by the header; the body only assigns it.
      std::string source = {};
      std::string error = {};
      if (statement.src == INVALID_VALUE_ID ||
          !GLSLOperatorEmitter::EmitValue(program, statement.src, &source, &error)) {
        return {};
      }
      const auto* varyingValue = statement.dst.base < program.values.size()
                                     ? &program.values[statement.dst.base]
                                     : nullptr;
      if (varyingValue == nullptr || varyingValue->debugName.empty()) {
        return {};
      }
      out += "  " + varyingValue->debugName + " = " + source + ";\n";
      continue;
    }
    if (statement.kind == IRStatementKind::Assign || statement.kind == IRStatementKind::StoreOutput) {
      if (!PrintIRStatement(program, statement, &declared, &out)) {
        return {};
      }
      continue;
    }
    return {};
  }
  return out;
}

std::string GLSLPrinter::PrintStage(const ProgramBuilder& programBuilder, ShaderStage stage,
                                    const InterfaceDesc& interface, const ShaderBuilder& builder) {
  std::string uniforms = builder.extraUniforms();
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

////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
////////////////////////////////////////////////////////////////////////////////////////////////

#include "HLSLPrinter.h"
#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>
#include <vector>
#include "core/utils/Log.h"
#include "gpu/ProgramBuilder.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderBindingLayout.h"
#include "gpu/ShaderBuilder.h"
#include "gpu/UniformData.h"
#include "gpu/hlsl/HLSLOperatorEmitter.h"

namespace tgfx {
namespace {

constexpr char Indent[] = "  ";

/// The local the vertex body writes the clip-space position into. The GLSL printer writes
/// gl_Position; Direct3D names that output through SV_Position on a struct member, so the body
/// assigns a local that the epilogue copies into the struct.
constexpr char PositionLocal[] = "tgfx_Position";

/// The local the fragment body writes the color into; the epilogue returns it. Same name the GLSL
/// route uses for its output, which is why the mapping below is an identity for this one.
constexpr char FragCoordLocal[] = "tgfx_FragCoord";

void AppendSection(std::string* out, const std::string& section) {
  if (section.empty()) {
    return;
  }
  *out += section;
  *out += "\n";
}

const char* ComponentOf(size_t index) {
  static const char* components[] = {"x", "y", "z", "w"};
  return components[index % 4];
}

/// The name the printed body writes for one IR value. The position and the fragment color are the
/// two values the interface owns: the body writes a plain local and the stage epilogue copies it
/// into the system-value output.
std::string OutputNameFor(const std::string& debugName) {
  if (debugName == "gl_Position") {
    return PositionLocal;
  }
  if (debugName == "fragmentOutput") {
    return CUSTOM_COLOR_OUTPUT_NAME;
  }
  return debugName;
}

/// The identifiers a printed line may reference without the body declaring them: HLSL types and
/// math functions, the Direct3D sampler readers, the stage outputs and the operator symbols.
bool IsBuiltinIdentifier(const std::string& name) {
  static const std::set<std::string> builtins = {
      "float",   "float2",  "float3",  "float4",  "half",    "half2",   "half3",   "half4",
      "int",     "int2",    "int3",    "int4",    "float2x2", "float3x3", "float4x4",
      "min",     "max",     "abs",     "clamp",   "lerp",    "frac",    "fmod",    "rsqrt",
      "sqrt",    "pow",     "exp",     "log",     "sin",     "cos",     "tan",     "atan2",
      "step",    "smoothstep", "normalize", "dot", "length", "saturate", "sign",   "floor",
      "ceil",    "round",   "trunc",   "discard", "true",    "false",   "return",  "if",
      "else",    "for",     "while",   "break",   "continue",
      "tgfxTextureSample", "tgfxTextureSampleRect", "tgfxTextureSampleExternal",
      PositionLocal, FragCoordLocal, CUSTOM_COLOR_OUTPUT_NAME};
  return builtins.count(name) != 0;
}

bool ResolvesIdentifier(const std::string& name, const ProgramIR& program,
                        const std::set<std::string>& declared) {
  if (declared.count(name) != 0 || IsBuiltinIdentifier(name)) {
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
  for (const auto& uniform : program.interface.uniformArrays) {
    if (uniform.name == name) {
      return true;
    }
  }
  for (const auto& sampler : program.interface.samplers) {
    // A read goes through the texture variable and the sampler state the stage declares beside it.
    if (sampler.name == name || HLSLOperatorEmitter::SamplerStateName(sampler.name) == name) {
      return true;
    }
  }
  for (const auto& value : program.values) {
    if (value.kind == IRValueKind::Uniform || value.kind == IRValueKind::UniformArray ||
        value.kind == IRValueKind::Varying || value.kind == IRValueKind::Sampler ||
        value.kind == IRValueKind::Attribute) {
      if (value.debugName == name) {
        return true;
      }
    }
  }
  for (size_t index = 0; index < OperatorCount(); ++index) {
    const auto* desc = OperatorAt(index);
    if (desc != nullptr && desc->glslSymbol != nullptr && name == desc->glslSymbol) {
      return true;
    }
  }
  return false;
}

/// Prints one statement body line into out. Returns false when the statement cannot be printed.
bool PrintIRStatement(const ProgramIR& program, const IRStatement& statement,
                      std::set<std::string>* declared, std::string* out) {
  std::string source = {};
  std::string error = {};
  if (statement.src == INVALID_VALUE_ID) {
    LOGE("HLSLPrinter: a statement has no source value (kind=%d)",
         static_cast<int>(statement.kind));
    return false;
  }
  if (!HLSLOperatorEmitter::EmitValue(program, statement.src, &source, &error)) {
    LOGE("HLSLPrinter: a statement source cannot be printed: %s", error.c_str());
    return false;
  }
  if (statement.dst.base == INVALID_VALUE_ID || statement.dst.base >= program.values.size()) {
    LOGE("HLSLPrinter: a statement has no destination value");
    return false;
  }
  const auto& dstValue = program.values[statement.dst.base];
  if (dstValue.debugName.empty()) {
    LOGE("HLSLPrinter: a statement destination has no name");
    return false;
  }
  const bool isColorOutput = dstValue.debugName == "fragmentOutput";
  const bool isPositionOutput = dstValue.debugName == "gl_Position";
  std::string dst = OutputNameFor(dstValue.debugName);
  if (!statement.dst.swizzle.empty()) {
    dst += "." + statement.dst.swizzle;
  }
  const char* typeName = HLSLOperatorEmitter::TypeName(dstValue.type);
  // A field assignment (yuv.x = ...) cannot declare through the whole-variable path below, so its
  // base is declared once here; the fields then assign into that variable.
  if (!statement.dst.swizzle.empty() && !isColorOutput && !isPositionOutput &&
      declared->insert(dstValue.debugName).second) {
    if (typeName != nullptr) {
      *out += Indent;
      *out += typeName;
      *out += " ";
      *out += dstValue.debugName;
      *out += ";\n";
    }
  }
  const bool canDeclare = statement.dst.swizzle.empty() && typeName != nullptr;
  const bool needsDeclare =
      canDeclare && !isColorOutput && !isPositionOutput &&
      (dstValue.kind == IRValueKind::Local || dstValue.kind == IRValueKind::Expr) &&
      declared->insert(dstValue.debugName).second;
  *out += Indent;
  if (needsDeclare) {
    *out += typeName;
    *out += " ";
  }
  *out += dst;
  *out += " = ";
  *out += source;
  *out += ";\n";
  return true;
}

/// Prints one stage's uniform block as a cbuffer whose members are pinned to their std140
/// placements. `layout` is the CPU-side table the bytes are written through, so the two cannot
/// drift; a missing placement is logged and the member is emitted unpinned, which only compiles
/// when the natural packing happens to agree.
std::string PrintUniformBlock(ShaderStage stage, int registerIndex,
                             const std::vector<UniformDesc>& uniforms,
                             const UniformData* layout, bool* ok) {
  *ok = true;
  std::vector<const UniformDesc*> members = {};
  for (const auto& uniform : uniforms) {
    if (uniform.stage == stage) {
      members.push_back(&uniform);
    }
  }
  if (members.empty()) {
    return "";
  }
  const char* blockName =
      stage == ShaderStage::Vertex ? VertexUniformBlockName : FragmentUniformBlockName;
  std::string out = "cbuffer ";
  out += blockName;
  out += " : register(b";
  out += std::to_string(registerIndex);
  out += ") {\n";
  for (const auto* uniform : members) {
    const char* typeName = HLSLOperatorEmitter::TypeName(uniform->type);
    if (typeName == nullptr) {
      LOGE("HLSLPrinter: uniform '%s' has no HLSL type", uniform->name.c_str());
      *ok = false;
      return {};
    }
    out += Indent;
    out += typeName;
    out += " ";
    out += uniform->name;
    if (uniform->count > 1) {
      out += "[" + std::to_string(uniform->count) + "]";
    }
    UniformData::FieldLayout field = {};
    if (layout == nullptr || !layout->fieldLayout(uniform->name, &field)) {
      // Without the derived placement the member cannot be pinned, and HLSL's own packing (a
      // float3 followed by a float shares one register) would read the wrong bytes. Refusing here
      // keeps a layout drift a build failure instead of a silently wrong pixel.
      LOGE("HLSLPrinter: uniform '%s' has no derived std140 placement", uniform->name.c_str());
      *ok = false;
      return {};
    }
    // std140 places every member on a 4-byte boundary, so the packoffset register/component pair
    // is exact.
    out += " : packoffset(c" + std::to_string(field.offset / 16) + "." +
           ComponentOf((field.offset % 16) / 4) + ")";
    out += ";\n";
  }
  out += "};\n";
  return out;
}

/// Prints the sampler bindings: a texture at t{i} and the sampler state at s{i}, both at the index
/// the pipeline's descriptor tables use (BindingLayout's texture sampler order).
std::string PrintSamplers(const std::vector<SamplerDesc>& samplers, bool* ok) {
  *ok = true;
  std::string out = {};
  size_t index = 0;
  for (const auto& sampler : samplers) {
    if (sampler.type != SLType::Texture2DSampler) {
      LOGE("HLSLPrinter: sampler '%s' has no Direct3D form (type=%d)", sampler.name.c_str(),
           static_cast<int>(sampler.type));
      *ok = false;
      return {};
    }
    out += "Texture2D<float4> " + sampler.name + " : register(t" + std::to_string(index) + ");\n";
    out += "SamplerState " + HLSLOperatorEmitter::SamplerStateName(sampler.name) + " : register(s" +
           std::to_string(index) + ");\n";
    ++index;
  }
  return out;
}

std::string PrintVertexInputStruct(const std::vector<AttributeDesc>& attributes) {
  std::string out = "struct VSIn {\n";
  size_t index = 0;
  for (const auto& attribute : attributes) {
    const char* typeName = HLSLOperatorEmitter::TypeName(attribute.type);
    if (typeName == nullptr) {
      LOGE("HLSLPrinter: attribute '%s' has no HLSL type", attribute.name.c_str());
      return "";
    }
    // The input layout numbers the slots consecutively in attribute order (the runtime-built
    // shaders' dense convention), which is the same order the pipeline walks the buffer layouts.
    out += Indent;
    out += typeName;
    out += " " + attribute.name + " : TEXCOORD" + std::to_string(index++) + ";\n";
  }
  out += "};\n";
  return out;
}

std::string PrintVaryingStructMembers(const std::vector<VaryingDesc>& varyings, bool* ok) {
  *ok = true;
  std::string out = {};
  for (const auto& varying : varyings) {
    const char* typeName = HLSLOperatorEmitter::TypeName(varying.type);
    if (typeName == nullptr) {
      LOGE("HLSLPrinter: varying '%s' has no HLSL type", varying.name.c_str());
      *ok = false;
      return {};
    }
    out += Indent;
    if (varying.flat) {
      // A flat varying carries the provoking vertex's value on both sides; the qualifier must
      // match on the writer and the reader or the pipeline rejects the signature.
      out += "nointerpolation ";
    }
    out += typeName;
    out += " " + varying.name + " : TEXCOORD" + std::to_string(varying.location) + ";\n";
  }
  return out;
}

std::string PrintStageOutputStruct(const std::vector<VaryingDesc>& varyings, bool* ok) {
  std::string out = "struct VSOut {\n";
  out += Indent;
  out += "float4 position : SV_Position;\n";
  out += PrintVaryingStructMembers(varyings, ok);
  if (!*ok) {
    return {};
  }
  out += "};\n";
  return out;
}

std::string PrintStageInputStruct(ShaderStage stage, const std::vector<VaryingDesc>& varyings,
                                  bool* ok) {
  std::string out = stage == ShaderStage::Vertex ? "struct VSIn {\n" : "struct PSIn {\n";
  out += Indent;
  out += "float4 position : SV_Position;\n";
  out += PrintVaryingStructMembers(varyings, ok);
  if (!*ok) {
    return {};
  }
  out += "};\n";
  return out;
}

/// The local declarations that make the stage input reachable under the names the printed body
/// references: attributes and varyings keep their interface names inside main.
/// The HLSL spelling of one IR type as a string; empty when the type has no form (the caller then
/// fails the stage instead of printing a null type name).
std::string TypeSpelling(SLType type) {
  const char* name = HLSLOperatorEmitter::TypeName(type);
  return name == nullptr ? std::string() : std::string(name);
}

std::string PrintBodyDeclarations(ShaderStage stage, const InterfaceDesc& interface, bool* ok) {
  *ok = true;
  std::string out = {};
  auto appendLocal = [&](const std::string& typeName, const std::string& declaration) {
    if (typeName.empty()) {
      *ok = false;
      return;
    }
    out += Indent;
    out += typeName;
    out += " ";
    out += declaration;
    out += ";\n";
  };
  if (stage == ShaderStage::Vertex) {
    for (const auto& attribute : interface.attributes) {
      appendLocal(TypeSpelling(attribute.type), attribute.name + " = stage_input." + attribute.name);
    }
    out += Indent;
    out += std::string("float4 ") + PositionLocal + " = float4(0.0, 0.0, 0.0, 1.0);\n";
    for (const auto& varying : interface.varyings) {
      appendLocal(TypeSpelling(varying.type), varying.name);
    }
    return out;
  }
  out += Indent;
  out += std::string("float4 ") + FragCoordLocal + " = stage_input.position;\n";
  for (const auto& varying : interface.varyings) {
    appendLocal(TypeSpelling(varying.type), varying.name + " = stage_input." + varying.name);
  }
  out += Indent;
  out += std::string("float4 ") + CUSTOM_COLOR_OUTPUT_NAME + " = float4(0.0, 0.0, 0.0, 0.0);\n";
  return out;
}

}  // namespace

void HLSLPrinter::UniquifyValueNames(ProgramIR& program) {
  // The rule is language-neutral, so the GLSL printer's pass applies unchanged; it lives here so
  // the HLSL route does not depend on the GLSL text layer.
  std::set<ValueID> referenced = {};
  std::function<void(ExprID)> collectExpr = [&](ExprID exprId) {
    if (exprId == INVALID_EXPR_ID || exprId >= program.exprs.size()) {
      return;
    }
    for (const auto arg : program.exprs[exprId].args) {
      if (arg == INVALID_VALUE_ID || arg >= program.values.size()) {
        continue;
      }
      referenced.insert(arg);
      collectExpr(program.values[arg].definingExpr);
    }
  };
  auto collectStatements = [&](const std::vector<IRStatement>& statements) {
    for (const auto& statement : statements) {
      const ValueID ids[] = {statement.dst.base, statement.src, statement.loopInit,
                             statement.loopBound, statement.loopStep};
      for (const auto id : ids) {
        if (id == INVALID_VALUE_ID || id >= program.values.size()) {
          continue;
        }
        referenced.insert(id);
        collectExpr(program.values[id].definingExpr);
      }
    }
  };
  collectStatements(program.statements);
  collectStatements(program.vertexStatements);
  auto reserved = [](const std::string& name) {
    return name == "gl_Position" || name == "gl_FragCoord" || name == "fragmentOutput";
  };
  std::map<std::string, size_t> counts = {};
  for (const auto id : referenced) {
    const auto& value = program.values[id];
    if ((value.kind == IRValueKind::Local || value.kind == IRValueKind::Expr) &&
        !reserved(value.debugName)) {
      ++counts[value.debugName];
    }
  }
  size_t counter = 0;
  for (const auto id : referenced) {
    auto& value = program.values[id];
    if (value.kind != IRValueKind::Local && value.kind != IRValueKind::Expr) {
      continue;
    }
    if (reserved(value.debugName)) {
      continue;
    }
    if (value.debugName.empty() || counts[value.debugName] > 1) {
      value.debugName = "v" + std::to_string(counter++);
    }
  }
}

std::string HLSLPrinter::PrintFragmentBodyFromIR(const ProgramIR& program) {
  std::string out = {};
  std::string error = {};
  std::set<std::string> declared = {};
  // The epilogue writes through the stage outputs; they are declared by the header.
  declared.insert(PositionLocal);
  declared.insert(CUSTOM_COLOR_OUTPUT_NAME);
  declared.insert(FragCoordLocal);
  for (const auto& statement : program.statements) {
    if (statement.kind == IRStatementKind::ForLoopBegin) {
      std::string init = {};
      std::string bound = {};
      if (!HLSLOperatorEmitter::EmitValue(program, statement.loopInit, &init, &error) ||
          !HLSLOperatorEmitter::EmitValue(program, statement.loopBound, &bound, &error)) {
        LOGE("HLSLPrinter: the loop '%s' bounds cannot be printed: %s",
             statement.loopVarName.c_str(), error.c_str());
        return {};
      }
      declared.insert(statement.loopVarName);
      out += Indent;
      out += "for (int " + statement.loopVarName + " = " + init + "; " + statement.loopVarName +
             " <= " + bound + "; ++" + statement.loopVarName + ") {\n";
      continue;
    }
    if (statement.kind == IRStatementKind::ForLoopEnd) {
      out += Indent;
      out += "}\n";
      continue;
    }
    if (statement.kind == IRStatementKind::RawText) {
      // RawText carries GLSL text from a processor that has no IR form yet. There is no HLSL
      // spelling of it, so the program is refused instead of compiling GLSL into an HLSL shader.
      LOGE("HLSLPrinter: a processor emitted GLSL raw text ('%s'); it has no HLSL form",
           statement.rawDeclaredName.c_str());
      return {};
    }
    if (statement.kind == IRStatementKind::Discard) {
      std::string condition = {};
      if (!HLSLOperatorEmitter::EmitValue(program, statement.src, &condition, &error)) {
        LOGE("HLSLPrinter: the discard condition cannot be printed: %s", error.c_str());
        return {};
      }
      out += Indent;
      out += "if (" + condition + ".r <= 0.0 && " + condition + ".g <= 0.0 && " + condition +
             ".b <= 0.0) {\n";
      out += Indent;
      out += Indent;
      out += "discard;\n";
      out += Indent;
      out += "}\n";
      continue;
    }
    if (statement.kind != IRStatementKind::Assign &&
        statement.kind != IRStatementKind::StoreOutput) {
      LOGE("HLSLPrinter: statement kind %d has no HLSL form", static_cast<int>(statement.kind));
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
          LOGE("HLSLPrinter: an identifier is unresolved: '%s' in line: %s", identifier.c_str(),
               line.c_str());
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

std::string HLSLPrinter::PrintVertexBodyFromIR(const ProgramIR& program) {
  std::string out = {};
  std::set<std::string> declared = {};
  declared.insert(PositionLocal);
  for (const auto& statement : program.vertexStatements) {
    if (statement.kind == IRStatementKind::StoreVarying) {
      std::string source = {};
      std::string error = {};
      if (statement.src == INVALID_VALUE_ID ||
          !HLSLOperatorEmitter::EmitValue(program, statement.src, &source, &error)) {
        return {};
      }
      const auto* varyingValue = statement.dst.base < program.values.size()
                                     ? &program.values[statement.dst.base]
                                     : nullptr;
      if (varyingValue == nullptr || varyingValue->debugName.empty()) {
        return {};
      }
      // The varying is a local declared by the header; the body only assigns it.
      out += Indent;
      out += varyingValue->debugName + " = " + source + ";\n";
      continue;
    }
    if (statement.kind == IRStatementKind::Assign || statement.kind == IRStatementKind::StoreOutput) {
      if (!PrintIRStatement(program, statement, &declared, &out)) {
        return {};
      }
      continue;
    }
    LOGE("HLSLPrinter: vertex statement kind %d has no HLSL form",
         static_cast<int>(statement.kind));
    return {};
  }
  return out;
}

std::string HLSLPrinter::PrintStage(const ProgramBuilder& programBuilder, ShaderStage stage,
                                    const InterfaceDesc& interface, const ShaderBuilder& builder,
                                    const UniformData* uniformLayout) {
  // The builder is not consulted directly: the stage text is assembled from the typed interface
  // and the two builders' sections, which is why the parameter is otherwise unused.
  (void)programBuilder;
  std::string out = {};
  bool ok = true;
  // The uniform block first: the function section and the body both read its members.
  const int registerIndex =
      stage == ShaderStage::Vertex ? VERTEX_UBO_BINDING_POINT : FRAGMENT_UBO_BINDING_POINT;
  AppendSection(&out,
                PrintUniformBlock(stage, registerIndex, interface.uniforms, uniformLayout, &ok));
  if (!ok) {
    return {};
  }
  AppendSection(&out, PrintSamplers(interface.samplers, &ok));
  if (!ok) {
    return {};
  }
  std::string structs = {};
  if (stage == ShaderStage::Vertex) {
    structs = PrintVertexInputStruct(interface.attributes);
    structs += PrintStageOutputStruct(interface.varyings, &ok);
  } else {
    structs = PrintStageInputStruct(stage, interface.varyings, &ok);
  }
  if (!ok || structs.empty()) {
    return {};
  }
  AppendSection(&out, structs);
  AppendSection(&out, builder.functions());
  const auto declarations = PrintBodyDeclarations(stage, interface, &ok);
  if (!ok) {
    return {};
  }
  if (stage == ShaderStage::Vertex) {
    out += "VSOut main(VSIn stage_input) {\n";
  } else {
    out += "float4 main(PSIn stage_input) : SV_Target {\n";
  }
  out += declarations;
  out += builder.code();
  return out;
}

}  // namespace tgfx

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

#include "ProgramBodyPrinter.h"
#include <cctype>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include "core/utils/Log.h"
#include "gpu/glsl/GLSLOperatorEmitter.h"
#include "gpu/ir/ShaderLanguageNaming.h"
#include "gpu/FragmentShaderBuilder.h"

namespace tgfx {
namespace {

bool PrintIRStatement(const ProgramIR& program, const IRStatement& statement,
                       std::set<std::string>* declared, std::string* out,
                       const BodyPrintTraits& traits) {
  std::string source = {};
  std::string error = {};
  if (statement.src == INVALID_VALUE_ID) {
    LOGE("IRPrinter: a statement has no source value (kind=%d)", static_cast<int>(statement.kind));
    return false;
  }
  if (!GLSLOperatorEmitter::EmitValue(program, statement.src, &source, &error, traits.language)) {
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
  // A language that keeps the clip-space position in its stage output writes it there, flipping y
  // where the language's clip space points the other way.
  if (traits.positionTarget != nullptr && statement.dst.base < program.values.size() &&
      program.values[statement.dst.base].debugName == "gl_Position") {
    *out += "  ";
    *out += traits.positionTarget;
    *out += " = ";
    if (traits.flipPositionY) {
      *out += std::string(ValueTypeName(traits.language, SLType::Float4)) + "((" + source +
              ").x, -(" + source + ").y, (" + source + ").z, (" + source + ").w);\n";
    } else {
      *out += source + ";\n";
    }
    return true;
  }
  // The xfer route's logical output name maps to the stage's color output (the same mapping the
  // SPIR-V route applies to its output variable).
  const bool isColorOutput = dstValue.debugName == "fragmentOutput";
  std::string dst = isColorOutput ? CUSTOM_COLOR_OUTPUT_NAME : dstValue.debugName;
  if (!statement.dst.swizzle.empty()) {
    dst += "." + statement.dst.swizzle;
  }
  // A field assignment (yuv.x = ...) cannot declare through the whole-variable path below, so
  // its base is declared once here; the fields then assign into that variable.
  if (!statement.dst.swizzle.empty() && !isColorOutput && declared->insert(dstValue.debugName).second) {
    if (const char* typeName = ValueTypeName(traits.language, dstValue.type)) {
      *out += "  ";
      *out += ValuePrecisionPrefix(traits.language, dstValue.precision);
      *out += typeName;
      *out += " ";
      *out += dstValue.debugName;
      *out += ";\n";
    }
  }
  const bool canDeclare =
      statement.dst.swizzle.empty() && ValueTypeName(traits.language, dstValue.type) != nullptr;
  const bool needsDeclare =
      canDeclare && !isColorOutput &&
      (dstValue.kind == IRValueKind::Local || dstValue.kind == IRValueKind::Expr) &&
      declared->insert(dstValue.debugName).second;
  if (needsDeclare) {
    *out += "  ";
    *out += ValuePrecisionPrefix(traits.language, dstValue.precision);
    *out += ValueTypeName(traits.language, dstValue.type);
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

bool ResolvesIdentifier(const std::string& name, const ProgramIR& program,
                         const std::set<std::string>& declared,
                         const BodyPrintTraits& traits) {
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
    // An array slot ('KernelX_P1[0]') is spelled through the element name; the scanner stops at
    // the bracket, so the base name resolves through the declaration it prefixes.
    if (uniform.name.size() > name.size() && uniform.name.compare(0, name.size(), name) == 0 &&
        uniform.name[name.size()] == '[') {
      return true;
    }
  }
  // Uniform arrays are registered apart from the scalar block members.
  for (const auto& array : program.interface.uniformArrays) {
    if (array.name == name) {
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
      "tgfxTextureSample", "tgfxTextureSampleRect", "tgfxClamp",
      // The scalar math every library function body may use. Without these the statement printer
      // rejects an emitted expression that calls a builtin, and the whole program is dropped.
      "length",       "distance",     "dot",            "cross", "normalize",
      "sqrt",         "inversesqrt",  "pow",            "exp",   "log",
      "exp2",         "log2",         "sin",            "cos",   "tan",
      "asin",         "acos",         "atan",           "sign",  "floor",
      "ceil",         "fract",        "mod",            "smoothstep",
      "lessThan",     "lessThanEqual", "greaterThan",   "greaterThanEqual",
      "equal",        "notEqual",     "any",            "all",   "not"};
  if (builtins.count(name) != 0) {
    return true;
  }
  if (traits.language == ShaderLibraryLanguage::MSL) {
    static const std::set<std::string> msl = {
        "float2",       "float3",       "float4",     "float2x2",   "float3x3",
        "float4x4",     "int2",         "int3",       "int4",       "out",
        "in",           "uint",         "discard_fragment", "tgfx_FragHeight"};
    if (msl.count(name) != 0) {
      return true;
    }
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

bool IsReservedName(const std::string& name) {
  return name == "gl_Position" || name == "gl_FragCoord" || name == "fragmentOutput" ||
         name == CUSTOM_COLOR_OUTPUT_NAME;
}
}  // namespace

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

void ProgramBodyPrinter::Uniquify(ProgramIR& program) {
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

std::string ProgramBodyPrinter::PrintFragment(const ProgramIR& program,
                                             const BodyPrintTraits& traits) {
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
      if (!GLSLOperatorEmitter::EmitValue(program, statement.loopInit, &init, &error, traits.language) ||
          !GLSLOperatorEmitter::EmitValue(program, statement.loopBound, &bound, &error,
                                          traits.language)) {
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
    if (statement.kind == IRStatementKind::RawText) {
      if (!statement.rawDeclaredName.empty()) {
        declared.insert(statement.rawDeclaredName);
      }
      out += statement.rawText;
      if (!out.empty() && out.back() != '\n') {
        out += "\n";
      }
      continue;
    }
    if (statement.kind == IRStatementKind::Discard) {
      std::string condition = {};
      if (!GLSLOperatorEmitter::EmitValue(program, statement.src, &condition, &error, traits.language)) {
        LOGE("IRPrinter: the discard condition cannot be printed: %s", error.c_str());
        return {};
      }
      // The text route's zero-coverage early-out: a fully transparent premultiplied source writes
      // nothing, so the pixel keeps the destination (a Clear that reaches it would zero it).
      const char* discard = traits.language == ShaderLibraryLanguage::MSL ? "discard_fragment();"
                                                                  : "discard;";
      out += "  if (" + condition + ".r <= 0.0 && " + condition + ".g <= 0.0 && " + condition +
             ".b <= 0.0) {\n    " + discard + "\n  }\n";
      continue;
    }
    if (statement.kind != IRStatementKind::Assign && statement.kind != IRStatementKind::StoreOutput) {
      return {};
    }
    const size_t before = out.size();
    if (!PrintIRStatement(program, statement, &declared, &out, traits)) {
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
        if (!ResolvesIdentifier(identifier, program, declared, traits)) {
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
              LOGE("IRDIAG stmt kind=%d dst='%s' dstId=%u srcId=%u", static_cast<int>(statement.kind),
                   dstName.c_str(), statement.dst.base, statement.src);
            }
            for (const auto& uniform : program.interface.uniforms) {
              LOGE("IRDIAG uniform '%s' stage=%d count=%u", uniform.name.c_str(),
                   static_cast<int>(uniform.stage), uniform.count);
            }
            for (const auto& varying : program.interface.varyings) {
              LOGE("IRDIAG varying '%s'", varying.name.c_str());
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
  // The stage contract: a fragment body that never stores the color output draws nothing at
  // all, and the caller would see a silently empty pass.
  {
    bool storesColor = false;
    for (const auto& statement : program.statements) {
      if (statement.kind == IRStatementKind::StoreOutput &&
          statement.dst.base < program.values.size()) {
        const auto& name = program.values[statement.dst.base].debugName;
        if (name == "fragmentOutput" || name == CUSTOM_COLOR_OUTPUT_NAME) {
          storesColor = true;
          break;
        }
      }
    }
    if (!storesColor) {
      LOGE("IRContract: the fragment stage never stores the color output");
    }
  }
  return out;
}

std::string ProgramBodyPrinter::PrintVertex(const ProgramIR& program,
                                           const BodyPrintTraits& traits) {
  std::string out = {};
  std::set<std::string> declared = {};
  declared.insert("gl_Position");
  for (const auto& statement : program.vertexStatements) {
    if (statement.kind == IRStatementKind::StoreVarying) {
      // The varying is declared by the header; the body only assigns it.
      std::string source = {};
      std::string error = {};
      if (statement.src == INVALID_VALUE_ID ||
          !GLSLOperatorEmitter::EmitValue(program, statement.src, &source, &error, traits.language)) {
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
      if (!PrintIRStatement(program, statement, &declared, &out, traits)) {
        return {};
      }
      continue;
    }
    return {};
  }
  return out;
}
}  // namespace tgfx

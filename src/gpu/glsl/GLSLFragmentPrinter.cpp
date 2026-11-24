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

#include "GLSLFragmentPrinter.h"

namespace tgfx {
namespace {

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

std::string PrintGLSLFragmentProgram(const FragmentProgram& program) {
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

}  // namespace tgfx

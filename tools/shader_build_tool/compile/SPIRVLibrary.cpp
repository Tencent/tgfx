/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "compile/SPIRVLibrary.h"
#include <cstring>
#include <set>
#include "compile/StageCompiler.h"

namespace tgfx {
namespace {

// The opcodes the surgery touches.
constexpr uint16_t kOpTypeVoid = 19;
constexpr uint16_t kOpEntryPoint = 15;
constexpr uint16_t kOpExecutionMode = 16;
constexpr uint16_t kOpName = 5;
constexpr uint16_t kOpMemberName = 6;
constexpr uint16_t kOpDecorate = 71;
constexpr uint16_t kOpCapability = 17;
constexpr uint16_t kOpFunction = 54;
constexpr uint16_t kOpFunctionParameter = 55;
constexpr uint16_t kOpFunctionCall = 57;
constexpr uint16_t kOpFunctionEnd = 56;
constexpr uint16_t kCapabilityLinkage = 5;
constexpr uint16_t kDecorationLinkageAttributes = 41;
constexpr uint16_t kLinkageTypeExport = 0;
constexpr uint16_t kLinkageTypeImport = 1;

struct Instruction {
  uint16_t opcode = 0;
  size_t offset = 0;
  size_t wordCount = 0;
};

// Walks the module's instructions, skipping the five-word header. An unparseable word count ends
// the walk with an empty result, which the caller treats as a failure.
std::vector<Instruction> Parse(const std::vector<uint32_t>& words) {
  std::vector<Instruction> instructions = {};
  size_t offset = 5;
  while (offset < words.size()) {
    Instruction instruction = {};
    instruction.opcode = static_cast<uint16_t>(words[offset] & 0xFFFFu);
    instruction.wordCount = words[offset] >> 16;
    instruction.offset = offset;
    if (instruction.wordCount == 0 || offset + instruction.wordCount > words.size()) {
      return {};
    }
    instructions.push_back(instruction);
    offset += instruction.wordCount;
  }
  return instructions;
}

// The NUL-terminated literal string starting at an operand word.
std::string LiteralStringAt(const std::vector<uint32_t>& words, size_t operandOffset) {
  std::string text = {};
  for (size_t i = operandOffset; i < words.size(); ++i) {
    for (int shift = 0; shift < 4; ++shift) {
      const char c = static_cast<char>((words[i] >> (shift * 8)) & 0xFFu);
      if (c == '\0') {
        return text;
      }
      text += c;
    }
  }
  return text;
}

std::vector<uint32_t> EncodeStringWords(const std::string& text) {
  std::string padded = text;
  padded.push_back('\0');
  while (padded.size() % 4 != 0) {
    padded.push_back('\0');
  }
  std::vector<uint32_t> words(padded.size() / 4, 0);
  std::memcpy(words.data(), padded.data(), padded.size());
  return words;
}

// Copies one instruction's words into the output; the offsets are size_t and the iterators'
// difference type is signed, so the arithmetic goes through one explicit cast.
void AppendWords(std::vector<uint32_t>* out, const std::vector<uint32_t>& words, size_t offset,
                 size_t count) {
  out->insert(out->end(), words.begin() + static_cast<std::ptrdiff_t>(offset),
              words.begin() + static_cast<std::ptrdiff_t>(offset + count));
}

}  // namespace

std::vector<uint32_t> ExportSPIRVFunctionForLinking(const std::vector<uint32_t>& words,
                                                    const char* functionName, std::string* error) {
  if (error == nullptr || words.size() <= 5) {
    if (error != nullptr) {
      *error = "the module is too small to patch";
    }
    return {};
  }
  const auto instructions = Parse(words);
  if (instructions.empty()) {
    *error = "the module's instructions do not parse";
    return {};
  }
  // The entry point names the function to remove; the export target is the one function the entry
  // calls (the wrapper's dummy call reaches the operator and nothing else — the operator's own
  // helpers are called from inside the operator, not from the entry).
  uint32_t entryId = 0;
  for (const auto& instruction : instructions) {
    if (instruction.opcode == kOpEntryPoint && instruction.wordCount >= 3) {
      entryId = words[instruction.offset + 2];
      break;
    }
  }
  if (entryId == 0) {
    *error = "the module declares no entry point";
    return {};
  }
  size_t entryFunctionOffset = 0;
  size_t entryFunctionEnd = 0;
  std::set<uint32_t> callees = {};
  for (const auto& instruction : instructions) {
    if (instruction.opcode != kOpFunction || instruction.wordCount < 3 ||
        words[instruction.offset + 2] != entryId) {
      continue;
    }
    entryFunctionOffset = instruction.offset;
    for (const auto& rest : instructions) {
      if (rest.offset < entryFunctionOffset) {
        continue;
      }
      if (rest.opcode == kOpFunctionCall && rest.wordCount >= 4) {
        // OpFunctionCall: result type, result id, then the callee.
        callees.insert(words[rest.offset + 3]);
      }
      if (rest.opcode == kOpFunctionEnd) {
        entryFunctionEnd = rest.offset + rest.wordCount;
        break;
      }
    }
    break;
  }
  if (entryFunctionEnd == 0) {
    *error = "the entry point's function body was not found";
    return {};
  }
  callees.erase(entryId);
  if (callees.size() != 1) {
    *error = "the wrapper's entry must call exactly one function to export";
    return {};
  }
  const uint32_t exportId = *callees.begin();

  std::vector<uint32_t> out = {};
  out.reserve(words.size() + 16);
  bool linkageCapabilityAdded = false;
  bool exportDecorationAdded = false;
  for (const auto& instruction : instructions) {
    const bool insideEntry =
        instruction.offset >= entryFunctionOffset && instruction.offset < entryFunctionEnd;
    // The entry point, its execution mode and its body leave; every OpName goes with them — the
    // names are debug-only, and the ones the body defined would dangle (linkage matches on the
    // LinkageAttributes string, never on a name).
    if (insideEntry || instruction.opcode == kOpEntryPoint ||
        instruction.opcode == kOpExecutionMode || instruction.opcode == kOpName ||
        instruction.opcode == kOpMemberName) {
      continue;
    }
    if (instruction.opcode == kOpCapability) {
      AppendWords(&out, words, instruction.offset, instruction.wordCount);
      if (!linkageCapabilityAdded) {
        linkageCapabilityAdded = true;
        // OpCapability carries the capability as its one operand: two words.
        const uint32_t linkage[] = {(2u << 16) | kOpCapability, kCapabilityLinkage};
        out.insert(out.end(), linkage, linkage + 2);
      }
      continue;
    }
    if (!exportDecorationAdded &&
        (instruction.opcode == kOpDecorate || instruction.opcode == kOpTypeVoid)) {
      // The annotation section's start (or the type section, when nothing is decorated): the
      // export decoration joins its own kind there.
      exportDecorationAdded = true;
      const std::vector<uint32_t> nameWords = EncodeStringWords(functionName);
      out.push_back(static_cast<uint32_t>((3 + nameWords.size() + 1) << 16 | kOpDecorate));
      out.push_back(exportId);
      out.push_back(kDecorationLinkageAttributes);
      out.insert(out.end(), nameWords.begin(), nameWords.end());
      out.push_back(kLinkageTypeExport);
    }
    AppendWords(&out, words, instruction.offset, instruction.wordCount);
  }
  if (!exportDecorationAdded) {
    *error = "the module has no annotation or type section to hold the export decoration";
    return {};
  }
  // The header's five words carry the id bound, which the surgery does not change: no id is
  // created, and the removed ids simply fall out of use.
  out.insert(out.begin(), words.begin(), words.begin() + 5);
  return out;
}

std::vector<uint32_t> CompileSPIRVLibraryModule(const std::string& wrapperSource,
                                                const char* functionName, const char* shaderName,
                                                std::string* error) {
  if (error == nullptr) {
    return {};
  }
  const auto compiled =
      CompileGLSL(wrapperSource, ShaderStageType::Fragment, shaderName, 0, false, error);
  if (compiled.empty()) {
    return {};
  }
  std::string patchError;
  auto patched = ExportSPIRVFunctionForLinking(compiled, functionName, &patchError);
  if (patched.empty()) {
    *error = patchError;
    return {};
  }
  return patched;
}

std::vector<uint32_t> ImportSPIRVFunction(const std::vector<uint32_t>& words,
                                          const char* functionName, std::string* error) {
  if (error == nullptr || words.size() <= 5) {
    if (error != nullptr) {
      *error = "the module is too small to patch";
    }
    return {};
  }
  const auto instructions = Parse(words);
  if (instructions.empty()) {
    *error = "the module's instructions do not parse";
    return {};
  }
  // The stub is named by glslang as "chainBody(...)" with its parameter types; the entry stays —
  // this is the frame's shape, where main calls the imported body.
  const std::string mangledPrefix = std::string(functionName) + "(";
  uint32_t targetId = 0;
  for (const auto& instruction : instructions) {
    if (instruction.opcode != kOpName || instruction.wordCount < 3) {
      continue;
    }
    const std::string name = LiteralStringAt(words, instruction.offset + 2);
    if (name == functionName || name.rfind(mangledPrefix, 0) == 0) {
      targetId = words[instruction.offset + 1];
      break;
    }
  }
  if (targetId == 0) {
    *error = "the module does not declare the function to import";
    return {};
  }
  // The stubbed function's extent: header through OpFunctionEnd, its body dropped. The whole
  // declaration (header, parameters, end) is replayed before any definition, because glslang may
  // emit the stub after main and SPIR-V requires declarations first.
  size_t functionStart = 0;
  size_t functionEnd = 0;
  size_t bodyStart = 0;
  for (const auto& instruction : instructions) {
    if (instruction.opcode == kOpFunction && instruction.wordCount >= 3 &&
        words[instruction.offset + 2] == targetId) {
      functionStart = instruction.offset;
      bodyStart = instruction.offset + instruction.wordCount;
      break;
    }
  }
  if (functionStart == 0) {
    *error = "the stub's function was not found";
    return {};
  }
  for (const auto& instruction : instructions) {
    if (instruction.offset < functionStart) {
      continue;
    }
    if (instruction.opcode == kOpFunctionParameter) {
      bodyStart = instruction.offset + instruction.wordCount;
    }
    if (instruction.opcode == kOpFunctionEnd) {
      functionEnd = instruction.offset + instruction.wordCount;
      break;
    }
  }
  if (functionEnd == 0 || bodyStart > functionEnd) {
    *error = "the stub's body extent was not found";
    return {};
  }
  std::vector<uint32_t> declaration(words.begin() + static_cast<std::ptrdiff_t>(functionStart),
                                    words.begin() + static_cast<std::ptrdiff_t>(functionEnd));
  std::vector<uint32_t> out = {};
  out.reserve(words.size());
  bool linkageCapabilityAdded = false;
  bool importDecorationAdded = false;
  bool declarationEmitted = false;
  auto flushDeclaration = [&]() {
    if (!declarationEmitted) {
      out.insert(out.end(), declaration.begin(), declaration.end());
      declarationEmitted = true;
    }
  };
  for (const auto& instruction : instructions) {
    // Drop the stub entirely (its declaration is replayed before the first function).
    if (instruction.offset >= functionStart && instruction.offset < functionEnd) {
      continue;
    }
    if (instruction.opcode == kOpCapability) {
      AppendWords(&out, words, instruction.offset, instruction.wordCount);
      if (!linkageCapabilityAdded) {
        linkageCapabilityAdded = true;
        // OpCapability carries the capability as its one operand: two words.
        const uint32_t linkage[] = {(2u << 16) | kOpCapability, kCapabilityLinkage};
        out.insert(out.end(), linkage, linkage + 2);
      }
      continue;
    }
    if (!importDecorationAdded &&
        (instruction.opcode == kOpDecorate || instruction.opcode == kOpTypeVoid)) {
      importDecorationAdded = true;
      const std::vector<uint32_t> nameWords = EncodeStringWords(functionName);
      out.push_back(static_cast<uint32_t>((3 + nameWords.size() + 1) << 16 | kOpDecorate));
      out.push_back(targetId);
      out.push_back(kDecorationLinkageAttributes);
      out.insert(out.end(), nameWords.begin(), nameWords.end());
      out.push_back(kLinkageTypeImport);
    }
    if (instruction.opcode == kOpFunction) {
      flushDeclaration();
    }
    AppendWords(&out, words, instruction.offset, instruction.wordCount);
  }
  if (!importDecorationAdded) {
    *error = "the module has no annotation or type section to hold the import decoration";
    return {};
  }
  out.insert(out.begin(), words.begin(), words.begin() + 5);
  return out;
}
}  // namespace tgfx

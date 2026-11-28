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
#include "gpu/ShaderBindingLayout.h"

namespace tgfx {

std::string BuildOperatorWrapperSource(const char* symbol, SLType returnType,
                                       const std::vector<OperatorModuleParam>& params,
                                       const std::vector<std::string>& includeSources) {
  std::string text = "#version 450\n";
  bool needsLeafSampler = false;
  bool needsRectSampler = false;
  for (const auto& param : params) {
    if (param.type == SLType::Texture2DSampler) {
      needsLeafSampler = true;
    } else if (param.type == SLType::Texture2DRectSampler) {
      needsRectSampler = true;
    }
  }
  if (needsLeafSampler) {
    text += "layout(set = " + std::to_string(TEXTURE_DESCRIPTOR_SET) + ", binding = " +
            std::to_string(DUMMY_LEAF_SAMPLER_BINDING) + ") uniform sampler2D " +
            DUMMY_LEAF_SAMPLER_NAME + ";\n";
  }
  if (needsRectSampler) {
    text += "layout(set = " + std::to_string(TEXTURE_DESCRIPTOR_SET) + ", binding = " +
            std::to_string(DUMMY_RECT_SAMPLER_BINDING) + ") uniform sampler2DRect " +
            DUMMY_RECT_SAMPLER_NAME + ";\n";
  }
  for (const auto& include : includeSources) {
    text += include;
  }
  text += "\nlayout(location = 0) out vec4 dummyOut;\nvoid main() {\n  dummyOut = ";
  std::string widenPrefix = {};
  std::string widenSuffix = {};
  switch (returnType) {
    case SLType::Float:
      widenPrefix = "vec4(";
      widenSuffix = ")";
      break;
    case SLType::Float2:
      widenPrefix = "vec4(";
      widenSuffix = ", 0.0, 0.0)";
      break;
    case SLType::Float3:
      widenPrefix = "vec4(";
      widenSuffix = ", 0.0)";
      break;
    default:
      break;
  }
  text += widenPrefix;
  text += symbol == nullptr ? "" : symbol;
  text += "(";
  for (size_t argument = 0; argument < params.size(); ++argument) {
    if (argument > 0) {
      text += ", ";
    }
    const auto& param = params[argument];
    if (param.isArray) {
      text += "vec4[" + std::to_string(param.arrayCount) + "](";
      for (uint32_t element = 0; element < param.arrayCount; ++element) {
        if (element > 0) {
          text += ", ";
        }
        text += "vec4(1.0)";
      }
      text += ")";
      continue;
    }
    switch (param.type) {
      case SLType::Float:
        text += "0.0";
        break;
      case SLType::Int:
        text += "0";
        break;
      case SLType::Float2:
        text += "vec2(1.0)";
        break;
      case SLType::Float3:
        text += "vec3(1.0)";
        break;
      case SLType::Float4:
        text += "vec4(1.0)";
        break;
      case SLType::Float3x3:
        text += "mat3(1.0)";
        break;
      case SLType::Float4x4:
        text += "mat4(1.0)";
        break;
      case SLType::Int4:
        text += "ivec4(0)";
        break;
      case SLType::Texture2DSampler:
        text += DUMMY_LEAF_SAMPLER_NAME;
        break;
      case SLType::Texture2DRectSampler:
        text += DUMMY_RECT_SAMPLER_NAME;
        break;
      default:
        break;
    }
  }
  text += ")";
  text += widenSuffix;
  text += ";\n}\n";
  return text;
}
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
constexpr uint16_t kOpVariable = 59;
constexpr uint16_t kOpFunctionParameter = 55;
constexpr uint16_t kOpTypeInt = 21;
constexpr uint16_t kOpTypeFloat = 22;
constexpr uint16_t kOpTypeVector = 23;
constexpr uint16_t kOpTypeMatrix = 24;
constexpr uint16_t kOpTypeImage = 25;
constexpr uint16_t kOpTypeSampledImage = 27;
constexpr uint16_t kOpTypeArray = 28;
constexpr uint16_t kOpTypePointer = 32;
constexpr uint16_t kOpTypeFunction = 33;
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

namespace {

// The dummy entry's sampler exists only so the wrapper compiles; the entry is dropped with the
// export patch and the variable must go with it (a sampler at a fixed binding in every module would
// collide after linking).
bool IsDummySamplerName(const std::string& name) {
  return name == "dummyLeafTex" || name == "dummyDstTex";
}

std::vector<uint32_t> DropDummySamplers(const std::vector<uint32_t>& words, std::string* error) {
  const auto instructions = Parse(words);
  if (instructions.empty()) {
    *error = "the module's instructions do not parse";
    return {};
  }
  std::unordered_map<uint32_t, std::string> names = {};
  for (const auto& instruction : instructions) {
    if (instruction.opcode != kOpName || instruction.wordCount < 3) {
      continue;
    }
    const uint32_t targetId = words[instruction.offset + 1];
    std::string name = {};
    for (size_t word = instruction.offset + 2; word < instruction.offset + instruction.wordCount;
         ++word) {
      const uint32_t value = words[word];
      for (int byte = 0; byte < 4; ++byte) {
        const char c = static_cast<char>((value >> (byte * 8)) & 0xFFu);
        if (c == '\0') {
          break;
        }
        name.push_back(c);
      }
    }
    names[targetId] = name;
  }
  std::set<uint32_t> drop = {};
  for (const auto& pair : names) {
    if (IsDummySamplerName(pair.second)) {
      drop.insert(pair.first);
    }
  }
  if (drop.empty()) {
    return words;
  }
  std::vector<uint32_t> out = {};
  out.reserve(words.size());
  out.insert(out.end(), words.begin(), words.begin() + 5);
  for (const auto& instruction : instructions) {
    const uint32_t firstOperand = instruction.wordCount >= 2 ? words[instruction.offset + 1] : 0;
    if (instruction.opcode == kOpVariable) {
      const uint32_t resultId = instruction.wordCount >= 3 ? words[instruction.offset + 2] : 0;
      if (drop.count(resultId) != 0) {
        continue;
      }
    }
    if ((instruction.opcode == kOpDecorate || instruction.opcode == kOpName) &&
        drop.count(firstOperand) != 0) {
      continue;
    }
    AppendWords(&out, words, instruction.offset, instruction.wordCount);
  }
  return out;
}

}  // namespace

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
  std::string dummyError;
  auto withoutDummy = DropDummySamplers(compiled, &dummyError);
  if (withoutDummy.empty()) {
    *error = dummyError;
    return {};
  }
  std::string patchError;
  auto patched = ExportSPIRVFunctionForLinking(withoutDummy, functionName, &patchError);
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

std::vector<OperatorABIType> ReflectFunctionSignature(const std::vector<uint32_t>& words,
                                                     const char* functionName, std::string* error) {
  if (error == nullptr) {
    return {};
  }
  const auto instructions = Parse(words);
  if (instructions.empty()) {
    *error = "the module's instructions do not parse";
    return {};
  }
  struct TypeInfo {
    uint16_t opcode = 0;
    std::vector<uint32_t> operands;
  };
  std::unordered_map<uint32_t, TypeInfo> types;
  uint32_t targetId = 0;
  for (const auto& instruction : instructions) {
    const uint16_t opcode = instruction.opcode;
    if (opcode == kOpTypeInt || opcode == kOpTypeFloat || opcode == kOpTypeVector ||
        opcode == kOpTypeMatrix || opcode == kOpTypeImage || opcode == kOpTypeSampledImage ||
        opcode == kOpTypeArray || opcode == kOpTypePointer || opcode == kOpTypeFunction) {
      TypeInfo info = {};
      info.opcode = opcode;
      for (uint32_t word = 1; word < instruction.wordCount; ++word) {
        info.operands.push_back(words[instruction.offset + word]);
      }
      types[info.operands[0]] = std::move(info);
    } else if (opcode == kOpDecorate && instruction.wordCount >= 4 &&
               words[instruction.offset + 2] == kDecorationLinkageAttributes) {
      if (LiteralStringAt(words, instruction.offset + 3) == functionName) {
        targetId = words[instruction.offset + 1];
      }
    }
  }
  if (targetId == 0) {
    *error = "the module does not export the function";
    return {};
  }
  uint32_t functionTypeId = 0;
  for (const auto& instruction : instructions) {
    if (instruction.opcode == kOpFunction && instruction.wordCount >= 5 &&
        words[instruction.offset + 2] == targetId) {
      functionTypeId = words[instruction.offset + 4];
    }
  }
  const auto functionType = types.find(functionTypeId);
  if (functionType == types.end() || functionType->second.opcode != kOpTypeFunction) {
    *error = "the exported function's type is missing";
    return {};
  }
  std::vector<OperatorABIType> reflected = {};
  // operands[0] is the type's own id, [1] the return type, the rest the parameters (pointers).
  for (size_t index = 2; index < functionType->second.operands.size(); ++index) {
    uint32_t typeId = functionType->second.operands[index];
    const auto pointer = types.find(typeId);
    if (pointer != types.end() && pointer->second.opcode == kOpTypePointer) {
      typeId = pointer->second.operands[2];
    }
    const auto type = types.find(typeId);
    if (type == types.end()) {
      *error = "a parameter type is not declared";
      return {};
    }
    switch (type->second.opcode) {
      case kOpTypeFloat:
        reflected.push_back(OperatorABIType::Float);
        break;
      case kOpTypeInt:
        reflected.push_back(OperatorABIType::Int);
        break;
      case kOpTypeVector: {
        const uint32_t component = type->second.operands[1];
        const uint32_t count = type->second.operands[2];
        const auto componentType = types.find(component);
        const bool isFloat =
            componentType != types.end() && componentType->second.opcode == kOpTypeFloat;
        if (!isFloat) {
          if (count != 4) {
            *error = "an unsupported integer vector parameter";
            return {};
          }
          reflected.push_back(OperatorABIType::IVec4);
          break;
        }
        if (count == 2) {
          reflected.push_back(OperatorABIType::Vec2);
        } else if (count == 3) {
          reflected.push_back(OperatorABIType::Vec3);
        } else if (count == 4) {
          reflected.push_back(OperatorABIType::Vec4);
        } else {
          *error = "an unsupported vector parameter";
          return {};
        }
        break;
      }
      case kOpTypeArray: {
        // An array of float vec4s is the table's Vec4Array8 (the only array parameter form).
        const uint32_t elementTypeId = type->second.operands[1];
        const auto elementType = types.find(elementTypeId);
        if (elementType != types.end() && elementType->second.opcode == kOpTypeVector) {
          const uint32_t component = elementType->second.operands[1];
          const uint32_t componentCount = elementType->second.operands[2];
          const auto componentType = types.find(component);
          const bool isFloat =
              componentType != types.end() && componentType->second.opcode == kOpTypeFloat;
          if (isFloat && componentCount == 4) {
            reflected.push_back(OperatorABIType::Vec4Array8);
            break;
          }
        }
        *error = "an unsupported array parameter";
        return {};
      }
      case kOpTypeMatrix: {
        const uint32_t column = type->second.operands[1];
        const uint32_t count = type->second.operands[2];
        if (count == 3) {
          reflected.push_back(OperatorABIType::Mat3);
        } else if (count == 4) {
          reflected.push_back(OperatorABIType::Mat4);
        } else {
          *error = "an unsupported matrix parameter";
          return {};
        }
        (void)column;
        break;
      }
      case kOpTypeSampledImage:
      case kOpTypeImage:
        reflected.push_back(OperatorABIType::SampledImage);
        break;
      default:
        *error = "an unsupported parameter type";
        return {};
    }
  }
  return reflected;
}

}  // namespace tgfx

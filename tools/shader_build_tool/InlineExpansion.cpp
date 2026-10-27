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

#include "InlineExpansion.h"
#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include "core/DiagnosticCodes.h"

namespace tgfx {

namespace {
constexpr uint32_t SpirvMagic = 0x07230203;
constexpr size_t SpirvHeaderWords = 5;
constexpr uint32_t OpName = 5;
constexpr uint32_t OpEntryPoint = 15;
constexpr uint32_t OpFunction = 54;
constexpr uint32_t OpFunctionEnd = 56;
constexpr uint32_t OpFunctionCall = 57;

// The duplicated instructions of a function are its own instructions times its copies beyond the
// first. The limit is on that product, not on the body size: a 135-instruction helper called from
// eight sites (the former per-mode HSL blend helpers, 945 duplicated instructions) costs the
// driver as much as a large body called twice. Small pickers duplicated a few times stay below.
// There are no exceptions: every heavy function the kernels share has a single call site.
constexpr size_t MaxDuplicatedInstructions = 400;

size_t DuplicatedInstructions(const InlinedFunction& function) {
  return function.copies > 1 ? function.instructions * (function.copies - 1) : 0;
}

std::string DecodeLiteralString(const std::vector<uint32_t>& spirv, size_t firstWord,
                                size_t endWord) {
  std::string text;
  for (size_t word = firstWord; word < endWord; ++word) {
    for (int byte = 0; byte < 4; ++byte) {
      auto character = static_cast<char>((spirv[word] >> (8 * byte)) & 0xFF);
      if (character == '\0') {
        return text;
      }
      text.push_back(character);
    }
  }
  return text;
}

// glslang names functions "<name>(<parameter mangling>"; keep the source-level name.
std::string SourceName(const std::string& mangled) {
  auto paren = mangled.find('(');
  return paren == std::string::npos ? mangled : mangled.substr(0, paren);
}
}  // namespace

InlineExpansion AnalyzeInlineExpansion(const std::vector<uint32_t>& spirv) {
  InlineExpansion result = {};
  if (spirv.size() < SpirvHeaderWords || spirv[0] != SpirvMagic) {
    return result;
  }
  std::unordered_map<uint32_t, std::string> names = {};
  std::vector<uint32_t> entryPoints = {};
  struct FunctionBody {
    size_t instructions = 0;
    std::map<uint32_t, size_t> calls = {};
  };
  std::map<uint32_t, FunctionBody> functions = {};
  uint32_t current = 0;
  bool inFunction = false;
  size_t word = SpirvHeaderWords;
  while (word < spirv.size()) {
    const uint32_t opcode = spirv[word] & 0xFFFF;
    const size_t count = spirv[word] >> 16;
    if (count == 0 || word + count > spirv.size()) {
      return result;
    }
    if (opcode == OpName && count >= 3) {
      names[spirv[word + 1]] = DecodeLiteralString(spirv, word + 2, word + count);
    } else if (opcode == OpEntryPoint && count >= 3) {
      entryPoints.push_back(spirv[word + 2]);
    } else if (opcode == OpFunction && count >= 3) {
      if (inFunction) {
        return result;
      }
      current = spirv[word + 2];
      inFunction = true;
      functions[current] = {};
    } else if (opcode == OpFunctionEnd) {
      if (!inFunction) {
        return result;
      }
      inFunction = false;
    } else if (inFunction) {
      auto& body = functions[current];
      ++body.instructions;
      if (opcode == OpFunctionCall && count >= 4) {
        ++body.calls[spirv[word + 3]];
      }
    }
    word += count;
  }
  if (inFunction || entryPoints.size() != 1 || functions.count(entryPoints[0]) == 0) {
    return result;
  }
  // GLSL forbids recursion, so the call graph is a DAG: the copies of a function are the sum over
  // its call sites of the caller's copies. A reverse post-order from the entry point visits every
  // caller before its callees.
  std::vector<uint32_t> order = {};
  std::set<uint32_t> visited = {};
  std::set<uint32_t> onStack = {};
  bool cyclic = false;
  std::function<void(uint32_t)> visit = [&](uint32_t function) {
    if (onStack.count(function) != 0) {
      cyclic = true;
      return;
    }
    if (!visited.insert(function).second) {
      return;
    }
    onStack.insert(function);
    auto found = functions.find(function);
    if (found != functions.end()) {
      for (const auto& call : found->second.calls) {
        visit(call.first);
      }
    }
    onStack.erase(function);
    order.push_back(function);
  };
  visit(entryPoints[0]);
  if (cyclic) {
    return result;
  }
  std::reverse(order.begin(), order.end());
  std::map<uint32_t, size_t> copies = {};
  copies[entryPoints[0]] = 1;
  for (auto function : order) {
    auto found = functions.find(function);
    if (found == functions.end()) {
      return result;
    }
    for (const auto& call : found->second.calls) {
      copies[call.first] += copies[function] * call.second;
    }
  }
  for (auto function : order) {
    InlinedFunction entry = {};
    auto name = names.find(function);
    entry.name = name != names.end() ? SourceName(name->second) : "%" + std::to_string(function);
    entry.instructions = functions[function].instructions;
    entry.copies = copies[function];
    result.ownInstructions += entry.instructions;
    result.expandedInstructions += entry.instructions * entry.copies;
    result.functions.push_back(std::move(entry));
  }
  result.valid = true;
  return result;
}

size_t ValidateInlineExpansion(const std::vector<InlineExpansionRecord>& records,
                               Diagnostics* diagnostics) {
  // One record per (shader, function) with the worst variant, instead of one per variant.
  struct Violation {
    size_t copies = 0;
    size_t instructions = 0;
    size_t duplicated = 0;
    size_t variants = 0;
    uint32_t worstVariant = 0;
  };
  std::map<std::pair<std::string, std::string>, Violation> violations = {};
  size_t invalid = 0;
  for (const auto& record : records) {
    if (!record.expansion.valid) {
      // Not a policy violation: the module itself could not be analyzed, so the check could not
      // run. Reported as unverifiable rather than folded into the policy count.
      Diagnostic diagnostic;
      diagnostic.severity = Severity::Unverifiable;
      diagnostic.layer = Layer::Verify;
      diagnostic.code = DiagnosticCode::VerifyNotApplicable;
      diagnostic.shader = record.shaderName;
      diagnostic.fragPermutationIndex = static_cast<int>(record.fragPermutationIndex);
      diagnostic.message = "the SPIR-V module could not be analyzed";
      diagnostics->report(std::move(diagnostic));
      ++invalid;
      continue;
    }
    for (const auto& function : record.expansion.functions) {
      auto duplicated = DuplicatedInstructions(function);
      if (duplicated < MaxDuplicatedInstructions) {
        continue;
      }
      auto& violation = violations[{record.shaderName, function.name}];
      ++violation.variants;
      if (duplicated > violation.duplicated) {
        violation.copies = function.copies;
        violation.instructions = function.instructions;
        violation.duplicated = duplicated;
        violation.worstVariant = record.fragPermutationIndex;
      }
    }
  }
  for (const auto& entry : violations) {
    const auto& violation = entry.second;
    Diagnostic diagnostic;
    diagnostic.severity = Severity::Error;
    diagnostic.layer = Layer::Verify;
    diagnostic.code = DiagnosticCode::VerifyInlinePolicy;
    diagnostic.shader = entry.first.first;
    diagnostic.fragPermutationIndex = static_cast<int>(violation.worstVariant);
    diagnostic.message =
        "'" + entry.first.second + "' (" + std::to_string(violation.instructions) +
        " instructions) reaches the driver " + std::to_string(violation.copies) +
        " times in frag " + std::to_string(violation.worstVariant) + " (" +
        std::to_string(violation.variants) + " variant(s)), " +
        std::to_string(violation.duplicated) + " duplicated instructions; the limit is " +
        std::to_string(MaxDuplicatedInstructions) +
        ". Funnel the call sites into one: select the operands, then call once (or run the call "
        "in a loop whose pass count is uniform data).";
    diagnostics->report(std::move(diagnostic));
  }
  return invalid + violations.size();
}

bool WriteInlineExpansionReport(const std::string& path,
                                const std::vector<InlineExpansionRecord>& records,
                                Diagnostics* diagnostics) {
  std::ofstream file(path);
  if (!file) {
    Diagnostic diagnostic;
    diagnostic.severity = Severity::Error;
    diagnostic.layer = Layer::Output;
    diagnostic.code = DiagnosticCode::OutputWrite;
    diagnostic.message = "cannot write the inline expansion report: " + path;
    diagnostics->report(std::move(diagnostic));
    return false;
  }
  file << "{\n  \"maxDuplicatedInstructions\": " << MaxDuplicatedInstructions
       << ",\n  \"variants\": [";
  bool firstRecord = true;
  for (const auto& record : records) {
    file << (firstRecord ? "\n" : ",\n");
    firstRecord = false;
    const auto& expansion = record.expansion;
    file << "    {\"shader\": \"" << record.shaderName
         << "\", \"frag\": " << record.fragPermutationIndex
         << ", \"valid\": " << (expansion.valid ? "true" : "false")
         << ", \"ownInstructions\": " << expansion.ownInstructions
         << ", \"expandedInstructions\": " << expansion.expandedInstructions
         << ", \"duplicated\": [";
    bool firstFunction = true;
    for (const auto& function : expansion.functions) {
      if (function.copies <= 1) {
        continue;
      }
      file << (firstFunction ? "" : ", ") << "{\"name\": \"" << function.name
           << "\", \"instructions\": " << function.instructions
           << ", \"copies\": " << function.copies
           << ", \"duplicatedInstructions\": " << DuplicatedInstructions(function) << "}";
      firstFunction = false;
    }
    file << "]}";
  }
  file << "\n  ]\n}\n";
  return static_cast<bool>(file);
}

}  // namespace tgfx

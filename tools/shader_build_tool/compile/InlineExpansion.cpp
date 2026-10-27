#include "compile/InlineExpansion.h"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tgfx {

namespace {
constexpr uint32_t SpirvMagic = 0x07230203;
constexpr size_t SpirvHeaderWords = 5;
constexpr uint32_t OpName = 5;
constexpr uint32_t OpEntryPoint = 15;
constexpr uint32_t OpFunction = 54;
constexpr uint32_t OpFunctionEnd = 56;
constexpr uint32_t OpFunctionCall = 57;

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

}  // namespace tgfx

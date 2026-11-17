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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/spec/SpecGatePruner.h"
#include <cctype>
#include <cstring>
#include <unordered_map>

namespace tgfx {

namespace {

constexpr const char* kGateMacroPrefix = "TGFX_GATE_";

/// The trimmed view of one line, without its newline: what the directive tests read.
std::string TrimmedLine(const char* begin, const char* end) {
  while (begin < end && (*begin == ' ' || *begin == '\t')) {
    ++begin;
  }
  while (end > begin && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
    --end;
  }
  return std::string(begin, end);
}

bool StartsWith(const std::string& text, const char* prefix) {
  const auto length = std::strlen(prefix);
  return text.size() >= length && text.compare(0, length, prefix) == 0;
}

/// One gate's definition line:
///   #define TGFX_GATE_<name> (<prefix>0 == 0 || ((<prefix><id> & <mask>) != 0))
/// The build normalizes the condition to exactly this shape, so the parse is a fixed-token walk
/// instead of an expression evaluator.
bool ParseGateDefine(const std::string& line, const char* valuePrefix, std::string* name,
                     uint32_t* id, uint32_t* mask) {
  const std::string definePrefix = std::string("#define ") + kGateMacroPrefix;
  if (!StartsWith(line, definePrefix.c_str())) {
    return false;
  }
  size_t at = definePrefix.size();
  const auto nameStart = at;
  while (at < line.size() &&
         (std::isalnum(static_cast<unsigned char>(line[at])) != 0 || line[at] == '_')) {
    ++at;
  }
  if (at == nameStart) {
    return false;
  }
  *name = line.substr(nameStart, at - nameStart);
  auto skipSpaces = [&line, &at]() {
    while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) {
      ++at;
    }
  };
  auto expect = [&line, &at, &skipSpaces](const char* token) {
    skipSpaces();
    const auto length = std::strlen(token);
    if (line.compare(at, length, token) != 0) {
      return false;
    }
    at += length;
    return true;
  };
  auto parseNumber = [&line, &at, &skipSpaces](uint32_t* value) {
    skipSpaces();
    const auto start = at;
    uint64_t result = 0;
    while (at < line.size() && std::isdigit(static_cast<unsigned char>(line[at])) != 0) {
      result = result * 10 + static_cast<uint64_t>(line[at] - '0');
      if (result > 0xFFFFFFFFull) {
        return false;
      }
      ++at;
    }
    if (at == start) {
      return false;
    }
    *value = static_cast<uint32_t>(result);
    return true;
  };
  // The master switch's own define, then the condition's field id and the mask.
  if (!expect("(") || !expect(valuePrefix) || !expect("0") || !expect("==") || !expect("0")) {
    return false;
  }
  if (!expect("||") || !expect("((") || !expect(valuePrefix)) {
    return false;
  }
  if (!parseNumber(id) || !expect("&") || !parseNumber(mask)) {
    return false;
  }
  if (!expect(")") || !expect("!=") || !expect("0") || !expect(")") || !expect(")")) {
    return false;
  }
  skipSpaces();
  return at == line.size();
}

}  // namespace

std::string ResolveSpecGates(const std::string& text, const std::vector<uint32_t>& valuesById,
                             const char* valuePrefix) {
  std::unordered_map<std::string, bool> gateOpen;
  std::string output;
  output.reserve(text.size());
  bool inGate = false;
  bool keepBody = false;
  bool sawElse = false;
  size_t cursor = 0;
  while (cursor < text.size()) {
    const auto lineEnd = text.find('\n', cursor);
    const auto end = lineEnd == std::string::npos ? text.size() : lineEnd + 1;
    const char* begin = text.data() + cursor;
    const char* stop = text.data() + (lineEnd == std::string::npos ? text.size() : lineEnd);
    const auto line = TrimmedLine(begin, stop);
    const std::string definePrefix = std::string("#define ") + kGateMacroPrefix;
    if (StartsWith(line, definePrefix.c_str())) {
      std::string name;
      uint32_t id = 0;
      uint32_t mask = 0;
      if (!ParseGateDefine(line, valuePrefix, &name, &id, &mask) || id >= valuesById.size()) {
        return text;
      }
      gateOpen[name] = (valuesById[id] & mask) != 0;
      cursor = end;
      continue;
    }
    const std::string ifPrefix = std::string("#if ") + kGateMacroPrefix;
    if (!inGate && StartsWith(line, ifPrefix.c_str())) {
      const auto name = line.substr(ifPrefix.size());
      const auto found = gateOpen.find(name);
      if (found == gateOpen.end()) {
        return text;
      }
      inGate = true;
      keepBody = found->second;
      sawElse = false;
      cursor = end;
      continue;
    }
    if (inGate) {
      if (line == "#else") {
        if (sawElse) {
          return text;
        }
        sawElse = true;
        cursor = end;
        continue;
      }
      if (line == "#endif") {
        if (!sawElse) {
          return text;
        }
        inGate = false;
        cursor = end;
        continue;
      }
      if (!line.empty() && line[0] == '#') {
        return text;
      }
      if ((keepBody && !sawElse) || (!keepBody && sawElse)) {
        output.append(text, cursor, end - cursor);
      }
      cursor = end;
      continue;
    }
    output.append(text, cursor, end - cursor);
    cursor = end;
  }
  if (inGate) {
    return text;
  }
  return output;
}

std::string RemoveUncalledFunctions(const std::string& text) {
  struct Span {
    size_t begin;
    size_t end;
  };
  std::string current = text;
  for (int pass = 0; pass < 4; ++pass) {
    std::vector<std::pair<size_t, std::string>> definitions = {};
    for (size_t lineStart = 0; lineStart < current.size();) {
      const auto lineEnd = current.find('\n', lineStart);
      const auto stop = lineEnd == std::string::npos ? current.size() : lineEnd;
      const auto line = current.substr(lineStart, stop - lineStart);
      const auto brace = line.find('{');
      const auto paren = line.find('(');
      const bool candidate = !line.empty() && line[0] != '#' && line[0] != '/' &&
                             std::isalpha(static_cast<unsigned char>(line[0])) != 0 &&
                             paren != std::string::npos && brace != std::string::npos;
      if (candidate) {
        const auto space = line.find(' ');
        const auto nameStart =
            space == std::string::npos ? std::string::npos : line.find_first_not_of(' ', space);
        if (nameStart != std::string::npos && nameStart < paren) {
          const auto name = line.substr(nameStart, paren - nameStart);
          const bool identifier =
              !name.empty() &&
              (std::isalpha(static_cast<unsigned char>(name[0])) != 0 || name[0] == '_') &&
              name.find_first_not_of(
                  "abcdefghijklmnopqrstuvwxyz"
                  "ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789") == std::string::npos;
          if (identifier && name != "main") {
            definitions.push_back({lineStart, name});
          }
        }
      }
      if (lineEnd == std::string::npos) {
        break;
      }
      lineStart = lineEnd + 1;
    }
    if (definitions.empty()) {
      break;
    }
    std::vector<Span> dead = {};
    // One pass collects every identifier followed by '(' — its call count. Checking each
    // definition against this map replaces a full rescan of the text per definition (O(functions
    // * bytes)); on a 30KB chain program that was 0.50ms per program creation, and this is
    // 0.18ms (-64%, byte-identical output), so the pass costs ~2% of the compile it shortens.
    std::unordered_map<std::string, size_t> callCounts = {};
    for (size_t i = 0; i < current.size(); ++i) {
      const char c = current[i];
      if (std::isalpha(static_cast<unsigned char>(c)) == 0 && c != '_') {
        continue;
      }
      const auto start = i;
      while (i < current.size() &&
             (std::isalnum(static_cast<unsigned char>(current[i])) != 0 || current[i] == '_')) {
        ++i;
      }
      size_t after = i;
      while (after < current.size() && current[after] == ' ') {
        ++after;
      }
      if (after < current.size() && current[after] == '(') {
        ++callCounts[current.substr(start, i - start)];
      }
    }
    for (const auto& definition : definitions) {
      const auto& name = definition.second;
      if (callCounts[name] > 1) {
        continue;  // the definition plus at least one call site
      }
      const auto open = current.find('{', definition.first);
      if (open == std::string::npos) {
        return text;
      }
      int depth = 0;
      size_t cursor = open;
      for (; cursor < current.size(); ++cursor) {
        if (current[cursor] == '{') {
          ++depth;
        } else if (current[cursor] == '}' && --depth == 0) {
          break;
        }
      }
      if (depth != 0 || cursor >= current.size()) {
        return text;  // unbalanced: leave the text alone rather than cutting into it
      }
      dead.push_back({definition.first, cursor + 1});
    }
    if (dead.empty()) {
      break;
    }
    for (auto it = dead.rbegin(); it != dead.rend(); ++it) {
      current.erase(it->begin, it->end - it->begin);
    }
  }
  return current;
}

std::string ResolveSpecBranches(const std::string& text, const std::vector<uint32_t>& valuesById) {
  static const char* kPrefix = "#pragma tgfx_spec_branch ";
  if (text.find(kPrefix) == std::string::npos) {
    return text;
  }
  std::string output;
  output.reserve(text.size());
  size_t at = 0;
  while (at < text.size()) {
    const auto lineEnd = text.find('\n', at);
    const auto stop = lineEnd == std::string::npos ? text.size() : lineEnd;
    const auto line = text.substr(at, stop - at);
    const auto pragmaAt = line.find_first_not_of(" \t");
    if (pragmaAt == std::string::npos || line.compare(pragmaAt, strlen(kPrefix), kPrefix) != 0) {
      output.append(line);
      output += '\n';
      at = stop + 1;
      continue;
    }
    uint32_t id = 0;
    uint32_t mask = 0;
    if (sscanf(line.c_str() + pragmaAt + strlen(kPrefix), "%u %u", &id, &mask) != 2) {
      at = stop + 1;  // unparseable: drop the pragma, keep the block below it
      continue;
    }
    at = stop + 1;
    if (id >= valuesById.size()) {
      continue;  // an id this draw does not carry: drop the pragma, keep the block
    }
    if ((valuesById[id] & mask) != 0) {
      continue;
    }
    const auto open = text.find('{', at);
    if (open == std::string::npos) {
      break;
    }
    int depth = 0;
    auto cursor = open;
    for (; cursor < text.size(); ++cursor) {
      if (text[cursor] == '{') {
        ++depth;
      } else if (text[cursor] == '}' && --depth == 0) {
        break;
      }
    }
    if (depth != 0 || cursor >= text.size()) {
      return text;  // unbalanced: leave the whole text alone
    }
    at = text.find('\n', cursor);
    at = at == std::string::npos ? text.size() : at + 1;
  }
  return output;
}

}  // namespace tgfx

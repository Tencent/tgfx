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

}  // namespace tgfx

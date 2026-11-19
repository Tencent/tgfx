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
//  either express or implied. See the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "cli/BuildCommand.h"

namespace tgfx {
namespace {

struct CatalogEntry {
  std::string name = {};
  std::string source = {};
  std::vector<std::string>
  requires = {};
  std::string memberPrefix = {};
  size_t memberStart = 2;
  std::vector<std::pair<std::string, std::string>> members = {};     // param -> member
  std::vector<std::pair<std::string, std::string>> sources = {};     // param -> enum override
  std::vector<std::pair<std::string, std::string>> conditions = {};  // param -> guard
};

// Drops comments and blank lines from a catalog function (the header ships into the binary).
std::string CompactFunctionSource(const std::string& text) {
  std::string out = {};
  out.reserve(text.size());
  bool inBlock = false;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    const char next = i + 1 < text.size() ? text[i + 1] : '\0';
    if (inBlock) {
      if (c == '*' && next == '/') {
        inBlock = false;
        ++i;
      }
      continue;
    }
    if (c == '/' && next == '/') {
      while (i + 1 < text.size() && text[i] != '\n') {
        ++i;
      }
      continue;
    }
    if (c == '/' && next == '*') {
      inBlock = true;
      ++i;
      continue;
    }
    out.push_back(c);
  }
  std::string compact = {};
  compact.reserve(out.size());
  size_t lineStart = 0;
  while (lineStart <= out.size()) {
    size_t lineEnd = out.find('\n', lineStart);
    if (lineEnd == std::string::npos) {
      lineEnd = out.size();
    }
    const std::string line = out.substr(lineStart, lineEnd - lineStart);
    if (line.find_first_not_of(" \t\r") != std::string::npos) {
      compact += line;
      compact.push_back('\n');
    }
    if (lineEnd == out.size()) {
      break;
    }
    lineStart = lineEnd + 1;
  }
  return compact;
}

bool ParseAnnotatedFunction(const std::string& text, CatalogEntry* entry, std::string* error) {
  std::string source = {};
  size_t lineStart = 0;
  while (lineStart <= text.size()) {
    size_t lineEnd = text.find('\n', lineStart);
    if (lineEnd == std::string::npos) {
      lineEnd = text.size();
    }
    std::string line = text.substr(lineStart, lineEnd - lineStart);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const std::string functionTag = "//@function ";
    const std::string membersTag = "//@members ";
    const std::string sourceTag = "//@source ";
    const std::string whenTag = "//@when ";
    const std::string memberTag = "//@member ";
    const std::string memberStartTag = "//@member-start ";
    const std::string requiresTag = "//@requires ";
    if (line.rfind(functionTag, 0) == 0) {
      if (!entry->name.empty()) {
        *error = "a file declares more than one //@function";
        return false;
      }
      entry->name = line.substr(functionTag.size());
    } else if (line.rfind(requiresTag, 0) == 0) {
      *error = "//@requires is retired: the scan infers a function's dependencies from its body";
      return false;
    } else if (line.rfind(membersTag, 0) == 0) {
      entry->memberPrefix = line.substr(membersTag.size());
    } else if (line.rfind(sourceTag, 0) == 0 || line.rfind(whenTag, 0) == 0) {
      const bool isSource = line.rfind(sourceTag, 0) == 0;
      const std::string rest = line.substr(isSource ? sourceTag.size() : whenTag.size());
      const auto space = rest.find(' ');
      if (space != std::string::npos) {
        auto& target = isSource ? entry->sources : entry->conditions;
        target.emplace_back(rest.substr(0, space), rest.substr(space + 1));
      }
    } else if (line.rfind(memberStartTag, 0) == 0) {
      entry->memberStart =
          static_cast<size_t>(std::atoi(line.substr(memberStartTag.size()).c_str()));
    } else if (line.rfind(memberTag, 0) == 0) {
      const std::string rest = line.substr(memberTag.size());
      const auto space = rest.find(' ');
      if (space != std::string::npos) {
        entry->members.emplace_back(rest.substr(0, space), rest.substr(space + 1));
      }
    } else {
      source += line;
      source.push_back('\n');
    }
    if (lineEnd == text.size()) {
      break;
    }
    lineStart = lineEnd + 1;
  }
  if (entry->name.empty()) {
    *error = "no //@function annotation";
    return false;
  }
  entry->source = CompactFunctionSource(source);
  if (entry->source.empty()) {
    *error = "the function body is empty";
    return false;
  }
  return true;
}

// The parameters of the function's signature, in order, as (type, name).
std::vector<std::pair<std::string, std::string>> SignatureParams(const std::string& source,
                                                                 const std::string& name) {
  std::vector<std::pair<std::string, std::string>> params = {};
  const auto nameAt = source.find(name + "(");
  if (nameAt == std::string::npos) {
    return params;
  }
  const auto open = source.find('(', nameAt);
  const auto close = source.find(')', open);
  if (open == std::string::npos || close == std::string::npos) {
    return params;
  }
  std::string list = source.substr(open + 1, close - open - 1);
  size_t start = 0;
  while (start <= list.size()) {
    size_t end = list.find(',', start);
    if (end == std::string::npos) {
      end = list.size();
    }
    std::string item = list.substr(start, end - start);
    const auto first = item.find_first_not_of(" \t\n");
    const auto last = item.find_last_not_of(" \t\n");
    if (first != std::string::npos) {
      item = item.substr(first, last - first + 1);
      const auto space = item.find_last_of(" \t\n");
      if (space != std::string::npos) {
        params.emplace_back(item.substr(0, space), item.substr(space + 1));
      }
    }
    if (end == list.size()) {
      break;
    }
    start = end + 1;
  }
  return params;
}

const char* FragmentTypeOf(const std::string& glslType) {
  if (glslType == "int") {
    return "Int";
  }
  if (glslType == "float") {
    return "Float";
  }
  if (glslType == "vec2") {
    return "Float2";
  }
  if (glslType == "vec3") {
    return "Float3";
  }
  if (glslType == "vec4") {
    return "Float4";
  }
  if (glslType == "ivec4") {
    return "Int4";
  }
  if (glslType == "mat3") {
    return "Matrix3";
  }
  return nullptr;
}

// The member's binding source, derived from its name: Gradient_leftBorder -> GradientLeftBorder.
std::string BindingSourceOf(const std::string& member, const std::string& prefix) {
  std::string head = prefix;
  if (!head.empty() && head.back() == '_') {
    head.pop_back();
  }
  std::string tail = member;
  if (tail.rfind(prefix, 0) == 0) {
    // The separator form: Gradient_leftBorder -> LeftBorder -> GradientLeftBorder.
    tail = tail.substr(prefix.size());
  } else if (!head.empty() && member.rfind(head, 0) == 0) {
    return member;  // an explicit name already carries the prefix
  }
  if (!tail.empty()) {
    tail[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(tail[0])));
  }
  return head + tail;
}

}  // namespace

bool WriteFunctionCatalog(const std::string& shaderDir, const std::string& outPath,
                          std::string* error) {
  namespace fs = std::filesystem;
  if (error == nullptr) {
    return false;
  }
  const fs::path root = fs::path(shaderDir) / "fn";
  if (!fs::exists(root)) {
    *error = "no fn directory under " + shaderDir;
    return false;
  }
  std::vector<CatalogEntry> entries = {};
  for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator();
       ++it) {
    if (!it->is_regular_file()) {
      continue;
    }
    std::ifstream file(it->path(), std::ios::binary);
    if (!file.is_open()) {
      *error = "cannot read " + it->path().string();
      return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    CatalogEntry entry = {};
    std::string parseError;
    if (!ParseAnnotatedFunction(buffer.str(), &entry, &parseError)) {
      *error = it->path().string() + ": " + parseError;
      return false;
    }
    for (const auto& existing : entries) {
      if (existing.name == entry.name) {
        *error = "duplicate function " + entry.name + " (" + it->path().string() + ")";
        return false;
      }
    }
    entries.push_back(std::move(entry));
  }
  if (entries.empty()) {
    *error = "the fn directory declares no functions";
    return false;
  }
  // Dependencies come from the bodies: every identifier followed by '(' that names another
  // catalog function is a call. Declared edges cannot drift because there are none to declare.
  std::set<std::string> names = {};
  for (const auto& entry : entries) {
    names.insert(entry.name);
  }
  for (auto& entry : entries) {
    std::set<std::string> called = {};
    const std::string& body = entry.source;
    for (size_t index = 0; index < body.size();) {
      const unsigned char c = static_cast<unsigned char>(body[index]);
      if (std::isalpha(c) || c == '_') {
        size_t end = index;
        while (end < body.size() &&
               (std::isalnum(static_cast<unsigned char>(body[end])) || body[end] == '_')) {
          ++end;
        }
        const std::string token = body.substr(index, end - index);
        size_t next = end;
        while (next < body.size() && (body[next] == ' ' || body[next] == '\t')) {
          ++next;
        }
        if (next < body.size() && body[next] == '(' && token != entry.name &&
            names.count(token) != 0) {
          called.insert(token);
        }
        index = end;
      } else {
        ++index;
      }
    }
    entry.requires.assign(called.begin(), called.end());
  }
  std::vector<int> state(entries.size(), 0);  // -1 visiting, 1 done
  std::function<bool(size_t)> visit = [&](size_t index) {
    if (state[index] == 1) {
      return true;
    }
    if (state[index] == -1) {
      *error = "requirement cycle through " + entries[index].name;
      return false;
    }
    state[index] = -1;
    for (const auto& required : entries[index].requires) {
      for (size_t other = 0; other < entries.size(); ++other) {
        if (entries[other].name == required && !visit(other)) {
          return false;
        }
      }
    }
    state[index] = 1;
    return true;
  };
  for (size_t index = 0; index < entries.size(); ++index) {
    if (!visit(index)) {
      return false;
    }
  }
  std::ostringstream header;
  header << "// Generated by shader_build_tool --catalog. Do not edit.\n";
  header << "#pragma once\n\n#include <cstring>\n\nnamespace tgfx {\n\n";
  header << "struct ChainFunctionEntry {\n  const char* name = nullptr;\n"
            "  const char* source = nullptr;\n  const char* requires = nullptr;\n"
            "  const char* memberPrefix = nullptr;\n  const char* members = nullptr;\n};\n\n";
  header << "inline constexpr ChainFunctionEntry kChainFunctions[] = {\n";
  for (const auto& entry : entries) {
    std::string requiresList = {};
    for (size_t index = 0; index < entry.requires.size(); ++index) {
      if (index > 0) {
        requiresList += ",";
      }
      requiresList += entry.requires[index];
    }
    std::string membersList = {};
    for (const auto& member : entry.members) {
      if (!membersList.empty()) {
        membersList += ",";
      }
      membersList += member.first + "=" + member.second;
    }
    header << "    {\"" << entry.name << "\", R\"TGSLSRC(" << entry.source << ")TGSLSRC\", \""
           << requiresList << "\", \"" << entry.memberPrefix << "\", \"" << membersList << "\"},\n";
  }
  header << "};\n\n";
  header << "inline const char* FindChainFunctionSource(const char* name) {\n";
  header << "  for (const auto& entry : kChainFunctions) {\n";
  header
      << "    if (std::strcmp(entry.name, name) == 0) {\n      return entry.source;\n    }\n  }\n";
  header << "  return nullptr;\n}\n\n}  // namespace tgfx\n";
  std::ofstream out(outPath, std::ios::binary);
  if (!out.is_open()) {
    *error = "cannot write " + outPath;
    return false;
  }
  out << header.str();
  // The registration code the plan includes: same scan, emitted as C++ so no string-to-enum lookup
  // is needed. Sorted by function name and then by declaration order, so the file is reproducible.
  std::vector<const CatalogEntry*> sorted = {};
  for (const auto& entry : entries) {
    sorted.push_back(&entry);
  }
  std::sort(sorted.begin(), sorted.end(), [](const CatalogEntry* left, const CatalogEntry* right) {
    return left->name < right->name;
  });
  std::ostringstream registry;
  registry << "// Generated by shader_build_tool --catalog. Do not edit.\n";
  registry << "// Each section is guarded by TGFX_REGISTER_<function>: the includer defines the "
              "macro for\n"
              "// the one entry it registers (no include guard, the file is included once per "
              "entry).\n\n";
  for (const CatalogEntry* entry : sorted) {
    if (entry->members.empty() || entry->memberPrefix.empty()) {
      continue;
    }
    registry << "#if defined(TGFX_REGISTER_" << entry->name << ")\n";
    const auto params = SignatureParams(entry->source, entry->name);
    if (params.size() < entry->memberStart) {
      continue;
    }
    const std::string prefix = entry->memberPrefix;
    std::vector<std::string> order = {};
    std::map<std::string, std::tuple<std::string, size_t, std::string, std::string>> members = {};
    for (size_t index = entry->memberStart; index < params.size(); ++index) {
      const std::string& paramName = params[index].second;
      std::string memberName = prefix + paramName;
      size_t count = 1;
      for (const auto& member : entry->members) {
        if (member.first != paramName) {
          continue;
        }
        memberName = member.second;
        const auto bracket = memberName.find('[');
        if (bracket != std::string::npos) {
          count = static_cast<size_t>(std::atoi(memberName.c_str() + bracket + 1)) + 1;
          memberName = memberName.substr(0, bracket);
        }
        break;
      }
      const char* fragmentType = FragmentTypeOf(params[index].first);
      if (fragmentType == nullptr) {
        *error = entry->name + ": unsupported parameter type " + params[index].first;
        return false;
      }
      std::string enumName = BindingSourceOf(memberName, prefix);
      std::string guard = {};
      for (const auto& source : entry->sources) {
        if (source.first == paramName) {
          enumName = source.second;
        }
      }
      for (const auto& condition : entry->conditions) {
        if (condition.first == paramName) {
          guard = condition.second;
        }
      }
      auto found = members.find(memberName);
      if (found == members.end()) {
        order.push_back(memberName);
        members.emplace(memberName,
                        std::make_tuple(std::string(fragmentType), count, enumName, guard));
      } else if (count > std::get<1>(found->second)) {
        std::get<1>(found->second) = count;
      }
    }
    for (const auto& memberName : order) {
      const auto& info = members[memberName];
      if (!std::get<3>(info).empty()) {
        registry << "  if (" << std::get<3>(info) << ") {\n";
      }
      registry << (std::get<3>(info).empty() ? "  " : "    ") << "registry->uniform(\""
               << memberName << "\", FragmentType::" << std::get<0>(info)
               << ", ChainBindingSource::" << std::get<2>(info) << ", -1";
      if (std::get<1>(info) > 1) {
        // uniform(name, type, source, slot, index, count): an array names no index.
        registry << ", -1, " << std::get<1>(info);
      }
      registry << ");\n";
      if (!std::get<3>(info).empty()) {
        registry << "  }\n";
      }
    }
    registry << "#endif\n";
  }
  const std::filesystem::path registryPath =
      std::filesystem::path(outPath).parent_path() / "ChainOpRegistry.inc";
  std::ofstream registryOut(registryPath, std::ios::binary);
  if (!registryOut.is_open()) {
    *error = "cannot write " + registryPath.string();
    return false;
  }
  registryOut << registry.str();
  return true;
}

}  // namespace tgfx

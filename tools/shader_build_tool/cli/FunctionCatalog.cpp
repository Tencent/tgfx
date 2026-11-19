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

#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>
#include "cli/BuildCommand.h"

namespace tgfx {
namespace {

struct CatalogEntry {
  std::string name = {};
  std::string source = {};
  std::vector<std::string>
  requires = {};
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
    const std::string requiresTag = "//@requires ";
    if (line.rfind(functionTag, 0) == 0) {
      if (!entry->name.empty()) {
        *error = "a file declares more than one //@function";
        return false;
      }
      entry->name = line.substr(functionTag.size());
    } else if (line.rfind(requiresTag, 0) == 0) {
      const std::string list = line.substr(requiresTag.size());
      size_t start = 0;
      while (start <= list.size()) {
        size_t end = list.find(',', start);
        if (end == std::string::npos) {
          end = list.size();
        }
        const std::string name = list.substr(start, end - start);
        const auto first = name.find_first_not_of(" \t");
        const auto last = name.find_last_not_of(" \t");
        if (first != std::string::npos) {
          entry->requires.push_back(name.substr(first, last - first + 1));
        }
        if (end == list.size()) {
          break;
        }
        start = end + 1;
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
  for (const auto& entry : entries) {
    for (const auto& required : entry.requires) {
      bool found = false;
      for (const auto& other : entries) {
        if (other.name == required) {
          found = true;
        }
      }
      if (!found) {
        *error = entry.name + " requires the unknown function " + required;
        return false;
      }
    }
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
            "  const char* source = nullptr;\n  const char* requires = nullptr;\n};\n\n";
  header << "inline constexpr ChainFunctionEntry kChainFunctions[] = {\n";
  for (const auto& entry : entries) {
    std::string requiresList = {};
    for (size_t index = 0; index < entry.requires.size(); ++index) {
      if (index > 0) {
        requiresList += ",";
      }
      requiresList += entry.requires[index];
    }
    header << "    {\"" << entry.name << "\", R\"TGSLSRC(" << entry.source << ")TGSLSRC\", \""
           << requiresList << "\"},\n";
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
  return true;
}

}  // namespace tgfx

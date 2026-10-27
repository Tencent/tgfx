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
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// The diagnostic code registry is a vocabulary two sides have to agree on: the report sites
// reference DiagnosticCode::<Name>, and AllCodes lists what the report writer may ever emit.
// These tests keep the two directions in step, the same way LayeringTest keeps the include
// whitelist and the module graph in step: a referenced-but-undeclared code fails, and a
// declared-but-unreferenced code fails (dead vocabulary, or a site that still reports through
// something else).

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "core/DiagnosticCodes.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

const std::string ToolDir = std::string(TGFX_SOURCE_DIR) + "/tools/shader_build_tool";

struct DeclaredCode {
  std::string name;
  std::string value;
};

// Parses "inline constexpr const char* <Name> = "<value>";" lines of DiagnosticCodes.h: the
// header is the only place a code is defined, so reading it back is the registry's own check.
std::vector<DeclaredCode> DeclaredCodes() {
  std::ifstream file(ToolDir + "/core/DiagnosticCodes.h");
  std::vector<DeclaredCode> codes;
  const std::string marker = "inline constexpr const char* ";
  std::string line;
  while (std::getline(file, line)) {
    auto start = line.find(marker);
    if (start == std::string::npos) {
      continue;
    }
    start += marker.size();
    auto equals = line.find('=', start);
    if (equals == std::string::npos) {
      continue;
    }
    DeclaredCode code;
    code.name = line.substr(start, equals - start);
    while (!code.name.empty() && code.name.back() == ' ') {
      code.name.pop_back();
    }
    auto quoteStart = line.find('"', equals);
    auto quoteEnd = line.find('"', quoteStart + 1);
    if (quoteStart == std::string::npos || quoteEnd == std::string::npos) {
      continue;
    }
    code.value = line.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
    codes.push_back(std::move(code));
  }
  return codes;
}

// Every DiagnosticCode::<Name> the tool's sources reference (the declaration site excluded).
std::set<std::string> ReferencedCodeNames() {
  std::set<std::string> names;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(ToolDir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    auto extension = entry.path().extension().string();
    if (extension != ".cpp" && extension != ".h") {
      continue;
    }
    if (entry.path().filename() == "DiagnosticCodes.h") {
      continue;
    }
    std::ifstream file(entry.path());
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::string prefix = "DiagnosticCode::";
    size_t position = 0;
    while ((position = text.find(prefix, position)) != std::string::npos) {
      position += prefix.size();
      size_t end = position;
      while (end < text.size()) {
        char ch = text[end];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
              ch == '_')) {
          break;
        }
        end++;
      }
      if (end > position) {
        names.insert(text.substr(position, end - position));
      }
      position = end;
    }
  }
  return names;
}

TEST(DiagnosticCodeTest, TheRegistryListsEveryDeclaredCodeOnce) {
  auto declared = DeclaredCodes();
  ASSERT_FALSE(declared.empty());
  std::set<std::string> values;
  for (const auto& code : declared) {
    EXPECT_TRUE(values.insert(code.value).second) << "duplicate code value: " << code.value;
  }
  std::set<std::string> listed;
  for (const char* code : DiagnosticCode::AllCodes) {
    EXPECT_TRUE(listed.insert(code).second) << "duplicate in AllCodes: " << code;
  }
  // AllCodes is the report writer's vocabulary; every declared code must appear, and nothing
  // else may.
  EXPECT_EQ(values, listed);
}

TEST(DiagnosticCodeTest, EveryReferencedCodeIsDeclared) {
  std::set<std::string> declared;
  for (const auto& code : DeclaredCodes()) {
    declared.insert(code.name);
  }
  for (const auto& name : ReferencedCodeNames()) {
    EXPECT_EQ(declared.count(name), 1u)
        << "DiagnosticCode::" << name << " is referenced but not declared in DiagnosticCodes.h";
  }
}

TEST(DiagnosticCodeTest, EveryDeclaredCodeIsReferenced) {
  // A code that nothing reports is either dead vocabulary or a site that still prints instead
  // of reporting — both wrong. Deleting the constant or wiring the site are the two fixes.
  auto referenced = ReferencedCodeNames();
  for (const auto& code : DeclaredCodes()) {
    EXPECT_EQ(referenced.count(code.name), 1u)
        << code.name << " (" << code.value << ") is declared but never reported";
  }
}

}  // namespace
}  // namespace tgfx

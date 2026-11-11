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
//  license is distributed on an "as is" basis, without warranties of any kind, either express or
//  implied. see the license for the specific language governing permissions and limitations under
//  the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// The usage text is the only place a user learns the tool's options, so it must not drift from
// the parser: an option advertised but no longer parsed sends the user into "Unknown option", and
// the usage text they get back advertises the same option again. These tests keep the two in
// step, the same way DiagnosticCodeTest keeps the code vocabulary in step.

#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "cli/BuildCommand.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

// Every "--name" token in the usage text, in order of first appearance.
std::vector<std::string> DocumentedOptions(const std::string& usage) {
  std::vector<std::string> names;
  std::set<std::string> seen;
  size_t position = usage.find("--");
  while (position != std::string::npos) {
    size_t end = position + 2;
    while (end < usage.size()) {
      const char character = usage[end];
      if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
            character == '-')) {
        break;
      }
      ++end;
    }
    if (end > position + 2) {
      auto name = usage.substr(position, end - position);
      if (seen.insert(name).second) {
        names.push_back(std::move(name));
      }
    }
    position = usage.find("--", end);
  }
  return names;
}

// The argument a documented option needs to parse: the ones that validate their value get a legal
// one, everything else takes a placeholder. A flag ignores the placeholder and then fails on it as
// an unknown argument, which is what the caller's second attempt without a value covers.
std::string SampleValue(const std::string& name) {
  if (name == "--backends") {
    return "opengl";
  }
  if (name == "--d3d12-shader-format") {
    return "dxbc";
  }
  if (name == "--metal-sdk") {
    return "macosx";
  }
  if (name == "--metal-min-os") {
    return "15.0";
  }
  return "x";
}

// Whether ParseArgs accepts the option, alone or with a sample value.
bool Parses(const std::string& name, bool withValue) {
  std::vector<std::string> storage = {"shader_build_tool", name};
  if (withValue) {
    storage.push_back(SampleValue(name));
  }
  std::vector<char*> argv = {};
  argv.reserve(storage.size());
  for (auto& argument : storage) {
    argv.push_back(const_cast<char*>(argument.c_str()));
  }
  BuildOptions options = {};
  return ParseArgs(static_cast<int>(argv.size()), argv.data(), &options);
}

}  // namespace

TEST(CliTest, EveryDocumentedOptionParses) {
  // A rejected attempt prints the usage to stderr; the test's own failure message is the report.
  std::ostringstream sink;
  auto* previous = std::cerr.rdbuf(sink.rdbuf());
  std::vector<std::string> rejected = {};
  const auto documented = DocumentedOptions(UsageText());
  for (const auto& name : documented) {
    if (!Parses(name, true) && !Parses(name, false)) {
      rejected.push_back(name);
    }
  }
  std::cerr.rdbuf(previous);
  ASSERT_FALSE(documented.empty());
  for (const auto& name : rejected) {
    ADD_FAILURE() << name << " is documented in UsageText() but ParseArgs rejects it";
  }
}

TEST(CliTest, TheBuildOptionsAreDocumented) {
  const auto usage = UsageText();
  EXPECT_NE(usage.find("--shader-dir"), std::string::npos);
  EXPECT_NE(usage.find("--out-dir"), std::string::npos);
  EXPECT_NE(usage.find("--backends"), std::string::npos);
}

}  // namespace tgfx

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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// The report's diagnostics array is the machine-readable counterpart of the human lines the cli
// prints: these tests pin that a clean build writes an empty array and that every field of a
// record survives, with the message escaped (compiler stderr carries quotes and newlines).

#include <fstream>
#include <sstream>
#include <string>
#include "cli/ReportWriter.h"
#include "core/DiagnosticCodes.h"
#include "core/Diagnostics.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

std::string ReadReport(const std::string& dir) {
  std::ifstream file(dir + "/shader_build_report.json");
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

TEST(ReportWriterTest, ACleanBuildWritesAnEmptyDiagnosticsArray) {
  BuildReport report;
  auto dir = std::string(::testing::TempDir());
  ASSERT_TRUE(WriteReportJson(report, dir));
  auto text = ReadReport(dir);
  EXPECT_NE(text.find("\"diagnostics\": ["), std::string::npos) << text;
  EXPECT_NE(text.find("\"diagnostics\": [\n  ]"), std::string::npos) << text;
  // The counts the report always carried are still there.
  EXPECT_NE(text.find("\"shaders\": ["), std::string::npos) << text;
  EXPECT_NE(text.find("\"artifactProfiles\": ["), std::string::npos) << text;
}

TEST(ReportWriterTest, ADiagnosticKeepsEveryFieldAndEscapesItsMessage) {
  BuildReport report;
  report.diagnostics.registerProfile("webgpu");
  Diagnostic diagnostic;
  diagnostic.severity = Severity::Unverifiable;
  diagnostic.layer = Layer::Verify;
  diagnostic.code = DiagnosticCode::VerifyNotApplicable;
  diagnostic.shader = "MaskFillShader";
  diagnostic.profile = "webgpu";
  diagnostic.vertPermutationIndex = 3;
  diagnostic.fragPermutationIndex = 7;
  diagnostic.message = "no \"fn main(\" in the\nvertex stage";
  report.diagnostics.report(std::move(diagnostic));
  auto dir = std::string(::testing::TempDir());
  ASSERT_TRUE(WriteReportJson(report, dir));
  auto text = ReadReport(dir);
  EXPECT_NE(text.find("\"severity\": \"unverifiable\""), std::string::npos) << text;
  EXPECT_NE(text.find("\"layer\": \"verify\""), std::string::npos) << text;
  EXPECT_NE(text.find("\"code\": \"verify.unverifiable\""), std::string::npos) << text;
  EXPECT_NE(text.find("\"shader\": \"MaskFillShader\""), std::string::npos) << text;
  EXPECT_NE(text.find("\"profile\": \"webgpu\""), std::string::npos) << text;
  EXPECT_NE(text.find("\"vert\": 3"), std::string::npos) << text;
  EXPECT_NE(text.find("\"frag\": 7"), std::string::npos) << text;
  // Quotes and the newline are escaped, so the file stays valid JSON.
  EXPECT_NE(text.find("no \\\"fn main(\\\" in the\\nvertex stage"), std::string::npos) << text;
  // The unverifiable record fails the build and counts against its profile.
  EXPECT_TRUE(report.diagnostics.hasErrors());
  EXPECT_EQ(report.diagnostics.errorCounts().at("webgpu"), 1u);
}

}  // namespace
}  // namespace tgfx

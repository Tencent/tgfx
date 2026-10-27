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

// Diagnostics is the error ledger the build reports through. These tests pin its counting rules:
// an error whose profile is empty lands on every profile that takes part in the build at that
// moment, a profile error lands on that profile alone, warnings are logged but never counted,
// and "unverifiable" fails the build like an error while staying a distinct severity. They also
// pin that re-registering a profile keeps its count, which is what the per-profile figures in
// shader_build_report.json rely on.

#include <map>
#include <string>
#include "core/DiagnosticCodes.h"
#include "core/Diagnostics.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

Diagnostic MakeDiagnostic(Severity severity, const char* profile, const char* message) {
  Diagnostic diagnostic;
  diagnostic.severity = severity;
  diagnostic.layer = Layer::Verify;
  diagnostic.code = DiagnosticCode::CompileGlsl;
  diagnostic.profile = profile;
  diagnostic.shader = "TestShader";
  diagnostic.message = message;
  return diagnostic;
}

TEST(DiagnosticsTest, ACommonErrorCountsEveryRegisteredProfile) {
  Diagnostics diagnostics;
  diagnostics.registerProfile("opengl");
  diagnostics.registerProfile("vulkan");
  diagnostics.report(MakeDiagnostic(Severity::Error, "", "common"));
  diagnostics.report(MakeDiagnostic(Severity::Error, "", "common"));
  EXPECT_EQ(diagnostics.errorCounts().at("opengl"), 2u);
  EXPECT_EQ(diagnostics.errorCounts().at("vulkan"), 2u);
}

TEST(DiagnosticsTest, ACommonErrorBeforeAnyRegistrationIsLost) {
  // The ledger counts over its registered profiles, exactly as the bare map did: a common
  // error recorded before any profile exists changes no figure. The build registers every
  // requested profile before the first compile, so this is the empty-build corner only.
  Diagnostics diagnostics;
  diagnostics.report(MakeDiagnostic(Severity::Error, "", "common"));
  EXPECT_TRUE(diagnostics.errorCounts().empty());
}

TEST(DiagnosticsTest, AProfileErrorCountsOnlyThatProfile) {
  Diagnostics diagnostics;
  diagnostics.registerProfile("opengl");
  diagnostics.registerProfile("vulkan");
  diagnostics.report(MakeDiagnostic(Severity::Error, "vulkan", "mismatch"));
  diagnostics.report(MakeDiagnostic(Severity::Error, "vulkan", "mismatch"));
  EXPECT_EQ(diagnostics.errorCounts().at("opengl"), 0u);
  EXPECT_EQ(diagnostics.errorCounts().at("vulkan"), 2u);
}

TEST(DiagnosticsTest, AProfileErrorForAnUnregisteredProfileStillShowsUp) {
  // A backend error for a profile nobody registered must not disappear: the report has to
  // show it rather than zero it out.
  Diagnostics diagnostics;
  diagnostics.registerProfile("opengl");
  diagnostics.report(MakeDiagnostic(Severity::Error, "metal", "backend"));
  EXPECT_EQ(diagnostics.errorCounts().at("metal"), 1u);
  EXPECT_EQ(diagnostics.errorCounts().at("opengl"), 0u);
}

TEST(DiagnosticsTest, RegisteringAProfileTwiceKeepsItsCount) {
  Diagnostics diagnostics;
  diagnostics.registerProfile("opengl");
  diagnostics.report(MakeDiagnostic(Severity::Error, "opengl", "backend"));
  diagnostics.registerProfile("opengl");
  EXPECT_EQ(diagnostics.errorCounts().at("opengl"), 1u);
}

TEST(DiagnosticsTest, TheCountsComeBackInTagOrder) {
  // The report iterates the counts to write its artifactProfiles array; the ledger keeps the
  // map's key order, so the array's profile order stays stable across runs.
  Diagnostics diagnostics;
  diagnostics.registerProfile("vulkan");
  diagnostics.registerProfile("opengl");
  std::map<std::string, uint64_t> expected = {{"opengl", 0}, {"vulkan", 0}};
  EXPECT_EQ(diagnostics.errorCounts(), expected);
}

TEST(DiagnosticsTest, ACommonErrorRecordCountsEveryProfileRegisteredAtThatMoment) {
  // Point-in-time semantics, preserved from the bare map: a profile registered after the record
  // does not inherit it, which is what keeps the report figures identical to the old counting.
  Diagnostics diagnostics;
  diagnostics.registerProfile("opengl");
  diagnostics.report(MakeDiagnostic(Severity::Error, "", "common"));
  diagnostics.registerProfile("vulkan");
  EXPECT_EQ(diagnostics.errorCounts().at("opengl"), 1u);
  EXPECT_EQ(diagnostics.errorCounts().at("vulkan"), 0u);
}

TEST(DiagnosticsTest, AWarningIsLoggedButDoesNotFailOrCount) {
  Diagnostics diagnostics;
  diagnostics.registerProfile("opengl");
  diagnostics.report(MakeDiagnostic(Severity::Warning, "opengl", "deprecated"));
  EXPECT_EQ(diagnostics.errorCounts().at("opengl"), 0u);
  EXPECT_EQ(diagnostics.count(Severity::Warning), 1u);
  EXPECT_FALSE(diagnostics.hasErrors());
}

TEST(DiagnosticsTest, AnUnverifiableRecordFailsTheBuildLikeAnError) {
  // "Never checked" is not "checked and passed": it must fail the build, and it counts against
  // the profile so the report shows the build as failed for that backend too.
  Diagnostics diagnostics;
  diagnostics.registerProfile("webgpu");
  diagnostics.report(MakeDiagnostic(Severity::Unverifiable, "webgpu", "could not run"));
  EXPECT_TRUE(diagnostics.hasErrors());
  EXPECT_EQ(diagnostics.errorCounts().at("webgpu"), 1u);
  EXPECT_EQ(diagnostics.count(Severity::Unverifiable), 1u);
}

TEST(DiagnosticsTest, RecordsKeepReportOrderAndContent) {
  Diagnostics diagnostics;
  diagnostics.registerProfile("opengl");
  diagnostics.report(MakeDiagnostic(Severity::Error, "opengl", "first"));
  diagnostics.report(MakeDiagnostic(Severity::Warning, "", "second"));
  diagnostics.report(MakeDiagnostic(Severity::Unverifiable, "metal", "third"));
  ASSERT_EQ(diagnostics.records().size(), 3u);
  EXPECT_EQ(diagnostics.records()[0].message, "first");
  EXPECT_EQ(diagnostics.records()[0].shader, "TestShader");
  EXPECT_EQ(diagnostics.records()[1].message, "second");
  EXPECT_EQ(diagnostics.records()[2].message, "third");
  EXPECT_EQ(diagnostics.records()[2].profile, "metal");
}

}  // namespace
}  // namespace tgfx

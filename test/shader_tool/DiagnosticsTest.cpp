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
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// Diagnostics is the error ledger the compile loop reports through. These tests pin its two
// counting rules: a common error lands on every profile that takes part in the build, and a
// profile error lands on that profile alone, whether or not it was registered up front. They
// also pin that re-registering a profile keeps its count, which is what the per-profile
// figures in shader_build_report.json rely on when a profile is registered once per build.

#include <map>
#include <string>
#include "core/Diagnostics.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

TEST(DiagnosticsTest, ACommonErrorCountsEveryRegisteredProfile) {
  Diagnostics diagnostics;
  diagnostics.registerProfile("opengl");
  diagnostics.registerProfile("vulkan");
  diagnostics.recordCommonError();
  diagnostics.recordCommonError();
  EXPECT_EQ(diagnostics.errorCounts().at("opengl"), 2u);
  EXPECT_EQ(diagnostics.errorCounts().at("vulkan"), 2u);
}

TEST(DiagnosticsTest, ACommonErrorBeforeAnyRegistrationIsLost) {
  // The ledger counts over its registered profiles, exactly as the bare map did: a common
  // error recorded before any profile exists changes no figure. The build registers every
  // requested profile before the first compile, so this is the empty-build corner only.
  Diagnostics diagnostics;
  diagnostics.recordCommonError();
  EXPECT_TRUE(diagnostics.errorCounts().empty());
}

TEST(DiagnosticsTest, AProfileErrorCountsOnlyThatProfile) {
  Diagnostics diagnostics;
  diagnostics.registerProfile("opengl");
  diagnostics.registerProfile("vulkan");
  diagnostics.recordProfileError("vulkan");
  diagnostics.recordProfileError("vulkan");
  EXPECT_EQ(diagnostics.errorCounts().at("opengl"), 0u);
  EXPECT_EQ(diagnostics.errorCounts().at("vulkan"), 2u);
}

TEST(DiagnosticsTest, AProfileErrorForAnUnregisteredProfileStillShowsUp) {
  // A backend error for a profile nobody registered must not disappear: the report has to
  // show it rather than zero it out.
  Diagnostics diagnostics;
  diagnostics.registerProfile("opengl");
  diagnostics.recordProfileError("metal");
  EXPECT_EQ(diagnostics.errorCounts().at("metal"), 1u);
  EXPECT_EQ(diagnostics.errorCounts().at("opengl"), 0u);
}

TEST(DiagnosticsTest, RegisteringAProfileTwiceKeepsItsCount) {
  Diagnostics diagnostics;
  diagnostics.registerProfile("opengl");
  diagnostics.recordProfileError("opengl");
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

}  // namespace
}  // namespace tgfx

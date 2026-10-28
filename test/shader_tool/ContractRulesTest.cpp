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

// The rule registries are the only way the build reaches a contract check, so what they hold
// is what the build checks. These tests pin the size of each registry (a dropped rule is a
// silent hole in the gate) and that every rule is clean on the real sources — the state the
// golden bundles were built in.

#include <string>
#include <vector>
#include "gpu/shaders/AllShaderFamilies.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gtest/gtest.h"
#include "verify/ContractRules.h"

namespace tgfx {
namespace {

const std::string RealShaderDir = std::string(TGFX_SOURCE_DIR) + "/src/gpu/shaders/glsl";

SourceRuleContext RealSourceContext() {
  SourceRuleContext context;
  context.shaderDir = RealShaderDir;
  for (auto factory : ShaderRegistry::All()) {
    context.registeredShaders.push_back(factory()->info().name);
  }
  return context;
}

}  // namespace

TEST(ContractRulesTest, TheSourceRegistryHoldsTheSixSourceRules) {
  EXPECT_EQ(MakeSourceRules().size(), 6u);
}

TEST(ContractRulesTest, TheArtifactRegistryHoldsTheFiveArtifactRules) {
  EXPECT_EQ(MakeArtifactRules().size(), 5u);
}

TEST(ContractRulesTest, EverySourceRuleIsCleanOnTheRealSources) {
  auto context = RealSourceContext();
  for (const auto& rule : MakeSourceRules()) {
    Diagnostics diagnostics;
    EXPECT_EQ(rule->check(context, &diagnostics), 0u)
        << "a source rule reports violations on the sources the golden bundles were built from";
    EXPECT_TRUE(diagnostics.records().empty());
  }
}

TEST(ContractRulesTest, EveryArtifactRuleIsCleanOnAnEmptyBuild) {
  ArtifactRuleContext context;
  std::vector<VariantData> variants;
  std::vector<InlineExpansionRecord> expansions;
  context.variants = &variants;
  context.inlineExpansions = &expansions;
  for (const auto& rule : MakeArtifactRules()) {
    Diagnostics diagnostics;
    EXPECT_EQ(rule->check(context, &diagnostics), 0u);
    EXPECT_TRUE(diagnostics.records().empty());
  }
}

}  // namespace tgfx

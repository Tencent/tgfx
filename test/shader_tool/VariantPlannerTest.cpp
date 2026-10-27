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

// The planner is the single enumeration entry of the tool: the build loop, --audit and
// --report-only all get their variants from it. These tests pin its two guarantees for every
// registered family — the plan agrees with the declaration layer's reachable set, and planning
// twice yields the same variants in the same order — so the bundle content stays reproducible
// and the three consumers cannot disagree.

#include <set>
#include <string>
#include <utility>
#include <vector>
#include "gpu/shaders/AllShaderFamilies.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "plan/VariantPlanner.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

std::vector<PrecompiledShaderInfo> AllFamilies() {
  std::vector<PrecompiledShaderInfo> families;
  for (const auto& factory : ShaderRegistry::All()) {
    families.push_back(factory()->info());
  }
  return families;
}

// The flat (family, vert, frag) sequence of a full plan over the registry, or an empty vector
// with *failure naming the family that has no plan.
std::vector<std::string> FlattenRegistryPlan(std::string* failure) {
  std::vector<std::string> sequence;
  for (const auto& info : AllFamilies()) {
    auto plan = VariantPlanner::PlanShader(info);
    if (!plan.variants.has_value()) {
      *failure = info.name;
      return {};
    }
    for (const auto& pair : *plan.variants) {
      sequence.push_back(info.name + "/" + std::to_string(pair.first) + "/" +
                         std::to_string(pair.second));
    }
  }
  return sequence;
}

}  // namespace

TEST(VariantPlannerTest, EveryFamilyHasAPlan) {
  for (const auto& info : AllFamilies()) {
    auto plan = VariantPlanner::PlanShader(info);
    ASSERT_TRUE(plan.variants.has_value()) << info.name;
    EXPECT_FALSE(plan.variants->empty()) << info.name;
  }
}

TEST(VariantPlannerTest, ThePlanAgreesWithTheDeclarationLayer) {
  // The planner may validate what the declaration layer reaches, never invent or drop it:
  // every family's plan equals the Compose-reachable set exactly, and on the current sources
  // every reachable pair is buildable.
  for (const auto& info : AllFamilies()) {
    auto plan = VariantPlanner::PlanShader(info);
    ASSERT_TRUE(plan.variants.has_value()) << info.name;
    auto reachable = EnumerateReachablePermutations(info.name);
    ASSERT_TRUE(reachable.has_value()) << info.name;
    EXPECT_EQ(*plan.variants, *reachable) << info.name;
    EXPECT_TRUE(plan.unbuildable.empty()) << info.name;
  }
}

TEST(VariantPlannerTest, PlanningTwiceYieldsTheSameVariantsInTheSameOrder) {
  // The variants of a plan are a sorted set, so planning is deterministic by construction;
  // this pins it, because the bundle byte content follows this order.
  for (const auto& info : AllFamilies()) {
    auto first = VariantPlanner::PlanShader(info);
    auto second = VariantPlanner::PlanShader(info);
    ASSERT_TRUE(first.variants.has_value()) << info.name;
    ASSERT_TRUE(second.variants.has_value()) << info.name;
    EXPECT_EQ(*first.variants, *second.variants) << info.name;
    EXPECT_EQ(first.unbuildable, second.unbuildable) << info.name;
  }
}

TEST(VariantPlannerTest, TheWholeRegistryPlansToAStableVariantSequence) {
  // The registry order is fixed by registration and each plan is sorted, so the flat
  // (family, vert, frag) sequence of a full plan is stable — the identity a deterministic
  // build rests on.
  std::string failure;
  auto first = FlattenRegistryPlan(&failure);
  EXPECT_TRUE(failure.empty()) << failure;
  auto second = FlattenRegistryPlan(&failure);
  EXPECT_TRUE(failure.empty()) << failure;
  EXPECT_FALSE(first.empty());
  EXPECT_EQ(first, second);
}

}  // namespace tgfx

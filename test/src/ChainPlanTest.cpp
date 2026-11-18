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
//  either express or implied. see the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// The plan's golden contract: the interface is the emission's closure - every call's parameter
// members are declared with the slot they belong to, and the (op, slot, param) triple names the
// typed slot field the writer reads. The slot-qualified member names are the guard the positional
// pool never had: a mis-mapped slot cannot compile into a wrong word here, it shows up as a
// missing member.

#include "base/TGFXTest.h"
#include "gpu/shaders/contract/ChainComposition.h"
#include "gpu/shaders/contract/ChainPlan.h"

namespace tgfx {
namespace {

ChainCompositionSlot Slot(int op, int in0, int in1, int outRegister) {
  ChainCompositionSlot slot = {};
  slot.op = op;
  slot.in0 = in0;
  slot.in1 = in1;
  slot.outRegister = outRegister;
  return slot;
}

const PlanParam* FindParam(const ChainPlan& plan, const std::string& member) {
  for (const auto& param : plan.params) {
    if (param.member == member) {
      return &param;
    }
  }
  return nullptr;
}

}  // namespace

TGFX_TEST(ChainPlanTest, FillDrawDeclaresOneNamedParameterAndSinksTheRoot) {
  ChainComposition composition = {};
  auto color = Slot(ChainISA::ConstColor, -1, -2, 0);
  color.inputMode = 1;
  composition.slots = {color};
  composition.rootRegister = 0;
  const auto plan = BuildChainPlan(composition);
  ASSERT_EQ(plan.calls.size(), 1u);
  EXPECT_EQ(plan.calls[0].op, ChainISA::ConstColor);
  EXPECT_EQ(plan.calls[0].slot, 0);
  EXPECT_EQ(plan.calls[0].sink, "result");
  ASSERT_EQ(plan.params.size(), 1u);
  EXPECT_EQ(plan.params[0].member, "pmApplyConstColor_0_color");
  EXPECT_EQ(plan.params[0].op, ChainISA::ConstColor);
  EXPECT_EQ(plan.params[0].slot, 0);
  EXPECT_EQ(plan.params[0].offset, 0u);
  EXPECT_EQ(plan.params[0].size, 16u);
  EXPECT_EQ(plan.paramBlockSize, 16u);
}

TGFX_TEST(ChainPlanTest, TextureLumaChainNamesTheSecondSlotsParameter) {
  ChainComposition composition = {};
  composition.slots = {Slot(ChainISA::Texture, -2, -2, 0), Slot(ChainISA::Luma, 0, -2, 1)};
  composition.rootRegister = 1;
  const auto plan = BuildChainPlan(composition);
  ASSERT_EQ(plan.calls.size(), 2u);
  EXPECT_EQ(plan.calls[0].op, ChainISA::Texture);
  EXPECT_EQ(plan.calls[0].slot, 0);
  ASSERT_EQ(plan.calls[0].args.size(), 4u);
  EXPECT_EQ(plan.calls[0].args[0], "chainLeafTex[0]");
  EXPECT_TRUE(plan.hasTexture);
  EXPECT_EQ(plan.calls[1].op, ChainISA::Luma);
  // The slot mapping this pins: the luma's coefficients belong to slot 1, both in the member name
  // and in the (op, slot, param) triple the writer reads.
  ASSERT_EQ(plan.calls[1].args.size(), 2u);
  EXPECT_EQ(plan.calls[1].args[0], "cg0");
  EXPECT_EQ(plan.calls[1].args[1], "pmApplyLuma_1_coefficients");
  const auto* coefficients = FindParam(plan, "pmApplyLuma_1_coefficients");
  ASSERT_NE(coefficients, nullptr);
  EXPECT_EQ(coefficients->op, ChainISA::Luma);
  EXPECT_EQ(coefficients->slot, 1);
  EXPECT_EQ(coefficients->size, 12u);
  EXPECT_EQ(plan.calls[1].sink, "result");
}

TGFX_TEST(ChainPlanTest, CoverageSlotsCarryTheirOwnMembers) {
  ChainComposition composition = {};
  composition.slots = {Slot(ChainISA::LocalRectCoverage, -4, -2, 0),
                       Slot(ChainISA::RRectCoverage, 0, -2, 1)};
  composition.clipCoverageRegister = 1;
  composition.rootRegister = 2;  // no sink for the first coverage call
  const auto plan = BuildChainPlan(composition);
  ASSERT_EQ(plan.calls.size(), 2u);
  EXPECT_EQ(plan.calls[0].sink, "");
  EXPECT_EQ(plan.calls[1].sink, "clipRegister");
  // Each coverage slot owns its members (slot-qualified), the offsets accumulate per std140.
  const auto* rect0 = FindParam(plan, "tgfxOpLocalRectCoverage_0_rect");
  const auto* matrix0 = FindParam(plan, "tgfxOpLocalRectCoverage_0_deviceToLocal");
  const auto* rect1 = FindParam(plan, "tgfxOpRRectCoverage_1_rect");
  ASSERT_NE(rect0, nullptr);
  ASSERT_NE(matrix0, nullptr);
  ASSERT_NE(rect1, nullptr);
  EXPECT_EQ(rect0->offset, 0u);
  EXPECT_EQ(rect0->size, 16u);
  EXPECT_EQ(matrix0->offset, 16u);
  EXPECT_EQ(matrix0->size, 48u);
  EXPECT_EQ(rect1->offset, 64u);
  EXPECT_EQ(rect1->slot, 1);
  // The rrect's five members follow in table order past 64.
  const auto* radiiX = FindParam(plan, "tgfxOpRRectCoverage_1_radiiX");
  ASSERT_NE(radiiX, nullptr);
  EXPECT_EQ(radiiX->offset, 80u);
}

}  // namespace tgfx

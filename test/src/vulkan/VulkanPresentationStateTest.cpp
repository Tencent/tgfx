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
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/vulkan/VulkanPresentationState.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(VulkanPresentationStateTest, SubmissionAggregation) {
  VulkanSubmissionPlan plan;
  // A frame first used and presented by the same submission.
  plan.add(true, false);
  plan.add(false, true);
  // A frame only used by this submission (for example a readback): waits, never presents.
  plan.add(true, false);
  // A frame whose acquire wait was wired by an earlier submission: presents without waiting.
  plan.add(false, true);
  EXPECT_EQ(plan.acquireWaitCount, 2u);
  EXPECT_EQ(plan.signalCount, 2u);
}

TGFX_TEST(VulkanPresentationStateTest, ManualPhaseAndToken) {
  VulkanManualToken token;
  EXPECT_TRUE(token.acquire());
  EXPECT_FALSE(token.acquire());
  EXPECT_TRUE(token.isActive());

  VulkanFrameState frame(true);
  EXPECT_FALSE(frame.beginPresent());
  frame.markRenderSubmitted();
  EXPECT_EQ(frame.phase(), VulkanManualPhase::RenderSubmitted);
  EXPECT_TRUE(frame.beginPresent());
  EXPECT_FALSE(frame.beginPresent());

  token.release();
  EXPECT_FALSE(token.isActive());
  EXPECT_TRUE(token.acquire());
}

TGFX_TEST(VulkanPresentationStateTest, LayoutAndReadbackSupport) {
  VulkanSwapchainImageState state;
  EXPECT_EQ(*state.layout, VK_IMAGE_LAYOUT_UNDEFINED);

  EXPECT_TRUE(VulkanSupportsReadback(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
  EXPECT_FALSE(VulkanSupportsReadback(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));
}

}  // namespace tgfx

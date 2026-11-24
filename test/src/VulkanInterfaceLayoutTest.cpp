/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/ir/ProgramIRBuilder.h"
#include "gpu/spirv/SPIRVInterfaceLayout.h"
#include "gtest/gtest.h"

#if defined(TGFX_USE_VULKAN)
#include "gpu/vulkan/VulkanGPU.h"
#include "gpu/vulkan/VulkanInterfaceLayout.h"
#include "utils/ContextScope.h"
#endif

namespace tgfx {

#if defined(TGFX_USE_VULKAN)
TEST(VulkanInterfaceLayoutTest, CreatesDescriptorSetLayouts) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  if (context->backend() != Backend::Vulkan) {
    GTEST_SKIP() << "the Vulkan backend is not active";
  }
  auto* gpu = static_cast<VulkanGPU*>(context->gpu());
  ASSERT_TRUE(gpu != nullptr);

  ProgramIRBuilder program = {};
  program.AddUniform({"ViewMatrix", SLType::Float4x4, Precision::High, ShaderStage::Vertex, 1});
  program.AddUniform({"Color", SLType::Float4, Precision::High, ShaderStage::Fragment, 1});
  program.AddSampler({"TextureSampler_0", SLType::Texture2DSampler, 0});

  SPIRVInterfaceLayout layout = {};
  std::string error = {};
  ASSERT_TRUE(layout.Build(program.IR().interface, &error)) << error;

  std::vector<VkDescriptorSetLayout> setLayouts = {};
  ASSERT_TRUE(VulkanInterfaceLayout::Create(gpu->device(), layout, &setLayouts, &error)) << error;
  ASSERT_EQ(setLayouts.size(), 3u);
  EXPECT_NE(setLayouts[0], VK_NULL_HANDLE);
  EXPECT_NE(setLayouts[1], VK_NULL_HANDLE);
  EXPECT_NE(setLayouts[2], VK_NULL_HANDLE);

  VkPipelineLayoutCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  createInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  createInfo.pSetLayouts = setLayouts.data();
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  EXPECT_EQ(vkCreatePipelineLayout(gpu->device(), &createInfo, nullptr, &pipelineLayout),
            VK_SUCCESS);
  if (pipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(gpu->device(), pipelineLayout, nullptr);
  }
  VulkanInterfaceLayout::Destroy(gpu->device(), &setLayouts);
  EXPECT_TRUE(setLayouts.empty());
}
#endif

}  // namespace tgfx

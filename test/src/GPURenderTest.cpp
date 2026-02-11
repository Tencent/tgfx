/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include <memory>
#include <vector>
#include "InstancedGridRenderPass.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/RenderPass.h"
#include "utils/TestUtils.h"

namespace tgfx {

// ==================== GPU Tests ====================

TGFX_TEST(GPURenderTest, DepthRenderPassTest) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(image != nullptr);
  TextureDescriptor depthTextureDesc(110, 110, PixelFormat::DEPTH24_STENCIL8, false, 1,
                                     TextureUsage::RENDER_ATTACHMENT);
  auto depthTexture = context->gpu()->createTexture(depthTextureDesc);
  ASSERT_TRUE(depthTexture != nullptr);
  TextureDescriptor renderTextureDesc(
      110, 110, PixelFormat::RGBA_8888, false, 1,
      TextureUsage::RENDER_ATTACHMENT | TextureUsage::TEXTURE_BINDING);
  auto renderTexture = context->gpu()->createTexture(renderTextureDesc);
  ASSERT_TRUE(renderTexture != nullptr);
  RenderPassDescriptor renderPassDescriptor(renderTexture);
  renderPassDescriptor.depthStencilAttachment.texture = depthTexture;
  auto commandEncoder = context->gpu()->createCommandEncoder();
  ASSERT_TRUE(commandEncoder != nullptr);
  auto renderPass = commandEncoder->beginRenderPass(renderPassDescriptor);
  ASSERT_TRUE(renderPass != nullptr);
}

TGFX_TEST(GPURenderTest, InstancedGridRender) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = context->gpu();

  // Create render texture
  constexpr uint32_t rows = 100;
  constexpr uint32_t columns = 100;
  const int width =
      static_cast<int>(InstancedGridRenderPass::GRID_SIZE * static_cast<float>(columns) +
                       InstancedGridRenderPass::GRID_SPACING * static_cast<float>(columns - 1));
  const int height =
      static_cast<int>(InstancedGridRenderPass::GRID_SIZE * static_cast<float>(rows) +
                       InstancedGridRenderPass::GRID_SPACING * static_cast<float>(rows - 1));

  TextureDescriptor renderTextureDesc(
      width, height, PixelFormat::RGBA_8888, false, 1,
      TextureUsage::RENDER_ATTACHMENT | TextureUsage::TEXTURE_BINDING);
  auto renderTexture = gpu->createTexture(renderTextureDesc);
  ASSERT_TRUE(renderTexture != nullptr);

  // Create render pass
  auto commandEncoder = gpu->createCommandEncoder();
  ASSERT_TRUE(commandEncoder != nullptr);
  auto renderPass = InstancedGridRenderPass::Make(rows, columns);
  ASSERT_TRUE(renderPass->onDraw(commandEncoder.get(), renderTexture));

  // Submit and wait for completion
  auto commandBuffer = commandEncoder->finish();
  ASSERT_TRUE(commandBuffer != nullptr);
  gpu->queue()->submit(commandBuffer);
  gpu->queue()->waitUntilCompleted();

  auto surface =
      Surface::MakeFrom(context, renderTexture->getBackendTexture(), ImageOrigin::TopLeft);
  ASSERT_TRUE(surface != nullptr);

  // Also compare with baseline
  EXPECT_TRUE(Baseline::Compare(surface, "GPURenderTest/InstancedGridRender"));
}

}  // namespace tgfx

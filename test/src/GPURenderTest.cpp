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
#include "MultisampleTestEffect.h"
#include "tgfx/core/ImageFilter.h"
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

// ==================== Multisample Tests ====================

TGFX_TEST(GPURenderTest, MultisampleCount) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);

  // sampleCount=1: no MSAA, the diagonal edge should have hard aliased pixels.
  MultisampleConfig config1x = {};
  config1x.sampleCount = 1;
  config1x.outputColor = Color::Red();
  auto effect1x = MultisampleTestEffect::Make(config1x);
  auto filter1x = ImageFilter::Runtime(std::move(effect1x));
  auto image1x = image->makeWithFilter(std::move(filter1x));
  auto surface = Surface::Make(context, 200, 200);
  auto canvas = surface->getCanvas();
  canvas->drawImage(std::move(image1x));
  EXPECT_TRUE(Baseline::Compare(surface, "GPURenderTest/MultisampleCount_1x"));

  // sampleCount=4: MSAA enabled, the diagonal edge should have smooth anti-aliased pixels.
  MultisampleConfig config4x = {};
  config4x.sampleCount = 4;
  config4x.outputColor = Color::Red();
  auto effect4x = MultisampleTestEffect::Make(config4x);
  auto filter4x = ImageFilter::Runtime(std::move(effect4x));
  auto image4x = image->makeWithFilter(std::move(filter4x));
  canvas->clear();
  canvas->drawImage(std::move(image4x));
  EXPECT_TRUE(Baseline::Compare(surface, "GPURenderTest/MultisampleCount_4x"));
}

TGFX_TEST(GPURenderTest, MultisampleMask) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);

  // mask=0xFFFFFFFF: all samples enabled, should render the red triangle normally.
  MultisampleConfig configAllSamples = {};
  configAllSamples.sampleCount = 4;
  configAllSamples.sampleMask = 0xFFFFFFFF;
  configAllSamples.outputColor = Color::Red();
  auto effectAll = MultisampleTestEffect::Make(configAllSamples);
  auto filterAll = ImageFilter::Runtime(std::move(effectAll));
  auto imageAll = image->makeWithFilter(std::move(filterAll));
  auto surface = Surface::Make(context, 200, 200);
  auto canvas = surface->getCanvas();
  canvas->drawImage(std::move(imageAll));
  EXPECT_TRUE(Baseline::Compare(surface, "GPURenderTest/MultisampleMask_AllSamples"));

  // mask=0x0: no samples written, the result should be the clear color (transparent).
  MultisampleConfig configNoSamples = {};
  configNoSamples.sampleCount = 4;
  configNoSamples.sampleMask = 0x0;
  configNoSamples.outputColor = Color::Red();
  auto effectNone = MultisampleTestEffect::Make(configNoSamples);
  auto filterNone = ImageFilter::Runtime(std::move(effectNone));
  auto imageNone = image->makeWithFilter(std::move(filterNone));
  canvas->clear();
  canvas->drawImage(std::move(imageNone));
  EXPECT_TRUE(Baseline::Compare(surface, "GPURenderTest/MultisampleMask_NoSamples"));
}

TGFX_TEST(GPURenderTest, AlphaToCoverage) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);

  // alphaToCoverage=false with alpha=0.5: all 4 samples get (0.5,0,0,0.5), resolve = (0.5,0,0,0.5)
  MultisampleConfig configOff = {};
  configOff.sampleCount = 4;
  configOff.outputColor = {1.0f, 0.0f, 0.0f, 0.5f};
  configOff.alphaToCoverage = false;
  auto effectOff = MultisampleTestEffect::Make(configOff);
  auto filterOff = ImageFilter::Runtime(std::move(effectOff));
  auto imageOff = image->makeWithFilter(std::move(filterOff));
  auto surface = Surface::Make(context, 200, 200);
  auto canvas = surface->getCanvas();
  canvas->drawImage(std::move(imageOff));
  EXPECT_TRUE(Baseline::Compare(surface, "GPURenderTest/AlphaToCoverage_Off"));

  // alphaToCoverage=true with alpha=0.5: alpha drives coverage, ~2 of 4 samples written,
  // resolve produces a different (typically dimmer) result than alphaToCoverage=false.
  MultisampleConfig configOn = {};
  configOn.sampleCount = 4;
  configOn.outputColor = {1.0f, 0.0f, 0.0f, 0.5f};
  configOn.alphaToCoverage = true;
  auto effectOn = MultisampleTestEffect::Make(configOn);
  auto filterOn = ImageFilter::Runtime(std::move(effectOn));
  auto imageOn = image->makeWithFilter(std::move(filterOn));
  canvas->clear();
  canvas->drawImage(std::move(imageOn));
  EXPECT_TRUE(Baseline::Compare(surface, "GPURenderTest/AlphaToCoverage_On"));
}

}  // namespace tgfx

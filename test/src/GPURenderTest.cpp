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
#include "StencilMaskRenderPass.h"
#include "gpu/ProgramInfo.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/RenderPass.h"
#include "tgfx/gpu/Texture.h"
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

// ==================== Stencil Pipeline Tests ====================

// End-to-end exercise of the stencil-aware pipeline plumbing introduced for the bezier
// rasterization render path: ProgramInfo's stencil/colourWriteMask cache key contributions,
// GLSLProgramBuilder forwarding `depthStencil` to the pipeline descriptor, and the GL
// stencil-state factory's no-op detection (which previously dropped any pipeline whose
// compare function was Always — exactly the configuration the mask pass uses).
//
// Pass 1 stamps a centred disc into the stencil buffer with compare=Always +
// passOp=Replace; pass 2 fills the entire viewport with red but is gated on stencil==1.
// Reading back the rendered pixels lets us verify both that the mask was written (centre
// pixel is red) and that the stencil test is precise (corner pixel is the clear colour).
TGFX_TEST(GPURenderTest, StencilMaskRenderPass) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto gpu = context->gpu();
  ASSERT_TRUE(gpu != nullptr);

  constexpr int SIZE = 200;
  TextureDescriptor renderTextureDesc(
      SIZE, SIZE, PixelFormat::RGBA_8888, false, 1,
      TextureUsage::RENDER_ATTACHMENT | TextureUsage::TEXTURE_BINDING);
  auto renderTexture = gpu->createTexture(renderTextureDesc);
  ASSERT_TRUE(renderTexture != nullptr);

  TextureDescriptor stencilTextureDesc(SIZE, SIZE, PixelFormat::DEPTH24_STENCIL8, false, 1,
                                       TextureUsage::RENDER_ATTACHMENT);
  auto stencilTexture = gpu->createTexture(stencilTextureDesc);
  ASSERT_TRUE(stencilTexture != nullptr);

  auto encoder = gpu->createCommandEncoder();
  ASSERT_TRUE(encoder != nullptr);
  // Cover colour: opaque red, in premultiplied space.
  auto coverColor = Color{1.0f, 0.0f, 0.0f, 1.0f}.premultiply();
  auto pass = StencilMaskRenderPass::Make(coverColor);
  ASSERT_TRUE(pass != nullptr);
  ASSERT_TRUE(pass->draw(encoder.get(), renderTexture, stencilTexture));

  auto commandBuffer = encoder->finish();
  ASSERT_TRUE(commandBuffer != nullptr);
  gpu->queue()->submit(commandBuffer);
  gpu->queue()->waitUntilCompleted();

  // Sample two pixels: the surface centre (well inside the disc, must be red) and a corner
  // (well outside the disc, must be the clear colour transparent black). The disc has
  // radius 0.6 in NDC, so a pixel 4 pixels in from the corner sits at NDC ~(-0.96, -0.96)
  // — comfortably outside.
  auto surface =
      Surface::MakeFrom(context, renderTexture->getBackendTexture(), ImageOrigin::TopLeft);
  ASSERT_TRUE(surface != nullptr);
  auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
  uint32_t centrePixel = 0;
  ASSERT_TRUE(surface->readPixels(info, &centrePixel, SIZE / 2, SIZE / 2));
  uint32_t cornerPixel = 0xDEADBEEF;
  ASSERT_TRUE(surface->readPixels(info, &cornerPixel, 4, 4));

  // RGBA_8888 byte order is R, G, B, A. The centre must read as opaque red; the corner must
  // read as the transparent-black clear colour.
  EXPECT_EQ(centrePixel, 0xFF0000FFu) << "centre pixel was not the opaque red cover colour";
  EXPECT_EQ(cornerPixel, 0u) << "corner pixel was not the clear colour";
}

// Verifies RenderTargetProxy::getStencil() lazily allocates a stencil texture the first
// time it is called and then returns the same instance on subsequent calls.
TGFX_TEST(GPURenderTest, RenderTargetProxyGetStencil) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto proxy = RenderTargetProxy::Make(context, 96, 64, /*alphaOnly=*/false);
  ASSERT_TRUE(proxy != nullptr);

  auto first = proxy->getStencil(1);
  ASSERT_TRUE(first != nullptr);
  EXPECT_EQ(first->width(), 96);
  EXPECT_EQ(first->height(), 64);
  EXPECT_EQ(first->sampleCount(), 1);

  auto second = proxy->getStencil(1);
  ASSERT_TRUE(second != nullptr);
  // Lazy-init contract: subsequent calls return the cached instance, not a fresh texture.
  EXPECT_EQ(first.get(), second.get());
}

// Verifies the setter/getter pair for ProgramInfo's stencil and colour-write-mask state, and
// that the colour write mask is propagated into the PipelineColorAttachment that
// GLSLProgramBuilder forwards to the backend. Without this, draw ops that need stencil-only
// passes (e.g. the bezier-rasterization stencil pass) could call setColorWriteMask(0) yet
// still emit colour writes downstream.
TGFX_TEST(GPURenderTest, ProgramInfoStencilAccessors) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto proxy = RenderTargetProxy::Make(context, 1, 1, /*alphaOnly=*/false);
  ASSERT_TRUE(proxy != nullptr);
  auto renderTarget = proxy->getRenderTarget();
  ASSERT_TRUE(renderTarget != nullptr);
  auto* allocator = context->drawingAllocator();
  auto geometryProcessor =
      DefaultGeometryProcessor::Make(allocator, {}, 1, 1, AAType::None, {}, {});
  ASSERT_TRUE(geometryProcessor != nullptr);
  std::vector<FragmentProcessor*> fragmentProcessors = {};
  ProgramInfo info(renderTarget.get(), geometryProcessor.get(), std::move(fragmentProcessors),
                   /*numColorProcessors=*/0, /*xferProcessor=*/nullptr, BlendMode::SrcOver);

  // Defaults: no-op stencil, all colour channels writable.
  EXPECT_EQ(info.getColorWriteMask(), ColorWriteMask::All);
  EXPECT_EQ(info.getDepthStencil().format, PixelFormat::Unknown);
  EXPECT_EQ(info.getPipelineColorAttachment().colorWriteMask, ColorWriteMask::All);

  info.setColorWriteMask(0);
  DepthStencilDescriptor stencilDS = {};
  stencilDS.format = PixelFormat::DEPTH24_STENCIL8;
  stencilDS.stencilFront.compare = CompareFunction::Equal;
  stencilDS.stencilFront.passOp = StencilOperation::Replace;
  stencilDS.stencilBack = stencilDS.stencilFront;
  info.setDepthStencil(stencilDS);

  EXPECT_EQ(info.getColorWriteMask(), 0u);
  EXPECT_EQ(info.getDepthStencil().format, PixelFormat::DEPTH24_STENCIL8);
  EXPECT_EQ(info.getDepthStencil().stencilFront.compare, CompareFunction::Equal);
  EXPECT_EQ(info.getDepthStencil().stencilFront.passOp, StencilOperation::Replace);
  // The colour write mask must reach the PipelineColorAttachment that the program builder
  // forwards to the backend.
  EXPECT_EQ(info.getPipelineColorAttachment().colorWriteMask, 0u);
}

// Verifies that ProgramInfo::getProgram()'s cache key distinguishes pipelines that share
// shaders but differ in colour write mask or stencil configuration. Otherwise the bezier
// rasterization stencil/cover passes would silently collapse onto a single cached program.
// This pins down the program-cache key encoding added for the stencil plumbing so any future
// regression there surfaces here, not at the much harder-to-diagnose draw call site.
TGFX_TEST(GPURenderTest, ProgramInfoStencilCacheKey) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto proxy = RenderTargetProxy::Make(context, 1, 1, /*alphaOnly=*/false);
  ASSERT_TRUE(proxy != nullptr);
  auto renderTarget = proxy->getRenderTarget();
  ASSERT_TRUE(renderTarget != nullptr);
  auto* allocator = context->drawingAllocator();

  // Each variant builds its own GeometryProcessor with the same parameters; the GP's key
  // contribution is content-addressed, so the three GPs hash identically and only the stencil
  // and colour-write-mask overlay should differentiate the cache lookups below.
  auto gpA = DefaultGeometryProcessor::Make(allocator, {}, 1, 1, AAType::None, {}, {});
  auto gpA2 = DefaultGeometryProcessor::Make(allocator, {}, 1, 1, AAType::None, {}, {});
  auto gpB = DefaultGeometryProcessor::Make(allocator, {}, 1, 1, AAType::None, {}, {});
  auto gpC = DefaultGeometryProcessor::Make(allocator, {}, 1, 1, AAType::None, {}, {});
  ASSERT_TRUE(gpA && gpA2 && gpB && gpC);

  DepthStencilDescriptor stencilDS = {};
  stencilDS.format = PixelFormat::DEPTH24_STENCIL8;
  stencilDS.stencilFront.compare = CompareFunction::Equal;
  stencilDS.stencilFront.passOp = StencilOperation::Replace;
  stencilDS.stencilBack = stencilDS.stencilFront;

  // Variant A: defaults.
  ProgramInfo infoA(renderTarget.get(), gpA.get(), {}, 0, nullptr, BlendMode::SrcOver);
  auto programA = infoA.getProgram();
  ASSERT_TRUE(programA != nullptr);

  // Variant A again: equivalent inputs must hit the same cached program.
  ProgramInfo infoA2(renderTarget.get(), gpA2.get(), {}, 0, nullptr, BlendMode::SrcOver);
  auto programA2 = infoA2.getProgram();
  EXPECT_EQ(programA2.get(), programA.get())
      << "identical ProgramInfo inputs should reuse the cached Program";

  // Variant B: differ only in colour write mask.
  ProgramInfo infoB(renderTarget.get(), gpB.get(), {}, 0, nullptr, BlendMode::SrcOver);
  infoB.setColorWriteMask(0);
  auto programB = infoB.getProgram();
  ASSERT_TRUE(programB != nullptr);
  EXPECT_NE(programB.get(), programA.get())
      << "differing colorWriteMask must not collapse onto the same cached Program";

  // Variant C: differ only in stencil configuration.
  ProgramInfo infoC(renderTarget.get(), gpC.get(), {}, 0, nullptr, BlendMode::SrcOver);
  infoC.setDepthStencil(stencilDS);
  auto programC = infoC.getProgram();
  ASSERT_TRUE(programC != nullptr);
  EXPECT_NE(programC.get(), programA.get())
      << "differing depthStencil must not collapse onto the same cached Program";
  EXPECT_NE(programC.get(), programB.get())
      << "differing depthStencil must not collapse with a colorWriteMask-only variant";
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

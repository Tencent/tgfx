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

#include "gpu/webgpu/WebGPUGPU.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/Texture.h"
#include "tgfx/gpu/webgpu/WebGPUTypes.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(WebGPUBackendTextureTest, LogicalSize) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "WebGPU backend not available";
  }
  auto gpu = static_cast<WebGPUGPU*>(context->gpu());
  ASSERT_TRUE(gpu != nullptr);

  const int physicalSize = 200;
  const int logicalSize = 100;

  TextureDescriptor descriptor(physicalSize, physicalSize, PixelFormat::RGBA_8888, false, 1,
                               TextureUsage::RENDER_ATTACHMENT | TextureUsage::TEXTURE_BINDING);
  auto texture = gpu->createTexture(descriptor);
  ASSERT_TRUE(texture != nullptr);

  {
    auto backendTexture = texture->getBackendTexture();
    WebGPUTextureInfo info = {};
    ASSERT_TRUE(backendTexture.getWebGPUTextureInfo(&info));
    BackendTexture logicalTexture(info, logicalSize, logicalSize);
    auto imported = gpu->importBackendTexture(
        logicalTexture, TextureUsage::RENDER_ATTACHMENT | TextureUsage::TEXTURE_BINDING, false);
    ASSERT_TRUE(imported != nullptr);
    EXPECT_EQ(imported->width(), logicalSize);
    EXPECT_EQ(imported->height(), logicalSize);
    auto roundTrip = imported->getBackendTexture();
    ASSERT_TRUE(roundTrip.isValid());
    EXPECT_EQ(roundTrip.width(), logicalSize);
    EXPECT_EQ(roundTrip.height(), logicalSize);
  }

  {
    auto backendRenderTarget = texture->getBackendRenderTarget();
    WebGPUTextureInfo info = {};
    ASSERT_TRUE(backendRenderTarget.getWebGPUTextureInfo(&info));
    BackendRenderTarget logicalRenderTarget(info, logicalSize, logicalSize);
    auto imported = gpu->importBackendRenderTarget(logicalRenderTarget);
    ASSERT_TRUE(imported != nullptr);
    EXPECT_EQ(imported->width(), logicalSize);
    EXPECT_EQ(imported->height(), logicalSize);
    auto roundTrip = imported->getBackendRenderTarget();
    ASSERT_TRUE(roundTrip.isValid());
    EXPECT_EQ(roundTrip.width(), logicalSize);
    EXPECT_EQ(roundTrip.height(), logicalSize);
  }
}

TGFX_TEST(WebGPUBackendTextureTest, SamplingOnlyTextureRejectsSurface) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "WebGPU backend not available";
  }
  auto gpu = static_cast<WebGPUGPU*>(context->gpu());
  auto texture = gpu->createTexture({16, 16, PixelFormat::RGBA_8888});
  ASSERT_TRUE(texture != nullptr);
  auto backendTexture = texture->getBackendTexture();
  EXPECT_TRUE(Image::MakeFrom(context, backendTexture) != nullptr);
  EXPECT_TRUE(Surface::MakeFrom(context, backendTexture, ImageOrigin::TopLeft) == nullptr);
  EXPECT_TRUE(gpu->importBackendTexture(backendTexture, TextureUsage::RENDER_ATTACHMENT) ==
              nullptr);

  WebGPUTextureInfo info = {};
  ASSERT_TRUE(backendTexture.getWebGPUTextureInfo(&info));
  BackendRenderTarget invalidTarget(info, 16, 16);
  EXPECT_TRUE(gpu->importBackendRenderTarget(invalidTarget) == nullptr);
  EXPECT_TRUE(Surface::MakeFrom(context, invalidTarget, ImageOrigin::TopLeft) == nullptr);
  auto nativeTexture = static_cast<WGPUTexture>(const_cast<void*>(info.texture));
  wgpuTextureReference(nativeTexture);
  EXPECT_TRUE(gpu->importBackendTexture(backendTexture, TextureUsage::RENDER_ATTACHMENT, true) ==
              nullptr);
  EXPECT_EQ(wgpuTextureGetWidth(nativeTexture), 16u);
  EXPECT_TRUE(Image::MakeFrom(context, backendTexture) != nullptr);
}

static void ExpectGreenSurface(const std::shared_ptr<Surface>& surface) {
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->clear(Color::Green());
  uint8_t pixel[4] = {};
  auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
  ASSERT_TRUE(surface->readPixels(info, pixel, 8, 8));
  EXPECT_EQ(pixel[0], 0);
  EXPECT_EQ(pixel[1], 255);
  EXPECT_EQ(pixel[2], 0);
  EXPECT_EQ(pixel[3], 255);
}

TGFX_TEST(WebGPUBackendTextureTest, AttachmentOnlyRenderTargetIsAccepted) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "WebGPU backend not available";
  }
  auto gpu = context->gpu();
  auto texture = gpu->createTexture(
      {16, 16, PixelFormat::RGBA_8888, false, 1, TextureUsage::RENDER_ATTACHMENT});
  ASSERT_TRUE(texture != nullptr);
  auto target = texture->getBackendRenderTarget();
  auto imported = gpu->importBackendRenderTarget(target);
  ASSERT_TRUE(imported != nullptr);
  EXPECT_EQ(imported->usage(), TextureUsage::RENDER_ATTACHMENT);
  EXPECT_FALSE(imported->getBackendTexture().isValid());
  WebGPUTextureInfo textureInfo = {};
  ASSERT_TRUE(target.getWebGPUTextureInfo(&textureInfo));
  BackendTexture invalidTexture(textureInfo, 16, 16);
  EXPECT_TRUE(Image::MakeFrom(context, invalidTexture) == nullptr);
  EXPECT_TRUE(Surface::MakeFrom(context, invalidTexture, ImageOrigin::TopLeft) == nullptr);
  ExpectGreenSurface(Surface::MakeFrom(context, target, ImageOrigin::TopLeft));
}

TGFX_TEST(WebGPUBackendTextureTest, RenderableTextureSurfaceIsAccepted) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "WebGPU backend not available";
  }
  auto texture = context->gpu()->createTexture(
      {16, 16, PixelFormat::RGBA_8888, false, 1,
       TextureUsage::TEXTURE_BINDING | TextureUsage::RENDER_ATTACHMENT});
  ASSERT_TRUE(texture != nullptr);
  ExpectGreenSurface(
      Surface::MakeFrom(context, texture->getBackendTexture(), ImageOrigin::TopLeft));
  ExpectGreenSurface(
      Surface::MakeFrom(context, texture->getBackendTexture(), ImageOrigin::BottomLeft, 4));
}

TGFX_TEST(WebGPUBackendTextureTest, SamplingImportDoesNotRequireCopyUsage) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "WebGPU backend not available";
  }
  auto gpu = static_cast<WebGPUGPU*>(context->gpu());
  WGPUTextureDescriptor descriptor = {};
  descriptor.dimension = WGPUTextureDimension_2D;
  descriptor.size = {16, 16, 1};
  descriptor.format = WGPUTextureFormat_RGBA8Unorm;
  descriptor.mipLevelCount = 1;
  descriptor.sampleCount = 1;
  descriptor.usage = WGPUTextureUsage_TextureBinding;
  auto texture = wgpuDeviceCreateTexture(gpu->device(), &descriptor);
  ASSERT_TRUE(texture != nullptr);
  WebGPUTextureInfo info = {};
  info.texture = texture;
  info.format = static_cast<uint32_t>(descriptor.format);
  BackendTexture backendTexture(info, 16, 16);
  {
    auto image = Image::MakeFrom(context, backendTexture);
    EXPECT_TRUE(image != nullptr);
    EXPECT_TRUE(Surface::MakeFrom(context, backendTexture, ImageOrigin::TopLeft) == nullptr);
  }
  gpu->processUnreferencedResources();
  wgpuTextureRelease(texture);
}

}  // namespace tgfx

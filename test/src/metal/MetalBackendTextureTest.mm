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

#include <Metal/Metal.h>
#include "gpu/metal/MetalGPU.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/metal/MetalTypes.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(MetalBackendTextureTest, PreservesLogicalSize) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  ASSERT_TRUE(gpu != nullptr);

  const int physicalSize = 1024;
  const int logicalWidth = 100;
  const int logicalHeight = 80;

  {
    MTLTextureDescriptor* descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:physicalSize
                                                          height:physicalSize
                                                       mipmapped:NO];
    id<MTLTexture> metalTexture = [gpu->device() newTextureWithDescriptor:descriptor];
    ASSERT_TRUE(metalTexture != nil);

    MetalTextureInfo metalInfo = {};
    metalInfo.texture = (__bridge const void*)metalTexture;
    metalInfo.format = static_cast<unsigned>(MTLPixelFormatRGBA8Unorm);

    BackendTexture backendTexture(metalInfo, logicalWidth, logicalHeight);
    auto texture = gpu->importBackendTexture(backendTexture, TextureUsage::TEXTURE_BINDING, true);
    ASSERT_TRUE(texture != nullptr);
    EXPECT_EQ(texture->width(), logicalWidth);
    EXPECT_EQ(texture->height(), logicalHeight);

    auto roundTrip = texture->getBackendTexture();
    ASSERT_TRUE(roundTrip.isValid());
    EXPECT_EQ(roundTrip.width(), logicalWidth);
    EXPECT_EQ(roundTrip.height(), logicalHeight);
  }

  {
    MTLTextureDescriptor* descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:physicalSize
                                                          height:physicalSize
                                                       mipmapped:NO];
    id<MTLTexture> metalTexture = [gpu->device() newTextureWithDescriptor:descriptor];
    ASSERT_TRUE(metalTexture != nil);

    MetalTextureInfo metalInfo = {};
    metalInfo.texture = (__bridge const void*)metalTexture;
    metalInfo.format = static_cast<unsigned>(MTLPixelFormatRGBA8Unorm);

    BackendRenderTarget backendRenderTarget(metalInfo, logicalWidth, logicalHeight);
    auto renderTarget = gpu->importBackendRenderTarget(backendRenderTarget);
    ASSERT_TRUE(renderTarget != nullptr);
    EXPECT_EQ(renderTarget->width(), logicalWidth);
    EXPECT_EQ(renderTarget->height(), logicalHeight);

    auto roundTrip = renderTarget->getBackendRenderTarget();
    ASSERT_TRUE(roundTrip.isValid());
    EXPECT_EQ(roundTrip.width(), logicalWidth);
    EXPECT_EQ(roundTrip.height(), logicalHeight);

    renderTarget.reset();
    [metalTexture release];
  }
}

}  // namespace tgfx

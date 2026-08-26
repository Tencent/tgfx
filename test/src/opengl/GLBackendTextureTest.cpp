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

#include "gpu/opengl/GLGPU.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/Texture.h"
#include "tgfx/gpu/opengl/GLTypes.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(GLBackendTextureTest, LogicalSize) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "OpenGL backend not available";
  }
  auto gpu = static_cast<GLGPU*>(context->gpu());
  ASSERT_TRUE(gpu != nullptr);

  const int physicalSize = 200;
  const int logicalSize = 100;

  TextureDescriptor descriptor(physicalSize, physicalSize, PixelFormat::RGBA_8888, false, 1,
                               TextureUsage::RENDER_ATTACHMENT | TextureUsage::TEXTURE_BINDING);
  auto texture = gpu->createTexture(descriptor);
  ASSERT_TRUE(texture != nullptr);

  {
    auto backendTexture = texture->getBackendTexture();
    GLTextureInfo info = {};
    ASSERT_TRUE(backendTexture.getGLTextureInfo(&info));
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
    GLFrameBufferInfo info = {};
    ASSERT_TRUE(backendRenderTarget.getGLFramebufferInfo(&info));
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

}  // namespace tgfx

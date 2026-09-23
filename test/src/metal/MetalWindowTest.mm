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
#include <QuartzCore/QuartzCore.h>
#include <cmath>
#include <memory>
#include "gpu/RenderContext.h"
#include "gpu/metal/MetalDrawableProxy.h"
#include "gpu/metal/MetalGPU.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Drawable.h"
#include "tgfx/gpu/metal/MetalWindow.h"
#include "utils/TestUtils.h"

namespace tgfx {

static bool NearlyMatches(const uint8_t* pixel, const Color& expected) {
  return std::abs(pixel[0] - static_cast<int>(expected.red * 255)) <= 1 &&
         std::abs(pixel[1] - static_cast<int>(expected.green * 255)) <= 1 &&
         std::abs(pixel[2] - static_cast<int>(expected.blue * 255)) <= 1 &&
         std::abs(pixel[3] - static_cast<int>(expected.alpha * 255)) <= 1;
}

static bool ReadCenterPixel(Drawable* drawable, const Color& expected) {
  auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
  uint8_t pixel[4] = {};
  if (!drawable->readPixels(info, pixel, drawable->width() / 2, drawable->height() / 2)) {
    return false;
  }
  return NearlyMatches(pixel, expected);
}

static CAMetalLayer* MakeTestLayer(id<MTLDevice> device, int width, int height) {
  auto layer = [CAMetalLayer layer];
  layer.device = device;
  layer.drawableSize = CGSizeMake(width, height);
  // Reading back blits from the drawable texture, which requires sample/blit access.
  layer.framebufferOnly = NO;
  layer.maximumDrawableCount = 3;
  return layer;
}

/**
 * Verifies that a Drawable acquired from Window::nextDrawable() can be rendered into via
 * Surface::MakeFrom() and read back after submission but before explicit presentation.
 */
TGFX_TEST(MetalWindowTest, ReadPixelsFromDrawable) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  ASSERT_TRUE(gpu != nullptr);

  constexpr int Width = 16;
  constexpr int Height = 16;
  auto layer = MakeTestLayer(gpu->device(), Width, Height);

  auto window = MetalWindow::MakeFrom(layer, nullptr, nullptr, false);
  ASSERT_TRUE(window != nullptr);
  auto drawable = window->nextDrawable(context);
  ASSERT_TRUE(drawable != nullptr);
  EXPECT_EQ(drawable->width(), Width);
  EXPECT_EQ(drawable->height(), Height);

  auto surface = Surface::MakeFrom(context, drawable);
  ASSERT_TRUE(surface != nullptr);
  auto yellow = Color::FromRGBA(255, 255, 0);
  surface->getCanvas()->clear(yellow);
  context->flushAndSubmit(true);

  EXPECT_TRUE(ReadCenterPixel(drawable.get(), yellow));
  drawable->present();
}

/**
 * Verifies that drawables rotate through the layer's pool without stalling when each one is
 * released after its frame, and that every frame reads back its own content. This renders more
 * frames than the drawable pool depth, so holding a drawable past the next acquire would
 * eventually make nextDrawable() block.
 */
TGFX_TEST(MetalWindowTest, DrawableRotation) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  ASSERT_TRUE(gpu != nullptr);

  auto layer = MakeTestLayer(gpu->device(), 16, 16);
  auto window = MetalWindow::MakeFrom(layer, nullptr, nullptr, false);
  ASSERT_TRUE(window != nullptr);

  Color frameColors[] = {Color::Red(),   Color::Green(), Color::Blue(),
                         Color::White(), Color::Black(), Color::FromRGBA(255, 255, 0)};
  for (const auto& color : frameColors) {
    @autoreleasepool {
      auto drawable = window->nextDrawable(context);
      ASSERT_TRUE(drawable != nullptr);
      auto surface = Surface::MakeFrom(context, drawable);
      ASSERT_TRUE(surface != nullptr);
      surface->getCanvas()->clear(color);
      context->flushAndSubmit(true);
      drawable->present();
      // Metal keeps the drawable readable after present() as long as it is held.
      EXPECT_TRUE(ReadCenterPixel(drawable.get(), color));
    }
  }
}

/**
 * Verifies that the automatic presentation path does not hold the drawable after present: the
 * drawable is returned to the layer's rotation pool right away, which keeps nextDrawable() from
 * stalling when the pool is shallow.
 */
TGFX_TEST(MetalWindowTest, AutoPresentDoesNotHoldDrawable) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  ASSERT_TRUE(gpu != nullptr);

  auto layer = MakeTestLayer(gpu->device(), 16, 16);
  auto window = MetalWindow::MakeFrom(layer, nullptr, nullptr, false);
  ASSERT_TRUE(window != nullptr);
  auto surface = Surface::MakeFrom(context, window);
  ASSERT_TRUE(surface != nullptr);

  surface->getCanvas()->clear(Color::Red());
  context->flushAndSubmit(true);

  auto proxy = std::static_pointer_cast<MetalDrawableProxy>(surface->renderContext->renderTarget);
  ASSERT_TRUE(proxy != nullptr);
  EXPECT_TRUE(proxy->getMetalDrawable() == nil);
}

}  // namespace tgfx

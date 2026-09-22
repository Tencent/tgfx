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
//  License is distributed on an "as IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <Metal/Metal.h>
#include <QuartzCore/QuartzCore.h>
#include <cmath>
#include <memory>
#include "gpu/metal/MetalDrawableProxy.h"
#include "gpu/metal/MetalGPU.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/metal/MetalWindow.h"
#include "utils/TestUtils.h"

namespace tgfx {

static bool NearlyMatches(const RGBA4f<AlphaType::Premultiplied>& pixel, const Color& expected) {
  auto delta =
      std::max({std::abs(pixel.red - expected.red), std::abs(pixel.green - expected.green),
                std::abs(pixel.blue - expected.blue), std::abs(pixel.alpha - expected.alpha)});
  return delta <= (1.0f / 255.0f);
}

/**
 * Reproduces the drawable lifecycle issue of MetalWindow: MetalDrawableProxy::getRenderTarget()
 * acquires a drawable from the CAMetalLayer rotation pool, while MetalWindow::onPresent() hands
 * the drawable back to the pool immediately after the command buffer is submitted. A
 * readPixels() between two frames then re-acquires an arbitrary drawable from the pool, so the
 * readback returns whichever stale frame happens to be in rotation (or an untouched drawable)
 * instead of the last presented frame.
 */
TGFX_TEST(MetalWindowTest, ReadPixelsAfterPresent) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  ASSERT_TRUE(gpu != nullptr);

  constexpr int Width = 16;
  constexpr int Height = 16;
  auto layer = [CAMetalLayer layer];
  layer.device = gpu->device();
  layer.drawableSize = CGSizeMake(Width, Height);
  // The readback blits from the drawable texture, which requires sample/blit access.
  layer.framebufferOnly = NO;
  layer.maximumDrawableCount = 3;

  auto window = MetalWindow::MakeFrom(layer, nullptr, nullptr, false);
  ASSERT_TRUE(window != nullptr);
  auto surface = Surface::MakeFrom(context, window);
  ASSERT_TRUE(surface != nullptr);

  // Render more frames than the drawable pool depth, each filled with a distinct color, so every
  // drawable in the rotation pool holds stale content when the loop ends.
  Color frameColors[] = {Color::Red(),   Color::Green(), Color::Blue(),
                         Color::White(), Color::Black(), Color::FromRGBA(255, 255, 0)};
  constexpr size_t FrameCount = sizeof(frameColors) / sizeof(frameColors[0]);
  for (const auto& color : frameColors) {
    surface->getCanvas()->clear(color);
    context->flushAndSubmit(true);
  }
  auto& lastFrameColor = frameColors[FrameCount - 1];

  auto proxy = std::static_pointer_cast<MetalDrawableProxy>(window->drawableProxy);
  ASSERT_TRUE(proxy != nullptr);
  auto drawableAfterPresent = proxy->getMetalDrawable();
  auto pixel = surface->getColor(Width / 2, Height / 2);
  auto drawableAfterReadback = proxy->getMetalDrawable();

  EXPECT_TRUE(NearlyMatches(pixel, lastFrameColor))
      << "readPixels() between frames did not return the last presented frame, got rgba("
      << pixel.red << ", " << pixel.green << ", " << pixel.blue << ", " << pixel.alpha
      << "); drawable held after present: " << (drawableAfterPresent == nil ? "no" : "yes")
      << "; drawable re-acquired by the readback: "
      << (drawableAfterReadback == nil ? "no" : "yes");
}

}  // namespace tgfx

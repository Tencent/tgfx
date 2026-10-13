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
#include "gpu/DrawingManager.h"
#include "gpu/RenderContext.h"
#include "gpu/metal/MetalCommandQueue.h"
#include "gpu/metal/MetalDrawableProxy.h"
#include "gpu/metal/MetalGPU.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/SurfaceReadback.h"
#include "tgfx/gpu/metal/MetalWindow.h"
#include "utils/TestUtils.h"

namespace tgfx {

static bool NearlyMatches(const uint8_t* pixel, const Color& expected) {
  return std::abs(pixel[0] - static_cast<int>(expected.red * 255)) <= 1 &&
         std::abs(pixel[1] - static_cast<int>(expected.green * 255)) <= 1 &&
         std::abs(pixel[2] - static_cast<int>(expected.blue * 255)) <= 1 &&
         std::abs(pixel[3] - static_cast<int>(expected.alpha * 255)) <= 1;
}

static bool ReadCenterPixel(Surface* surface, const Color& expected) {
  auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
  uint8_t pixel[4] = {};
  if (!surface->readPixels(info, pixel, surface->width() / 2, surface->height() / 2)) {
    return false;
  }
  return NearlyMatches(pixel, expected);
}

static bool ReadbackMatches(Context* context, SurfaceReadback* readback, const Color& expected) {
  auto pixels = readback->lockPixels(context);
  if (pixels == nullptr) {
    return false;
  }
  auto bytes = static_cast<const uint8_t*>(pixels);
  uint8_t rgba[4] = {bytes[0], bytes[1], bytes[2], bytes[3]};
  if (readback->info().colorType() == ColorType::BGRA_8888) {
    rgba[0] = bytes[2];
    rgba[2] = bytes[0];
  }
  readback->unlockPixels(context);
  return NearlyMatches(rgba, expected);
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

static MetalDrawableProxy* SurfaceProxy(Surface* surface) {
  return static_cast<MetalDrawableProxy*>(surface->renderContext->renderTarget.get());
}

/**
 * Verifies the synchronous readback of a window surface: reading back after drawing the frame
 * and before the batch is flushed returns exactly the drawn content. readPixels() submits the
 * batch, which also presents the frame, so a second read without drawing again is rejected
 * instead of resolving another frame from the layer's pool.
 */
TGFX_TEST(MetalWindowTest, ReadPixelsInDrawingBatch) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto window =
      MetalWindow::MakeFrom(MakeTestLayer(gpu->device(), 16, 16), nullptr, nullptr, false);
  ASSERT_TRUE(window != nullptr);
  EXPECT_TRUE(window->supportsReadback());
  auto surface = Surface::MakeFrom(context, window);
  ASSERT_TRUE(surface != nullptr);

  auto yellow = Color::FromRGBA(255, 255, 0);
  surface->getCanvas()->clear(yellow);
  EXPECT_TRUE(ReadCenterPixel(surface.get(), yellow));
  // The read submitted and presented the frame.
  EXPECT_TRUE(SurfaceProxy(surface.get())->getMetalDrawable() == nil);
  EXPECT_FALSE(ReadCenterPixel(surface.get(), yellow));
}

/**
 * Verifies the asynchronous readback of a window surface: a readback issued after drawing and
 * before the batch is flushed is copied in the same submission that presents the frame, ordered
 * after the drawing, so its result stays valid after the presentation.
 */
TGFX_TEST(MetalWindowTest, AsyncReadbackOutlivesPresentation) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto window =
      MetalWindow::MakeFrom(MakeTestLayer(gpu->device(), 16, 16), nullptr, nullptr, false);
  ASSERT_TRUE(window != nullptr);
  auto surface = Surface::MakeFrom(context, window);
  ASSERT_TRUE(surface != nullptr);

  surface->getCanvas()->clear(Color::Blue());
  auto readback = surface->asyncReadPixels(Rect::MakeXYWH(8, 8, 1, 1));
  ASSERT_TRUE(readback != nullptr);
  context->flushAndSubmit(true);
  EXPECT_TRUE(SurfaceProxy(surface.get())->getMetalDrawable() == nil);
  EXPECT_TRUE(ReadbackMatches(context, readback.get(), Color::Blue()));
}

/**
 * Verifies that a window surface rejects readbacks once the batch that drew its frame has been
 * flushed, whether or not it has been submitted yet: the frame belongs to the display from that
 * point on, and reading would resolve a different frame. Drawing again opens a new frame that is
 * readable within its batch.
 */
TGFX_TEST(MetalWindowTest, ReadbackAfterFlushIsRejected) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto window =
      MetalWindow::MakeFrom(MakeTestLayer(gpu->device(), 16, 16), nullptr, nullptr, false);
  ASSERT_TRUE(window != nullptr);
  auto surface = Surface::MakeFrom(context, window);
  ASSERT_TRUE(surface != nullptr);

  surface->getCanvas()->clear(Color::Red());
  auto recording = context->flush();
  ASSERT_TRUE(recording != nullptr);
  EXPECT_TRUE(surface->asyncReadPixels(Rect::MakeWH(1, 1)) == nullptr);
  context->submit(std::move(recording), true);
  EXPECT_TRUE(surface->asyncReadPixels(Rect::MakeWH(1, 1)) == nullptr);

  surface->getCanvas()->clear(Color::Green());
  EXPECT_TRUE(ReadCenterPixel(surface.get(), Color::Green()));
}

/**
 * Verifies that frames rotate through the layer's drawable pool without stalling: every batch
 * presents its frame and returns the drawable right away, so rendering more frames than the pool
 * holds never blocks, and every frame reads back its own content.
 */
TGFX_TEST(MetalWindowTest, FrameRotation) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto window =
      MetalWindow::MakeFrom(MakeTestLayer(gpu->device(), 16, 16), nullptr, nullptr, false);
  ASSERT_TRUE(window != nullptr);
  auto surface = Surface::MakeFrom(context, window);
  ASSERT_TRUE(surface != nullptr);

  Color frameColors[] = {Color::Red(),   Color::Green(), Color::Blue(),
                         Color::White(), Color::Black(), Color::FromRGBA(255, 255, 0)};
  for (const auto& color : frameColors) {
    @autoreleasepool {
      surface->getCanvas()->clear(color);
      EXPECT_TRUE(ReadCenterPixel(surface.get(), color));
      EXPECT_TRUE(SurfaceProxy(surface.get())->getMetalDrawable() == nil);
    }
  }
}

/**
 * Verifies that a window without readback support (framebufferOnly = YES, the Metal default)
 * still renders and presents normally: only the readback itself is rejected.
 */
TGFX_TEST(MetalWindowTest, NonReadableWindowStillRenders) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto layer = [CAMetalLayer layer];
  layer.device = gpu->device();
  layer.drawableSize = CGSizeMake(16, 16);
  layer.framebufferOnly = YES;
  auto window = MetalWindow::MakeFrom(layer, nullptr, nullptr, false);
  ASSERT_TRUE(window != nullptr);
  EXPECT_FALSE(window->supportsReadback());
  auto surface = Surface::MakeFrom(context, window);
  ASSERT_TRUE(surface != nullptr);

  surface->getCanvas()->clear(Color::Red());
  EXPECT_TRUE(surface->asyncReadPixels(Rect::MakeWH(1, 1)) == nullptr);
  auto recording = context->flush();
  EXPECT_TRUE(recording != nullptr);
  context->submit(std::move(recording), true);
  EXPECT_TRUE(SurfaceProxy(surface.get())->getMetalDrawable() == nil);
}

/**
 * Verifies that all surfaces of a window share one frame within a batch and that the frame is
 * presented once at the end of the batch, returning the drawable to the pool.
 */
TGFX_TEST(MetalWindowTest, SurfacesShareFrameWithinBatch) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto window =
      MetalWindow::MakeFrom(MakeTestLayer(gpu->device(), 16, 16), nullptr, nullptr, false);
  ASSERT_TRUE(window != nullptr);

  auto firstSurface = Surface::MakeFrom(context, window);
  auto secondSurface = Surface::MakeFrom(context, window);
  ASSERT_TRUE(firstSurface != nullptr);
  ASSERT_TRUE(secondSurface != nullptr);
  EXPECT_EQ(SurfaceProxy(firstSurface.get()), SurfaceProxy(secondSurface.get()));
  firstSurface->getCanvas()->clear(Color::Red());
  secondSurface->getCanvas()->clear(Color::Blue());
  EXPECT_EQ(context->drawingManager()->currentBuffer->presentables.size(), 1u);
  context->flushAndSubmit(true);
  EXPECT_TRUE(SurfaceProxy(firstSurface.get())->getMetalDrawable() == nil);
}

/**
 * Verifies the frame-retention contract of flushed-but-unsubmitted work: the Recording keeps
 * the window alive, and dropping it releases the window while the buffered rendering commands
 * still run; Metal presents through the frame's proxy without the window. The proxies hold their
 * windows weakly, so this retention never forms a device reference cycle.
 */
TGFX_TEST(MetalWindowTest, DroppedRecordingReleasesWindow) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto window =
      MetalWindow::MakeFrom(MakeTestLayer(gpu->device(), 16, 16), nullptr, nullptr, false);
  ASSERT_TRUE(window != nullptr);

  auto surface = Surface::MakeFrom(context, window);
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->clear(Color::Red());
  auto recording = context->flush();
  ASSERT_TRUE(recording != nullptr);

  std::weak_ptr<Window> weakWindow = window;
  surface = nullptr;
  window = nullptr;
  EXPECT_FALSE(weakWindow.expired());
  recording = nullptr;
  EXPECT_TRUE(weakWindow.expired());
  auto offscreen = Surface::Make(context, 8, 8);
  ASSERT_TRUE(offscreen != nullptr);
  offscreen->getCanvas()->clear(Color::Black());
  context->flushAndSubmit(true);
}

/**
 * Verifies that the factory-created default device stays alive: MetalWindow::MakeFrom(layer)
 * without an explicit device creates a TGFX MetalDevice that has no other owner, and callers
 * rely on window->getDevice() to keep it alive.
 */
TGFX_TEST(MetalWindowTest, FactoryDefaultDeviceStaysAlive) {
  auto layer = [CAMetalLayer layer];
  layer.device = MTLCreateSystemDefaultDevice();
  layer.drawableSize = CGSizeMake(16, 16);
  auto window = MetalWindow::MakeFrom(layer);
  ASSERT_TRUE(window != nullptr);
  auto device = window->getDevice();
  EXPECT_TRUE(device != nullptr);
  auto context = device->lockContext();
  EXPECT_TRUE(context != nullptr);
  device->unlock();
}

}  // namespace tgfx

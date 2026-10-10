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
#include "core/DrawableSurface.h"
#include "gpu/RenderContext.h"
#include "gpu/metal/MetalCommandQueue.h"
#include "gpu/metal/MetalDrawableProxy.h"
#include "gpu/metal/MetalGPU.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/SurfaceReadback.h"
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

static bool ReadCenterPixel(Surface* surface, const Color& expected) {
  auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
  uint8_t pixel[4] = {};
  if (!surface->readPixels(info, pixel, surface->width() / 2, surface->height() / 2)) {
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
 * Verifies that a Drawable acquired from Window::nextDrawable() can be imported via
 * Surface::MakeFrom(), read back after submission but before presentation, and presented through
 * Context::present(). Reading back after the presentation is rejected because the frame has been
 * delivered.
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
  EXPECT_TRUE(window->supportsReadback());

  auto drawable = window->nextDrawable();
  ASSERT_TRUE(drawable != nullptr);
  EXPECT_EQ(drawable->width(), Width);
  EXPECT_EQ(drawable->height(), Height);

  auto surface = Surface::MakeFrom(context, drawable);
  ASSERT_TRUE(surface != nullptr);
  auto yellow = Color::FromRGBA(255, 255, 0);
  surface->getCanvas()->clear(yellow);
  context->flushAndSubmit(true);

  EXPECT_TRUE(ReadCenterPixel(surface.get(), yellow));
  context->present(drawable);
  // The frame has been presented; further readback is not defined and is rejected.
  EXPECT_TRUE(surface->asyncReadPixels(Rect::MakeWH(1, 1)) == nullptr);
}

/**
 * Verifies that drawables rotate through the layer's pool without stalling when each one is
 * presented and released with its frame, and that every frame reads back its own content. This
 * renders more frames than the drawable pool depth, so holding a drawable past the next acquire
 * would eventually make nextDrawable() block.
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
      auto drawable = window->nextDrawable();
      ASSERT_TRUE(drawable != nullptr);
      auto surface = Surface::MakeFrom(context, drawable);
      ASSERT_TRUE(surface != nullptr);
      surface->getCanvas()->clear(color);
      context->flushAndSubmit(true);
      // Read back inside the portable window: after submission, before presentation.
      EXPECT_TRUE(ReadCenterPixel(surface.get(), color));
      context->present(drawable);
    }
  }
}

/**
 * Verifies that a Drawable whose frame was submitted but never presented is safely discarded
 * when dropped, and that the window keeps providing frames afterwards.
 */
TGFX_TEST(MetalWindowTest, DroppedDrawableDiscardsFrame) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto layer = MakeTestLayer(gpu->device(), 16, 16);
  auto window = MetalWindow::MakeFrom(layer, nullptr, nullptr, false);
  ASSERT_TRUE(window != nullptr);

  {
    auto drawable = window->nextDrawable();
    ASSERT_TRUE(drawable != nullptr);
    auto surface = Surface::MakeFrom(context, drawable);
    ASSERT_TRUE(surface != nullptr);
    surface->getCanvas()->clear(Color::Red());
    context->flushAndSubmit(true);
    // Drop both without presenting; the frame is discarded, not presented.
  }
  auto next = window->nextDrawable();
  EXPECT_TRUE(next != nullptr);
}

/**
 * Verifies the PresentRequested path: present() registered before the rendering is submitted
 * rides along with the submission that carries the rendering commands, and registering the
 * presentation ends the readback window.
 */
TGFX_TEST(MetalWindowTest, PresentBeforeSubmit) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto layer = MakeTestLayer(gpu->device(), 16, 16);
  auto window = MetalWindow::MakeFrom(layer, nullptr, nullptr, false);
  ASSERT_TRUE(window != nullptr);

  auto drawable = window->nextDrawable();
  ASSERT_TRUE(drawable != nullptr);
  auto surface = Surface::MakeFrom(context, drawable);
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->clear(Color::Blue());
  // Register the presentation before the rendering is submitted.
  context->present(drawable);
  // A registered presentation ends the readback window.
  EXPECT_TRUE(surface->asyncReadPixels(Rect::MakeWH(1, 1)) == nullptr);
  // The submission carries both the rendering and the presentation.
  context->flushAndSubmit(true);
  // The frame was delivered; the window keeps providing frames.
  EXPECT_TRUE(window->nextDrawable() != nullptr);
}

/**
 * Verifies that present() after a scheduled-but-unsubmitted readback submits the pending
 * readback transfer before presenting the frame, so the readback observes the frame's content.
 */
TGFX_TEST(MetalWindowTest, ReadbackBeforePresent) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto layer = MakeTestLayer(gpu->device(), 16, 16);
  auto window = MetalWindow::MakeFrom(layer, nullptr, nullptr, false);
  ASSERT_TRUE(window != nullptr);

  auto drawable = window->nextDrawable();
  ASSERT_TRUE(drawable != nullptr);
  auto surface = Surface::MakeFrom(context, drawable);
  ASSERT_TRUE(surface != nullptr);
  auto green = Color::Green();
  surface->getCanvas()->clear(green);
  context->flushAndSubmit(true);

  // Schedule a readback without submitting it.
  auto readback = surface->asyncReadPixels(Rect::MakeXYWH(8, 8, 1, 1));
  ASSERT_TRUE(readback != nullptr);

  // Present submits the pending readback first, then presents the frame.
  context->present(drawable);
  // The pending work was consumed by present().
  EXPECT_FALSE(context->flushAndSubmit());

  // The readback completed with the frame's content.
  auto pixels = readback->lockPixels(context);
  ASSERT_TRUE(pixels != nullptr);
  auto bytes = static_cast<const uint8_t*>(pixels);
  uint8_t rgba[4] = {};
  if (readback->info().colorType() == ColorType::BGRA_8888) {
    rgba[0] = bytes[2];
    rgba[1] = bytes[1];
    rgba[2] = bytes[0];
    rgba[3] = bytes[3];
  } else {
    rgba[0] = bytes[0];
    rgba[1] = bytes[1];
    rgba[2] = bytes[2];
    rgba[3] = bytes[3];
  }
  EXPECT_TRUE(NearlyMatches(rgba, green));
  readback->unlockPixels(context);
}

/**
 * Verifies that a presentation request binds to the recording that carries the frame's
 * rendering: after present() but before that recording is submitted, the queue must not hold
 * any pending presentation, so an earlier unrelated recording (flushed but not yet submitted)
 * cannot consume and present the frame before its rendering commands run.
 */
TGFX_TEST(MetalWindowTest, PresentBindsToFrameRecording) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto layer = MakeTestLayer(gpu->device(), 16, 16);
  auto window = MetalWindow::MakeFrom(layer, nullptr, nullptr, false);
  ASSERT_TRUE(window != nullptr);

  // Flush an unrelated offscreen recording (R0) without submitting it.
  auto offscreen = Surface::Make(context, 16, 16);
  ASSERT_TRUE(offscreen != nullptr);
  offscreen->getCanvas()->clear(Color::Red());
  auto recording = context->flush();
  ASSERT_TRUE(recording != nullptr);

  // Draw a drawable frame and register its presentation before any submission.
  auto drawable = window->nextDrawable();
  ASSERT_TRUE(drawable != nullptr);
  auto surface = Surface::MakeFrom(context, drawable);
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->clear(Color::Green());
  context->present(drawable);

  // The presentation request must not sit on the queue while R0 is still unsubmitted;
  // otherwise R0's command buffer would consume and present the frame.
  auto queue = static_cast<MetalCommandQueue*>(context->gpu()->queue());
  EXPECT_TRUE(queue->pendingDrawables.empty());

  // Submitting R0 first must not present the frame.
  context->submit(std::move(recording));
  EXPECT_TRUE(queue->pendingDrawables.empty());

  // Submitting the frame's own recording schedules the presentation with it and delivers it.
  context->flushAndSubmit(true);
  EXPECT_TRUE(surface->asyncReadPixels(Rect::MakeWH(1, 1)) == nullptr);
}

/**
 * Verifies the frame-retention contract of flushed-but-unsubmitted work: the pending drawing
 * buffer keeps the frame (and its window) alive until it is submitted, so dropping a Recording
 * never discards a presentation whose rendering commands still run; the window is released only
 * after the buffer is submitted. The Window holds its device weakly, so this retention never
 * forms a Device -> Context -> buffer -> frame -> window -> Device cycle.
 */
TGFX_TEST(MetalWindowTest, DroppedRecordingReleasesWindow) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto layer = MakeTestLayer(gpu->device(), 16, 16);
  auto window = MetalWindow::MakeFrom(layer, nullptr, nullptr, false);
  ASSERT_TRUE(window != nullptr);

  auto surface = Surface::MakeFrom(context, window);
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->clear(Color::Red());
  auto recording = context->flush();
  ASSERT_TRUE(recording != nullptr);

  std::weak_ptr<Window> weakWindow = window;
  surface = nullptr;
  window = nullptr;
  EXPECT_FALSE(weakWindow.expired());  // the Recording retains the work
  // Dropping the Recording does not drop the pending work: the buffered frame keeps the window
  // alive until its rendering and presentation are submitted.
  recording = nullptr;
  EXPECT_FALSE(weakWindow.expired());
  // Any later submission drains the pending queue in FIFO order, delivering the frame and
  // releasing the window.
  auto offscreen = Surface::Make(context, 8, 8);
  ASSERT_TRUE(offscreen != nullptr);
  offscreen->getCanvas()->clear(Color::Black());
  context->flushAndSubmit(true);
  EXPECT_TRUE(weakWindow.expired());
}

/**
 * Verifies that a window without readback support (framebufferOnly = YES, the Metal default)
 * still renders normally through the drawable path: rendering must not be gated by the
 * readback capability, only readback itself is.
 */
TGFX_TEST(MetalWindowTest, NonReadableFrameStillRenders) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto layer = [CAMetalLayer layer];
  layer.device = gpu->device();
  layer.drawableSize = CGSizeMake(16, 16);
  layer.framebufferOnly = YES;  // no readback support, but perfectly renderable
  auto window = MetalWindow::MakeFrom(layer, nullptr, nullptr, false);
  ASSERT_TRUE(window != nullptr);
  EXPECT_FALSE(window->supportsReadback());

  auto drawable = window->nextDrawable();
  ASSERT_TRUE(drawable != nullptr);
  auto surface = Surface::MakeFrom(context, drawable);
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->clear(Color::Red());
  // Readback is cleanly rejected (capability), but the drawing must have been recorded.
  EXPECT_TRUE(surface->asyncReadPixels(Rect::MakeWH(1, 1)) == nullptr);
  auto recording = context->flush();
  EXPECT_TRUE(recording != nullptr);
  context->submit(std::move(recording), true);
  context->present(drawable);
}

/**
 * Verifies that a presentation is delivered by the drawing batch that last collected the
 * frame, not by the earliest one: the frame's second batch (recording B, holding the last
 * drawing and the readback transfer) must schedule the presentation, so the presentation is
 * ordered after all of the frame's recorded commands.
 */
TGFX_TEST(MetalWindowTest, PresentationBindsToLastBatch) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto layer = MakeTestLayer(gpu->device(), 16, 16);
  auto window = MetalWindow::MakeFrom(layer, nullptr, nullptr, false);
  ASSERT_TRUE(window != nullptr);

  auto drawable = window->nextDrawable();
  ASSERT_TRUE(drawable != nullptr);
  auto surface = Surface::MakeFrom(context, drawable);
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->clear(Color::Red());
  auto recordingA = context->flush();
  ASSERT_TRUE(recordingA != nullptr);

  // Second batch: more drawing and a readback scheduled before the presentation request.
  auto green = Color::Green();
  surface->getCanvas()->clear(green);
  auto readback = surface->asyncReadPixels(Rect::MakeXYWH(8, 8, 1, 1));
  ASSERT_TRUE(readback != nullptr);
  context->present(drawable);

  // Submitting the earlier batch must not deliver the presentation.
  auto raw = drawable.get();
  context->submit(std::move(recordingA), true);
  EXPECT_TRUE(raw->_delivery == Drawable::Delivery::PresentRequested);

  // The last batch delivers the presentation, after its own drawing and readback transfer.
  context->flushAndSubmit(true);
  EXPECT_TRUE(raw->_delivery == Drawable::Delivery::Presented);

  auto pixels = readback->lockPixels(context);
  ASSERT_TRUE(pixels != nullptr);
  auto bytes = static_cast<const uint8_t*>(pixels);
  uint8_t rgba[4] = {};
  if (readback->info().colorType() == ColorType::BGRA_8888) {
    rgba[0] = bytes[2];
    rgba[1] = bytes[1];
    rgba[2] = bytes[0];
    rgba[3] = bytes[3];
  } else {
    rgba[0] = bytes[0];
    rgba[1] = bytes[1];
    rgba[2] = bytes[2];
    rgba[3] = bytes[3];
  }
  EXPECT_TRUE(NearlyMatches(rgba, green));
  readback->unlockPixels(context);
}

TGFX_TEST(MetalWindowTest, SurfaceRetainsDrawable) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto layer = MakeTestLayer(gpu->device(), 16, 16);
  auto window = MetalWindow::MakeFrom(layer, nullptr, nullptr, false);
  ASSERT_TRUE(window != nullptr);

  auto surface = Surface::MakeFrom(context, window->nextDrawable());
  ASSERT_TRUE(surface != nullptr);
  std::weak_ptr<Drawable> weakDrawable =
      std::static_pointer_cast<DrawableSurface>(surface)->getDrawable();
  EXPECT_FALSE(weakDrawable.expired());
  surface->getCanvas()->clear(Color::Red());
  context->flushAndSubmit(true);
  surface = nullptr;
  EXPECT_TRUE(weakDrawable.expired());
}

TGFX_TEST(MetalWindowTest, DrawableRetainsWindow) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto layer = MakeTestLayer(gpu->device(), 16, 16);
  auto window = MetalWindow::MakeFrom(layer, nullptr, nullptr, false);
  ASSERT_TRUE(window != nullptr);

  std::weak_ptr<Window> weakWindow = window;
  auto drawable = window->nextDrawable();
  ASSERT_TRUE(drawable != nullptr);
  auto surface = Surface::MakeFrom(context, drawable);
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->clear(Color::Green());
  context->flushAndSubmit(true);

  window = nullptr;
  surface = nullptr;
  // The undelivered frame keeps its window alive.
  EXPECT_FALSE(weakWindow.expired());
  // Presenting the frame delivers it and releases the frame's window reference.
  context->present(drawable);
  EXPECT_TRUE(weakWindow.expired());
  drawable = nullptr;
  EXPECT_TRUE(weakWindow.expired());
}

TGFX_TEST(MetalWindowTest, PresentsEachSurfaceRenderTarget) {
  ContextScope scope;
  auto context = scope.getContext();
  if (context == nullptr) {
    GTEST_SKIP() << "Metal backend not available";
  }
  auto gpu = static_cast<MetalGPU*>(context->gpu());
  auto layer = MakeTestLayer(gpu->device(), 16, 16);
  auto window = MetalWindow::MakeFrom(layer, nullptr, nullptr, false);
  ASSERT_TRUE(window != nullptr);

  auto firstSurface = Surface::MakeFrom(context, window);
  auto secondSurface = Surface::MakeFrom(context, window);
  ASSERT_TRUE(firstSurface != nullptr);
  ASSERT_TRUE(secondSurface != nullptr);
  auto firstProxy =
      std::static_pointer_cast<MetalDrawableProxy>(firstSurface->renderContext->renderTarget);
  auto secondProxy =
      std::static_pointer_cast<MetalDrawableProxy>(secondSurface->renderContext->renderTarget);
  firstSurface->getCanvas()->clear(Color::Red());
  secondSurface->getCanvas()->clear(Color::Blue());
  context->flushAndSubmit(true);

  EXPECT_TRUE(firstProxy->getMetalDrawable() == nil);
  EXPECT_TRUE(secondProxy->getMetalDrawable() == nil);
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

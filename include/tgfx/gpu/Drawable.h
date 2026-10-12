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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include "tgfx/core/ColorSpace.h"

namespace tgfx {

class Context;
class RenderTargetProxy;
class Window;

/**
 * Drawable represents the usage right of a single displayable frame of a Window. It carries only
 * the frame identity and its delivery state: it holds no drawing Context and never references a
 * Surface. The frame is imported into a Context via Surface::MakeFrom(context, drawable), which
 * resolves backend targets that need a current context, and is presented via
 * Context::present(drawable), which registers the presentation on the current recording batch;
 * the presentation is executed when that batch is submitted. Readback goes through
 * Surface::asyncReadPixels() until a presentation is registered or the frame is presented, and
 * only if the frame's window supports readback (see Window::supportsReadback()). Dropping a
 * Drawable without presenting it discards the frame; backends release it within their
 * device-synchronized scopes (see the backend Resource onRelease() mechanisms).
 */
class Drawable {
 public:
  virtual ~Drawable() = default;

  /**
   * Returns the width of the frame in pixels.
   */
  int width() const {
    return _width;
  }

  /**
   * Returns the height of the frame in pixels.
   */
  int height() const {
    return _height;
  }

  /**
   * Returns the color space associated with this frame, inherited from its Window. Returns
   * nullptr for the default sRGB.
   */
  std::shared_ptr<ColorSpace> colorSpace() const {
    return _colorSpace;
  }

 protected:
  Drawable(int width, int height, std::shared_ptr<ColorSpace> colorSpace);

  /**
   * Imports the frame target into the given Context. Called by Surface::MakeFrom() while the
   * Context is locked and current, so backends that need a current context (GL, EAGL) resolve
   * their targets here. Returns nullptr if the frame cannot be imported right now.
   */
  virtual std::shared_ptr<RenderTargetProxy> onImport(Context* context) = 0;

  /**
   * Discards the frame without presenting it. Implementations must not issue GPU calls from
   * arbitrary threads; backend cleanup runs within the device's synchronized scope (for example
   * through the Resource onRelease() mechanisms). Only backends with independent discard duties
   * (Vulkan's swapchain out-of-date recovery) override this.
   */
  virtual void onAbandon();

  /**
   * Must be invoked by subclass destructors to discard an undelivered frame, since the base class
   * cannot dispatch to onAbandon() while it is being destroyed.
   */
  void abandon();

  // The window is held weakly: a frame must not keep its window (and through it the device
  // and the context with its pending drawing buffers) alive, which would form a reference
  // cycle (buffer -> frame -> window -> device -> context -> buffer). While the frame's
  // surface or recording is alive the weak reference resolves; frame-level backends present
  // through their proxies and do not need the window at delivery at all.
  std::weak_ptr<Window> _window = {};
  std::shared_ptr<RenderTargetProxy> _importedTarget = nullptr;

 private:
  friend class Context;
  friend class DrawableSurface;
  friend class DrawingBuffer;
  friend class DrawingManager;
  friend class Surface;
  friend class Window;

  enum class Delivery {
    Acquired,
    Imported,
    Submitted,
    PresentRequested,
    Presented,
  };

  // Imports the frame while the Context is locked. Returns nullptr if the frame was already
  // imported or the import failed.
  std::shared_ptr<RenderTargetProxy> import(Context* context);

  // Returns true when readback may still be scheduled for this frame. This involves both
  // the frame state and the window's readback capability; see isFrameOpen() for a pure
  // frame-state check.
  bool canReadBack() const;

  // Returns true while the frame accepts more drawing, that is, before a presentation has been
  // registered for it. Unlike canReadBack(), this does not involve the window's readback
  // capability: a frame can be perfectly renderable on a window that does not support readback.
  bool isFrameOpen() const;

  // Registers a presentation request for the frame and closes it for further drawing. Returns
  // false on an invalid delivery state; the caller (Context::present()) records the request on
  // the current recording batch when this succeeds.
  bool requestPresent(Context* context);

  // Marks the frame as submitted after the drawing batch carrying its rendering commands has
  // been handed to the GPU queue.
  void markSubmitted();

  // Marks the frame as presented after the batch carrying its presentation request has been
  // submitted, and drops the references that are only needed while the frame is undelivered.
  void markPresented();

  // Drops the strong references that are only needed while the frame is undelivered.
  void releaseFrameHandles();

  Delivery _delivery = Delivery::Acquired;
  int _width = 0;
  int _height = 0;
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;
};

}  // namespace tgfx

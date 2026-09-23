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

#pragma once

#include <memory>
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/ImageInfo.h"

namespace tgfx {

class Context;
class RenderTargetProxy;
class Surface;

/**
 * Drawable represents a single displayable frame buffer of a Window, similar to
 * id&lt;CAMetalDrawable&gt; on the Metal platform. It is the only well-defined way to read back
 * pixels rendered to a Window: a readPixels() call on a Surface created from a Window has no
 * guaranteed content once the Surface is presented, because the underlying frame buffer is
 * recycled immediately after presentation. A Drawable is instead held by the caller for its
 * whole lifetime, so its content stays readable.
 *
 * Use Window::nextDrawable() to acquire a Drawable, then Surface::MakeFrom(context, drawable) to
 * render into it. The rendering commands must be submitted with Context::flushAndSubmit() before
 * calling readPixels() or present(). A Surface created from a Drawable is never presented
 * automatically at submit time; call present() manually, or let the Drawable present itself when
 * it is released if present() is never called.
 *
 * A Drawable is single-frame: acquire a new one for every frame and release it as soon as
 * possible. Holding multiple drawables of the same Window at once can stall frame acquisition.
 */
class Drawable {
 public:
  virtual ~Drawable() = default;

  /**
   * Returns the width of the drawable in pixels.
   */
  int width() const;

  /**
   * Returns the height of the drawable in pixels.
   */
  int height() const;

  /**
   * Returns the color space associated with this Drawable, which is inherited from the Window it
   * was acquired from. Returns nullptr for the default sRGB.
   */
  std::shared_ptr<ColorSpace> colorSpace() const;

  /**
   * Presents the content rendered into this Drawable to the screen. The rendering commands must
   * have been submitted (Context::flushAndSubmit()) before calling this method. present() is
   * idempotent; if it is never called, the Drawable is presented automatically when it is
   * released.
   */
  void present();

  /**
   * Copies a rect of pixels rendered into this Drawable to dstPixels with the specified ImageInfo.
   * Copy starts at (srcX, srcY) and does not exceed the Drawable bounds. Pixels are always
   * provided in top-left origin format; if the Drawable's origin is bottom-left, the pixels are
   * flipped during the copy. The returned data reflects the rendered content from the moment the
   * rendering commands are submitted until present() is called. The Metal backend additionally
   * allows reading after present(). Reading back requires the underlying frame buffer to be
   * readable; for example, a CAMetalLayer must have its framebufferOnly property set to NO.
   * @param dstInfo the ImageInfo describing the destination pixels.
   * @param dstPixels the destination pixel buffer, must not be nullptr.
   * @param srcX the column index to start reading from, defaults to 0.
   * @param srcY the row index to start reading from, defaults to 0.
   * @return true if pixels are copied to dstPixels.
   */
  bool readPixels(const ImageInfo& dstInfo, void* dstPixels, int srcX = 0, int srcY = 0);

 protected:
  Drawable(Context* context, std::shared_ptr<RenderTargetProxy> renderTarget,
           std::shared_ptr<ColorSpace> colorSpace = nullptr);

  /**
   * Presents the drawable to the screen. Called at most once, either from present() or from the
   * automatic presentation at release. Subclasses must also invoke present() in their destructors
   * to provide the fallback, since the base class cannot dispatch to onPresent() while it is
   * being destroyed.
   */
  virtual void onPresent() = 0;

  Context* getContext() const {
    return _context;
  }

 private:
  friend class Surface;

  Context* _context = nullptr;
  std::shared_ptr<RenderTargetProxy> _renderTarget = nullptr;
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;
  std::weak_ptr<Surface> _surface = {};
  bool _presented = false;
};

}  // namespace tgfx

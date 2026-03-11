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

namespace tgfx {
class Context;
class RenderTargetProxy;
class Surface;

/**
 * Drawable represents a rendering target that can be presented to the display. Use
 * Surface::MakeFrom() to create a Surface from a Drawable, then call present() after rendering to
 * display the content.
 */
class Drawable {
 public:
  virtual ~Drawable() = default;

  /**
   * Returns the width of the drawable surface in pixels.
   */
  int width() const {
    return _width;
  }

  /**
   * Returns the height of the drawable surface in pixels.
   */
  int height() const {
    return _height;
  }

  /**
   * Returns the color space associated with this drawable, or nullptr for the default sRGB.
   */
  std::shared_ptr<ColorSpace> colorSpace() const {
    return _colorSpace;
  }

  /**
   * Presents the rendered content to the display. The caller must flush and submit all GPU commands
   * before calling this method.
   */
  void present(Context* context);

  /**
   * Returns whether this drawable can be reused for the next frame. If false, a new drawable is
   * created each time getDrawable() is called.
   */
  virtual bool isReusable() const {
    return true;
  }

 protected:
  /**
   * Creates a new Drawable with the specified dimensions.
   */
  Drawable(int width, int height, std::shared_ptr<ColorSpace> colorSpace = nullptr)
      : _width(width), _height(height), _colorSpace(std::move(colorSpace)) {
  }

  /**
   * Returns the RenderTargetProxy for this drawable. Subclasses implement platform-specific proxy
   * creation logic.
   */
  virtual std::shared_ptr<RenderTargetProxy> getProxy(Context* context) = 0;

  /**
   * Called to present the rendered content to the display after GPU commands have been submitted.
   */
  virtual void onPresent(Context* context) = 0;

 private:
  int _width = 0;
  int _height = 0;
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;

  friend class Surface;
};
}  // namespace tgfx

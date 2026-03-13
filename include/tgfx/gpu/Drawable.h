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
#include "tgfx/gpu/CommandBuffer.h"

namespace tgfx {
class Context;
class RenderTargetProxy;
class Surface;

/**
 * Drawable represents a rendering target that can be presented to the display. Use
 * Surface::MakeFrom() to create a Surface from a Drawable. The rendered content is automatically
 * presented when Context::submit() is called with the associated Recording.
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

 protected:
  Drawable(int width, int height, std::shared_ptr<ColorSpace> colorSpace = nullptr)
      : _width(width), _height(height), _colorSpace(std::move(colorSpace)) {
  }

  virtual std::shared_ptr<RenderTargetProxy> onCreateProxy(Context* context) = 0;
  virtual void onPresent(Context* context, std::shared_ptr<CommandBuffer> commandBuffer) = 0;

  RenderTargetProxy* _proxy = nullptr;

 private:
  int _width = 0;
  int _height = 0;
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;
  std::shared_ptr<RenderTargetProxy> _proxyHolder = nullptr;

  std::shared_ptr<RenderTargetProxy> getProxy(Context* context);
  void present(Context* context, std::shared_ptr<CommandBuffer> commandBuffer);

  friend class Surface;
  friend class Context;
};
}  // namespace tgfx

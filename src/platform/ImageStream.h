/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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
#include <mutex>
#include <vector>
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Rect.h"

namespace tgfx {
class ImageReader;
class TextureView;
class Context;

/**
 * ImageStream represents a writable pixel buffer that can continuously generate ImageBuffer
 * objects, which can be directly accessed by The ImageReader class. ImageStream is an abstract
 * class. Use its subclasses instead.
 */
class ImageStream {
 public:
  virtual ~ImageStream() = default;

  /**
   * Returns the width of the ImageStream.
   */
  int width() const {
    return _width;
  }

  /**
   * Returns the height of the ImageStream.
   */
  int height() const {
    return _height;
  }

  virtual const std::shared_ptr<ColorSpace>& colorSpace() const = 0;

 protected:
  ImageStream(int width, int height) : _width(width), _height(height) {
  }

  /**
   * Creates a new TextureView capturing the pixels in the ImageBuffer. The mipmapped parameter
   * specifies whether created texture view must allocate mip map levels.
   */
  virtual std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) = 0;

  /**
   * Updates the specified bounds of the texture view with the pixels in the ImageBuffer.
   */
  virtual bool onUpdateTexture(std::shared_ptr<TextureView> textureView) = 0;

 private:
  int _width = 0;
  int _height = 0;

  friend class ImageReader;
};
}  // namespace tgfx

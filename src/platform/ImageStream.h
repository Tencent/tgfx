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
#include "tgfx/core/ImageBuffer.h"
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

  struct ReadResult {
    std::shared_ptr<TextureView> textureView = nullptr;
    ImageBufferStatus status = ImageBufferStatus::Failed;
    // Set true for every native latch, pixel write or unknown mutation, including failed attempts.
    // False promises the supplied texture and pixels stayed intact. Failed changes cannot be a
    // fallback; Timeout changes must retain a valid latched image, never partial or unknown pixels.
    bool contentChanged = false;
  };

  /**
   * Reads the frame with the given timestamp into the texture view, creating the texture view if
   * it is nullptr. Reads the next available frame if the timestamp is -1. Sources return only
   * Ready, Timeout or Failed; Pending and Expired are reader-owned lifetime states.
   */
  virtual ReadResult onReadTexture(Context* context, bool mipmapped,
                                   std::shared_ptr<TextureView> textureView, int64_t timestamp) = 0;

 private:
  int _width = 0;
  int _height = 0;

  friend class ImageReader;
};
}  // namespace tgfx

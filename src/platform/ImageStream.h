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
    /**
     * The texture view holding the most recently read frame, or nullptr if no frame has been read.
     */
    std::shared_ptr<TextureView> textureView = nullptr;
    /**
     * Ready if the requested frame was read, Timeout if it did not arrive in time, or Failed if
     * reading failed.
     */
    ImageBufferStatus status = ImageBufferStatus::Failed;
    /**
     * True if the pixels of the passed-in texture view may have changed during the read.
     */
    bool contentChanged = false;
  };

  /**
   * Reads the frame with the specified presentation timestamp into the texture view, creating the
   * texture view first if it is nullptr. Older frames are skipped, and a frame that has not arrived
   * yet is waited for a bounded time. If the timestamp is -1 or the source provides no timestamps,
   * reads the next available frame. The mipmapped parameter specifies whether a created texture
   * view must allocate mip map levels.
   */
  virtual ReadResult onReadTexture(Context* context, bool mipmapped,
                                   std::shared_ptr<TextureView> textureView, int64_t timestamp) = 0;

 private:
  int _width = 0;
  int _height = 0;

  friend class ImageReader;
};
}  // namespace tgfx

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

  /**
   * Returns true when currentFrameTimestamp() reports a meaningful presentation time, so that
   * ImageReader may wait for a target timestamp instead of only draining the queued frames. The
   * default implementation returns false: a stream without timestamps cannot tell whether a target
   * has been reached, so ImageReader ignores the target for it.
   */
  virtual bool hasFrameTimestamp() const {
    return false;
  }

  /**
   * Returns the presentation timestamp, in nanoseconds, of the frame held by the texture. Only valid
   * when hasFrameTimestamp() returns true.
   */
  virtual int64_t currentFrameTimestamp() {
    return 0;
  }

  /**
   * Consumes any frames the producer has already delivered beyond the one most recently consumed,
   * so the texture ends on the latest available frame. ImageReader calls this from readTexture()
   * after each frame has been consumed. The default implementation does nothing; only streams whose
   * producer can queue several frames ahead (the Android SurfaceTexture) override it. Implementations
   * must not block waiting for a new frame.
   */
  virtual void onDrainPendingFrames() {
  }

 private:
  int _width = 0;
  int _height = 0;

  friend class ImageReader;
};
}  // namespace tgfx

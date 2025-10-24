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

#include <mutex>
#include "tgfx/core/ImageBuffer.h"
#include "tgfx/core/Rect.h"

namespace tgfx {
class TextureView;
class ImageStream;

/**
 * The ImageReader class allows direct access to ImageBuffers generated from a video source of the
 * native platform. The video source could be a Surface on the android platform or an
 * HTMLVideoElement on the web platform. You should call ImageReader::acquireNextBuffer() to read a
 * new ImageBuffer each time when the video source is changed. All ImageBuffers generated from one
 * ImageReader share the same internal texture, which allows you to continuously read the latest
 * content from the video source with minimal memory copying. However, there are two limits:
 *
 *    1) The generated ImageBuffers are bound to the associated GPU Context when first being drawn
 *       and cannot be drawn to another Context anymore.
 *    2) The generated ImageBuffers may have a limited lifetime and cannot create textures after
 *       expiration. Usually, the previously acquired ImageBuffer will expire after the newly
 *       created ImageBuffer is drawn. So there are only two ImageBuffers that can be accessed
 *       simultaneously.
 */
class ImageReader {
 public:
  virtual ~ImageReader() = default;

  /**
   * Returns the width of generated image buffers.
   */
  int width() const;

  /**
   * Returns the height of generated image buffers.
   */
  int height() const;

  /**
   * Returns the ColorSpace of generated image buffers.
   */
  std::shared_ptr<ColorSpace> colorSpace() const;

  /**
   * Acquires the next ImageBuffer from the ImageReader after a new image frame has been rendered
   * into the associated video source. Note that the previously returned image buffers will
   * immediately expire after the newly created ImageBuffer is drawn.
   */
  std::shared_ptr<ImageBuffer> acquireNextBuffer();

 protected:
  std::mutex locker = {};
  std::weak_ptr<ImageReader> weakThis;
  std::shared_ptr<ImageStream> stream = nullptr;

  explicit ImageReader(std::shared_ptr<ImageStream> stream);

 private:
  std::shared_ptr<TextureView> textureView = nullptr;
  uint64_t bufferVersion = 0;
  uint64_t textureVersion = 0;

  static std::shared_ptr<ImageReader> MakeFrom(std::shared_ptr<ImageStream> imageStream);

  bool checkExpired(uint64_t contentVersion);

  std::shared_ptr<TextureView> readTexture(uint64_t contentVersion, Context* context,
                                           bool mipmapped);

  friend class ImageReaderBuffer;
  friend class ImageStream;
};
}  // namespace tgfx

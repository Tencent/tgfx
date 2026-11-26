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

#include <cstdint>
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
  const std::shared_ptr<ColorSpace>& colorSpace() const;

  /**
   * Acquires the next ImageBuffer from the ImageReader after a new image frame has been rendered
   * into the associated video source. The frame is read when the ImageBuffer is first drawn. If the
   * timestamp of the frame is specified, the drawing waits for a bounded time until the frame with
   * that timestamp arrives, skipping any older frames still queued in the video source. If the
   * frame does not arrive in time, or reading it fails, the drawing shows the most recently read
   * frame instead, and the status() of the ImageBuffer reports Timeout or Failed after the drawing
   * is flushed. Drawing a newly created Image of that ImageBuffer retries. Note that the previously
   * returned image buffers will immediately expire after the newly created ImageBuffer is drawn.
   * @param timestamp The presentation timestamp in nanoseconds that the producer attached to the
   * rendered frame. For example, a MediaCodec output buffer released with render set to true is
   * attached its presentationTimeUs multiplied by 1000. Pass -1 if unknown, in which case the
   * drawing reads the next available frame without verifying it. Video sources that provide no
   * timestamps, such as an HTMLVideoElement, ignore this parameter.
   */
  std::shared_ptr<ImageBuffer> acquireNextBuffer(int64_t timestamp = -1);

 protected:
  std::mutex locker = {};
  std::weak_ptr<ImageReader> weakThis;
  std::shared_ptr<ImageStream> stream = nullptr;

  explicit ImageReader(std::shared_ptr<ImageStream> stream);

 private:
  std::shared_ptr<TextureView> textureView = nullptr;
  uint64_t bufferVersion = 0;
  uint64_t textureVersion = 0;
  uint64_t failedVersion = 0;
  ImageBufferStatus failedStatus = ImageBufferStatus::Pending;

  static std::shared_ptr<ImageReader> MakeFrom(std::shared_ptr<ImageStream> imageStream);

  bool checkExpired(uint64_t contentVersion);

  ImageBufferStatus getStatus(uint64_t contentVersion);

  std::shared_ptr<TextureView> readTexture(uint64_t contentVersion, int64_t timestamp,
                                           Context* context, bool mipmapped);

  friend class ImageReaderBuffer;
  friend class ImageStream;
};
}  // namespace tgfx

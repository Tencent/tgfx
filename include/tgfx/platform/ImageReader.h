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
 *    2) A new acquisition expires older buffers except the one still bound to valid current pixels.
 *       A native read that changes or invalidates pixels expires the previously Ready buffer.
 *       Timeout and Failed buffers can retry until superseded by a newer acquisition.
 *
 * Acquisitions and status queries share the reader mutex with texture reads and may wait for an
 * in-flight read. They do not reserve pixels or guarantee Context/GPU lifetime. Rebuild callers and
 * subclasses with this version; changed virtual interfaces and layouts are not binary-compatible.
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
   * Returns the source color space, or nullptr if unspecified. The reference is forwarded without
   * locking; serialize this query and use of its reference with source texture reads when the
   * source can update its color space.
   */
  const std::shared_ptr<ColorSpace>& colorSpace() const;

  /**
   * Acquires a Pending buffer without reading source pixels. On Android, texture generation reads
   * until the requested timestamp is found or the one-second notification-wait budget is exhausted.
   * GPU, JNI and lock operations can exceed that budget; it is not a hard completion deadline.
   * Query status() after drawing is flushed. Android Ready confirms a nonnegative requested PTS;
   * -1 instead selects an available frame. Web ignores PTS and has no upload-result receipt.
   * Timeout can display the most recently valid latched image, which need not be the previously
   * accepted image. Failed can retain known-valid prior pixels, but unknown mutations draw nothing.
   * Neither fallback status implies a target match. Create a new Image from the same nonexpired
   * buffer to retry; reusing an already cached Image may not perform another source read.
   * Timestamps do not encode source restart generations. Callers that need to distinguish restarts
   * must avoid reusing the same native timestamp range for different generations.
   * @param timestamp The requested frame's presentation timestamp in nanoseconds. For MediaCodec
   * output released with render set to true, pass presentationTimeUs multiplied by 1000. Pass -1
   * to draw the next available frame without checking it. Ignored on the web platform.
   */
  std::shared_ptr<ImageBuffer> acquireNextBuffer(int64_t timestamp = -1);

 protected:
  std::mutex locker = {};
  std::weak_ptr<ImageReader> weakThis;
  std::shared_ptr<ImageStream> stream = nullptr;

  explicit ImageReader(std::shared_ptr<ImageStream> stream);

 private:
  std::shared_ptr<TextureView> textureView = nullptr;
  bool contentValid = false;
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

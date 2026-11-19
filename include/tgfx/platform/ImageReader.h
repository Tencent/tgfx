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

#include <atomic>
#include <mutex>
#include "tgfx/core/ImageBuffer.h"
#include "tgfx/core/Rect.h"

namespace tgfx {
class TextureView;
class ImageStream;

/**
 * Describes the delivery state of a texture read. Only Ready confirms that the requested frame is
 * bound. Other states do not prescribe whether the caller should display, retry, or cache it.
 */
enum class ImageReaderStatus {
  Pending,
  Ready,
  NotReady,
  Mismatched,
  Failed,
  Expired,
  Unsupported,
};

/**
 * Carries the target and delivery state of one ImageBuffer. Set targetTimestamp before acquiring
 * the buffer and keep it unchanged afterwards. The caller may update deadline before each texture
 * read, including retries. Read status with acquire ordering after the GPU resource tasks complete.
 */
struct ImageReaderFrame {
  /** The requested presentation timestamp in nanoseconds, or -1 to read the latest available frame. */
  int64_t targetTimestamp = -1;
  /** The absolute waiting deadline in microseconds on Clock::Now()'s monotonic timeline.
   * A deadline at or before Clock::Now(), including 0, disables waiting for producer notifications.
   * This bounds notification waits, not platform driver calls or GPU execution. */
  std::atomic<int64_t> deadline = 0;
  /** The timestamp of the bound texture in nanoseconds, or -1 if no timestamp is available. */
  std::atomic<int64_t> timestamp = -1;
  /** The result of the most recent texture read. */
  std::atomic<ImageReaderStatus> status = ImageReaderStatus::Pending;
};

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
   * Acquires an ImageBuffer after the producer renders a frame into the associated source. The
   * frame request belongs to this buffer and cannot be overwritten by later acquisitions. A texture
   * read returns no texture while reporting NotReady, Mismatched, Failed, or Expired. This prevents
   * resource caches from treating unrelated pixels as this buffer's content. The caller decides
   * whether to retry, retain its previous output, or request the latest frame instead. An incomplete
   * read remains retryable until another buffer is acquired or the producer seeks. A timestamped
   * stream may immediately report Mismatched again after passing the target; retrying does not seek
   * the producer. To recover an earlier target, reposition the producer, notifySeeked(), and acquire
   * a new buffer.
   * @param frame Optional target, waiting deadline, and delivery state for this buffer. Passing
   * nullptr requests the latest available frame without waiting. Do not reuse a frame request for
   * different buffers. Streams without timestamps report Unsupported for a nonnegative target.
   * @return The acquired ImageBuffer, whose pixels are resolved when it is first drawn.
   */
  std::shared_ptr<ImageBuffer> acquireNextBuffer(std::shared_ptr<ImageReaderFrame> frame = nullptr);

  /**
   * Tells the reader that the producer seeked, so the presentation time it compares the next frame
   * against is discarded. Without it a seek that restarts the timeline makes the first frames look
   * like they were already reached, and the texture keeps showing the frame from before the seek.
   */
  void notifySeeked();

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
                                           bool mipmapped,
                                           const std::shared_ptr<ImageReaderFrame>& frame);

  friend class ImageReaderBuffer;
  friend class ImageStream;
};
}  // namespace tgfx

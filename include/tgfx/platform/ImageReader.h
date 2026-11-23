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
 * Reports texture delivery, not playback progress or screen presentation. For a nonnegative target,
 * only Ready confirms that the bound texture has the requested timestamp. Without a target, Ready
 * confirms a valid texture read, not a new frame or a one-frame advance. The caller decides whether
 * to display, retry, reposition the producer, or cache the output.
 */
enum class ImageReaderStatus {
  Pending,      // No texture read result has been reported yet.
  Ready,        // A valid texture is bound, matching the target when one was supplied.
  NotReady,     // The target has not been reached; the buffer may still be retried.
  Mismatched,   // The bound timestamp does not match the target.
  Failed,       // The texture read failed.
  Expired,      // The buffer is no longer eligible for texture reads.
  Unsupported,  // The source cannot verify the supplied target timestamp.
};

/**
 * Caller-supplied constraints for a deferred texture read. The target is copied when acquiring a
 * buffer; later edits do not retarget that buffer. The caller may update the waiting deadline before
 * each read, including retries. Use separate options for buffers with independent waiting budgets.
 */
struct ImageReaderOptions {
  /** A forward-consumption stopping timestamp in nanoseconds, not a seek or random-access request.
   * Reading never rewinds the source to reach it. Use -1 to read the latest available frame. */
  int64_t targetTimestamp = -1;
  /** The absolute waiting deadline in microseconds on Clock::Now()'s monotonic timeline.
   * A deadline at or before Clock::Now(), including 0, disables waiting for producer notifications.
   * This bounds notification waits, not platform driver calls or GPU execution. */
  std::atomic<int64_t> deadline = 0;
};

/**
 * Reader-written delivery receipt for one buffer. Use a separate result for each buffer. After the
 * GPU resource tasks complete, read status with acquire ordering before reading timestamp. Do not
 * modify these fields while the buffer is in use. This reports texture reads, not playback progress
 * or screen presentation; the caller decides whether to publish or cache the output.
 */
struct ImageReaderResult {
  /** The timestamp of the bound texture in nanoseconds, or -1 if no timestamp is available. */
  std::atomic<int64_t> timestamp = -1;
  /** The result of the most recent texture read. */
  std::atomic<ImageReaderStatus> status = ImageReaderStatus::Pending;
};

/**
 * The ImageReader class allows direct access to ImageBuffers generated from a video source of the
 * native platform. The video source could be a Surface on the android platform or an
 * HTMLVideoElement on the web platform. You should call ImageReader::acquireBuffer() to read a
 * new ImageBuffer each time when the video source is changed. All ImageBuffers generated from one
 * ImageReader share the same internal texture, which allows you to continuously read the latest
 * content from the video source with minimal memory copying. However, there are two limits:
 *
 *    1) The generated ImageBuffers are bound to the associated GPU Context when first being drawn
 *       and cannot be drawn to another Context anymore.
 *    2) The generated ImageBuffers may have a limited lifetime and cannot create textures after
 *       expiration. A previously delivered buffer remains readable while the shared texture is
 *       known to be unchanged. A later read that changes or may have changed its content expires
 *       that buffer, even if the later read fails. Acquiring another buffer expires older undelivered
 *       buffers; notifySeeked() expires all previously acquired buffers.
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
   * Acquires a deferred ImageBuffer after the producer renders into the associated source. This
   * does not dequeue a frame; pixels are resolved during a texture read. Acquisition counts do not
   * identify source frames: the source may skip frames when latching its latest image. The frame
   * request belongs to this buffer and cannot be overwritten by later acquisitions. A texture
   * read returns no texture while reporting NotReady, Mismatched, Failed, or Expired. This prevents
   * resource caches from treating unrelated pixels as this buffer's content. The caller decides
   * whether to retry, retain its previous output, or request the latest frame instead. Redrawing a
   * previously delivered buffer is valid only while it has not expired. To retain output regardless
   * of subsequent stream reads, the caller must keep an independent pixel copy or rendered output.
   * An incomplete read remains retryable until another buffer is acquired or the producer seeks. A timestamped
   * stream may immediately report Mismatched again after passing the target; retrying does not seek
   * the producer. To recover an earlier target, reposition the producer, notifySeeked(), and acquire
   * a new buffer.
   * @param options Optional target and waiting deadline. A null value requests the latest available
   * frame without waiting. The target is copied here; the deadline is read on each texture read.
   * @param result Optional receipt for this buffer. Use a new result for each acquisition. Streams
   * without timestamps report Unsupported for a nonnegative target. Omitting the receipt does not
   * bypass the supplied read constraints or allow a mismatched texture to be returned.
   * @return The acquired ImageBuffer, whose pixels are resolved when it is first drawn.
   */
  std::shared_ptr<ImageBuffer> acquireBuffer(std::shared_ptr<ImageReaderOptions> options = nullptr,
                                             std::shared_ptr<ImageReaderResult> result = nullptr);

  /** Source-compatible entry point. Reads no longer wait for producer notifications; before the
   * first image arrives, a texture read may immediately return nullptr. Use acquireBuffer() instead. */
  [[deprecated("Use acquireBuffer() instead.")]] std::shared_ptr<ImageBuffer> acquireNextBuffer();

  /**
   * Tells the reader that the producer seeked, so the presentation time it compares the next frame
   * against is discarded. Without it a seek that restarts the timeline makes the first frames look
   * like they were already reached, and the texture keeps showing the frame from before the seek.
   * All previously acquired ImageBuffers expire immediately, including buffers whose texture reads
   * already reported Ready. Acquire a new buffer for frames produced after the seek.
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
                                           bool mipmapped, int64_t target,
                                           const std::shared_ptr<ImageReaderOptions>& options,
                                           const std::shared_ptr<ImageReaderResult>& receipt);

  friend class ImageReaderBuffer;
  friend class ImageStream;
};
}  // namespace tgfx

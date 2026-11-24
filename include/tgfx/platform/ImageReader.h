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

namespace tgfx {
class TextureView;
class ImageStream;

/**
 * Defers sequential image delivery until texture generation. All acquired buffers share a native
 * texture bound to one Context and must be rendered with that Context. Reads consume immediately
 * available contents with a bounded number of attempts, without waiting for producer notifications
 * or selecting a target timestamp. GPU, driver, and lock operations have no bounded completion time.
 * Delivered pixels remain valid only until their buffer expires. Preserve an independent rendered
 * output to retain pixels across later source reads.
 */
class ImageReader {
 public:
  virtual ~ImageReader() = default;

  /**
   * Returns the width of the source images in pixels.
   */
  int width() const;

  /**
   * Returns the height of the source images in pixels.
   */
  int height() const;

  /**
   * Returns the ColorSpace of the source images, or nullptr if unspecified.
   */
  const std::shared_ptr<ColorSpace>& colorSpace() const;

  /**
   * Acquires a Pending buffer without dequeuing source contents. Its first texture-generation read
   * delivers Ready pixels or reaches terminal Expired if no content is delivered, without waiting
   * for a producer notification. An Expired buffer cannot retry; acquire a new buffer instead.
   * Each acquisition expires older undelivered Pending buffers. A previously Ready buffer remains
   * available for redraw while the shared native contents are known unchanged, but expires after a
   * later native mutation or a failed read whose effect on those contents is unknown.
   * Ready means successful delivery, not a match for a caller's target frame. Use timestamp() for
   * the actual delivered presentation timestamp after texture generation completes.
   */
  std::shared_ptr<ImageBuffer> acquireBuffer();

  /**
   * Forwards to acquireBuffer() with identical deferred-delivery and lifetime semantics.
   * Retained without deprecation for existing zero-argument source calls. It does not restore the
   * former notification wait or guarantee compatibility with previously compiled binaries.
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
  static std::shared_ptr<ImageReader> MakeFrom(std::shared_ptr<ImageStream> stream);
  bool checkExpired(uint64_t version);
  std::shared_ptr<TextureView> readTexture(uint64_t version, Context* context, bool mipmapped,
                                           std::atomic<ImageBufferState>& state,
                                           std::atomic<int64_t>& timestamp);
  friend class ImageReaderBuffer;
  friend class ImageStream;
};
}  // namespace tgfx

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
 * Defers sequential image delivery until texture generation. All acquired buffers share a native
 * texture bound to one Context and must be rendered with that Context. The first texture-generation
 * attempt for a valid new credential performs one source read, without draining queued frames,
 * waiting for producer notifications, or selecting a target timestamp. The platform can still
 * return its latest image and skip intermediate frames.
 * GPU, driver, and lock operations have no bounded completion time.
 * Delivery is written while generating a texture on the rendering thread; other threads may query
 * buffer state and timestamp for reconciliation. Reads and these queries share the reader's mutex,
 * including the native texture lifetime check. Queries are thread-safe, not lock-free, and may wait
 * for an in-flight source read. Observe Ready after texture generation finishes, then read the
 * timestamp; this does not reserve the texture against a subsequent read or acquisition.
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
   * Returns the ColorSpace of the source images, or nullptr if unspecified. This forwards the
   * source's reference without locking; sources with changing metadata require callers to serialize
   * this query and use of the returned reference with texture reads.
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
  struct ReadResult {
    std::shared_ptr<TextureView> texture = nullptr;
    bool delivered = false;
    int64_t timestamp = -1;
  };
  int64_t textureTimestamp = -1;

  // Called with locker held; readTexture() additionally requires a nonexpired credential.
  bool checkExpired(uint64_t version);
  ReadResult readTexture(uint64_t version, Context* context, bool mipmapped);
  friend class ImageReaderBuffer;
  friend class ImageStream;
};
}  // namespace tgfx

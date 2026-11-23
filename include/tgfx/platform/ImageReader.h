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

/** Deferred sequential consumption. Reads never wait for producer notifications.
 * Each read drains immediately available contents with a fixed bound, without a target or deadline.
 * A buffer reports Pending until read, Ready once delivered, or terminal Expired when not delivered.
 * Ready reports a successful native read, not that it matches any caller's expected timestamp.
 * All buffers share a context-bound native texture, so prior delivered buffers have limited lifetime.
 * Acquisition itself does not dequeue. Redraw is valid only while the buffer has not expired.
 * Preserve an independent rendered output when retaining pixels across later source reads.
 */
class ImageReader {
 public:
  virtual ~ImageReader() = default;
  int width() const;
  int height() const;
  const std::shared_ptr<ColorSpace>& colorSpace() const;
  std::shared_ptr<ImageBuffer> acquireBuffer();
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

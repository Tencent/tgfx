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
#include "tgfx/platform/ImageReader.h"

namespace tgfx {
class ImageReader;
class TextureView;
class Context;

/**
 * ImageStream represents a writable pixel buffer that can continuously generate ImageBuffer
 * objects, which can be directly accessed by The ImageReader class. ImageStream is an abstract
 * class. Use its subclasses instead. ImageReader serializes callbacks only within one reader;
 * implementations shared by multiple readers must synchronize their own mutable state.
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
   * Creates a texture using the stream's native pixel source. The mipmapped parameter specifies
   * whether the created texture view must allocate mip map levels, when supported by the source.
   */
  virtual std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) = 0;

  /** Updates an existing texture using the stream's native pixel source. */
  virtual bool onUpdateTexture(std::shared_ptr<TextureView> textureView) = 0;

  struct TextureReadResult {
    std::shared_ptr<TextureView> texture = nullptr;
    ImageReaderStatus status = ImageReaderStatus::Failed;
    int64_t timestamp = -1;
    // False only when the previous texture and its pixels are known to be unchanged throughout
    // this read. Unknown or failed mutations must conservatively report true.
    bool contentChanged = true;
  };

  /**
   * Creates or updates a texture and reports which frame is bound. The deadline uses Clock::Now()'s
   * microsecond timeline and bounds all producer-notification waits in this read. A nonnegative
   * target is a forward-consumption stopping point, never a request to seek or rewind the source.
   * A target of -1 requests the latest available frame. The result may retain a texture without
   * declaring Ready; the caller, not the stream, determines playback and publication policy.
   */
  virtual TextureReadResult onReadTexture(Context* context, bool mipmapped,
                                          std::shared_ptr<TextureView> texture, int64_t target,
                                          int64_t /*deadline*/) {
    if (texture == nullptr) {
      texture = onMakeTexture(context, mipmapped);
    } else if (!onUpdateTexture(texture)) {
      return {std::move(texture), ImageReaderStatus::Failed, -1};
    }
    if (texture == nullptr) {
      return {};
    }
    auto status = target >= 0 ? ImageReaderStatus::Unsupported : ImageReaderStatus::Ready;
    return {std::move(texture), status, -1};
  }

  /**
   * Tells the stream that the producer restarted its timeline, so anything derived from the previous
   * presentation time must be dropped: after a seek the timestamps start over and a stale reference
   * would make the next frame look like it was already reached. The default implementation does
   * nothing.
   */
  virtual void onSeeked() {
  }

 private:
  int _width = 0;
  int _height = 0;

  friend class ImageReader;
};
}  // namespace tgfx

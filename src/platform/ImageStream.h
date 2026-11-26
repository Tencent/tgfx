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
#include <memory>
#include "tgfx/core/ColorSpace.h"

namespace tgfx {
class TextureView;
class Context;
class ImageReader;

/** A source of updated pixels consumed through ImageReader's deferred buffers. onReadTexture()
 * calls are serialized within one reader. Shared sources synchronize their own mutable state.
 */
class ImageStream {
 public:
  virtual ~ImageStream() = default;

  int width() const {
    return _width;
  }

  int height() const {
    return _height;
  }

  virtual const std::shared_ptr<ColorSpace>& colorSpace() const = 0;

 protected:
  ImageStream(int width, int height) : _width(width), _height(height) {
  }

  enum class ReadOutcome {
    /** No new image was delivered and the supplied texture and its pixels remain unchanged. */
    Unchanged,
    /** Valid source pixels were delivered. This does not establish a business target match. */
    Delivered,
    /** No valid image was delivered; the previous content may have changed or become unusable. */
    Invalidated
  };

  struct TextureReadResult {
    /** Current native texture; retaining it does not itself imply valid delivered content. */
    std::shared_ptr<TextureView> texture = nullptr;
    ReadOutcome outcome = ReadOutcome::Invalidated;
    /** Actual delivered source timestamp in nanoseconds, or -1 when unavailable. */
    int64_t timestamp = -1;
  };

  /** Performs one non-waiting source read without draining queued images or choosing a target.
   * Unchanged preserves the supplied texture and its pixels; the reader may then reuse previously
   * delivered content. Native latches with unknown effects report Invalidated, including failures.
   * Scheduling another acquisition and deciding whether its pixels match a target are caller work.
   */
  virtual TextureReadResult onReadTexture(Context* context, bool mipmapped,
                                          std::shared_ptr<TextureView> texture) = 0;

 private:
  int _width = 0;
  int _height = 0;
  friend class ImageReader;
};
}  // namespace tgfx

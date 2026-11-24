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

/** Abstract source of continuously updated pixels, consumed through ImageReader's deferred buffers.
 * Use a platform subclass to create and update native textures. Reader callbacks are serialized
 * within one reader; sources shared by different readers must synchronize their own mutable state. */
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
  /** Creates a native texture. The mipmapped flag requests mip map levels when supported by
   * the source. A null result means no texture was created. */
  virtual std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) = 0;

  /** Updates the native texture and reports success, not whether another queued image exists. */
  virtual bool onUpdateTexture(std::shared_ptr<TextureView> texture) = 0;

  struct TextureReadResult {
    /** Native texture, possibly retained after an empty or failed read. Its presence alone does
     * not make this read successful. */
    std::shared_ptr<TextureView> texture = nullptr;
    /** This read supplied valid content for a new credential; it does not match a business target. */
    bool advanced = false;
    /** Timestamp of the bound content in nanoseconds, or -1 when unavailable. */
    int64_t timestamp = -1;
    /** False only if the previous texture and pixels stayed unchanged throughout this callback.
     * Native latches and unknown mutations must report true, including when they fail. */
    bool contentChanged = true;
    /** A failure prevents delivery even if a texture is retained or content changed.
     * An empty queue instead reports advanced=false and failed=false. */
    bool failed = true;
  };

  /** Performs one create/update operation for a credential. Sources can override this to return
   * actual delivery metadata, but must not drain queued frames or wait for producer notifications.
   * Scheduling another attempt belongs to the caller. */
  virtual TextureReadResult onReadTexture(Context* context, bool mipmapped,
                                          std::shared_ptr<TextureView> texture) {
    bool advanced;
    if (texture == nullptr) {
      texture = onMakeTexture(context, mipmapped);
      advanced = texture != nullptr;
    } else {
      advanced = onUpdateTexture(texture);
    }
    return {std::move(texture), advanced, -1, true, !advanced};
  }

 private:
  int _width = 0;
  int _height = 0;
  friend class ImageReader;
};
}  // namespace tgfx

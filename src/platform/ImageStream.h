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
 * within one reader. Sources shared by different readers must synchronize their own mutable state.
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
  /** Legacy creation hook used by the default onReadTexture() adapter. The mipmapped flag requests
   * mip map levels when supported by the source. Defaults to nullptr for sources that override
   * onReadTexture() instead; it does not forward to that method. */
  virtual std::shared_ptr<TextureView> onMakeTexture(Context*, bool) {
    return nullptr;
  }

  /** Legacy update hook used by the default onReadTexture() adapter. Returns success, not whether
   * another queued image exists. Defaults to false and does not forward to onReadTexture(). */
  virtual bool onUpdateTexture(std::shared_ptr<TextureView>) {
    return false;
  }

  struct TextureReadResult {
    /** Native texture, possibly retained after an empty or failed read. Its presence alone does
     * not make this read successful. */
    std::shared_ptr<TextureView> texture = nullptr;
    /** This read supplied valid content for a new credential, not a business target match.
     */
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

  /** Calls one legacy creation or update hook and reports its success as delivery, with no PTS.
   * A missing hook or false/null result reports failure conservatively, since this adapter cannot
   * distinguish an empty queue from an error. Sources with that distinction override this method
   * without implementing legacy hooks. Overrides must perform one read without draining queued
   * frames or waiting for producer notifications. Scheduling another attempt belongs to the caller.
   */
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

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
#include "tgfx/core/ColorSpace.h"
#include "tgfx/platform/ImageReader.h"

namespace tgfx {
class TextureView;
class Context;
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
  virtual std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) = 0;
  virtual bool onUpdateTexture(std::shared_ptr<TextureView> texture) = 0;
  struct TextureReadResult {
    std::shared_ptr<TextureView> texture = nullptr;
    bool advanced = false;
    int64_t timestamp = -1;
    bool contentChanged = true;
    bool failed = true;
  };
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

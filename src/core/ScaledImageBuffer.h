/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "tgfx/core/ImageBuffer.h"

namespace tgfx {
class ScaledImageBuffer : public ImageBuffer {
 public:
  static std::shared_ptr<ScaledImageBuffer> Make(int width, int height,
                                                 const std::shared_ptr<ImageBuffer>& source);
  int width() const override {
    return _width;
  }

  int height() const override {
    return _height;
  }

  bool isAlphaOnly() const override {
    return source->isAlphaOnly();
  }

 protected:
  std::shared_ptr<Texture> onMakeTexture(Context* context, bool mipmapped) const override;

 private:
  int _width;
  int _height;
  std::shared_ptr<ImageBuffer> source = nullptr;

  ScaledImageBuffer(int width, int height, const std::shared_ptr<ImageBuffer>& source);
};
}  // namespace tgfx
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

#include <emscripten/val.h>
#include "tgfx/core/ImageBuffer.h"

namespace tgfx {
class Texture;
class WebImageBuffer : public ImageBuffer {
 public:
  /**
   * Creates a new ImageBuffer object from the platform-specific nativeImage in the CPU. The
   * returned ImageBuffer object takes a reference to the nativeImage. Returns nullptr if the
   * nativeImage is nullptr or has a size of zero.
   */
  static std::shared_ptr<WebImageBuffer> MakeFrom(emscripten::val nativeImage);

  /**
   * Creates a new ImageBuffer object from the platform-specific nativeImage in the CPU. The
   * returned ImageBuffer object takes a reference to the nativeImage and will call
   * tgfx.releaseNativeImage() when it goes out of scope. Returns nullptr if the nativeImage is
   * nullptr or has a size of zero.
   */
  static std::shared_ptr<WebImageBuffer> MakeAdopted(emscripten::val nativeImage,
                                                     bool alphaOnly = false);

  ~WebImageBuffer() override;

  int width() const override {
    return _width;
  }

  int height() const override {
    return _height;
  }

  bool isAlphaOnly() const override {
    return _alphaOnly;
  }

  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return ColorSpace::SRGB();
  }

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) const override;

  bool onUploadToTexture(Context* context, std::shared_ptr<Texture> texture, int offsetX,
                         int offsetY) const override;

 private:
  int _width = 0;
  int _height = 0;
  bool _alphaOnly = false;
  emscripten::val nativeImage = emscripten::val::null();
  bool adopted = false;

  WebImageBuffer(int width, int height, emscripten::val nativeImage, bool alphaOnly = false);

  emscripten::val getImage() const;

  friend class NativeCodec;
};
}  // namespace tgfx

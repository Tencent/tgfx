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

#include "JNIUtil.h"
#include "core/PixelBuffer.h"

namespace tgfx {
class NativeImageBuffer : public ImageBuffer {
 public:
  /**
   * Creates a ImageBuffer from the specified Android Bitmap object. Returns nullptr if the bitmap
   * is null, has an unpremultiplied alpha type, or its color type is neither RGBA_8888 nor ALPHA_8.
   */
  static std::shared_ptr<ImageBuffer> MakeFrom(JNIEnv* env, jobject bitmap);

  int width() const override {
    return info.width();
  }

  int height() const override {
    return info.height();
  }

  bool isAlphaOnly() const override {
    return info.isAlphaOnly();
  }

  std::shared_ptr<ColorSpace> colorSpace() const override;

  void setColorSpace(std::shared_ptr<ColorSpace>) override {
  }

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) const override;

 private:
  ImageInfo info = {};
  Global<jobject> bitmap = {};
  std::shared_ptr<ColorSpace> _colorSpace = ColorSpace::MakeSRGB();

  explicit NativeImageBuffer(const ImageInfo& info) : info(info) {
  }
};

}  // namespace tgfx

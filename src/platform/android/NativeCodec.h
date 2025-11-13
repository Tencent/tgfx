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
#include "tgfx/core/ImageCodec.h"

namespace tgfx {
class NativeCodec : public ImageCodec {
 public:
  static void JNIInit(JNIEnv* env);

 protected:
  bool onReadPixels(ColorType colorType, AlphaType alphaType, size_t dstRowBytes,
                    std::shared_ptr<ColorSpace> dstColorSpace, void* dstPixels) const override;

  std::shared_ptr<ImageBuffer> onMakeBuffer(bool tryHardware) const override;

 private:
  std::string imagePath;
  std::shared_ptr<Data> imageBytes;
  Global<jobject> nativeImage;

  static std::shared_ptr<NativeCodec> Make(JNIEnv* env, jobject sizeObject, int origin);

  NativeCodec(int width, int height, Orientation orientation,
              std::shared_ptr<ColorSpace> colorSpace = ColorSpace::SRGB())
      : ImageCodec(width, height, orientation, std::move(colorSpace)) {
  }

  jobject decodeBitmap(JNIEnv* env, ColorType colorType, AlphaType alphaType,
                       bool tryHardware) const;

  friend class ImageCodec;
};
}  // namespace tgfx

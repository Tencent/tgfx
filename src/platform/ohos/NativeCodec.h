/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "tgfx/core/ImageCodec.h"

struct OH_ImageSourceNative;
struct OH_PixelmapNative;

namespace tgfx {
class NativeCodec : public ImageCodec {
 public:
 protected:
  bool onReadPixels(ColorType colorType, AlphaType alphaType, size_t dstRowBytes,
                    void* dstPixels) const override;

 private:
  std::string imagePath;
  std::shared_ptr<Data> imageBytes;

  static std::shared_ptr<NativeCodec> Make(OH_ImageSourceNative* imageSource);

  static ImageInfo GetPixelmapInfo(OH_PixelmapNative* pixelmap);

  OH_ImageSourceNative* CreateImageSource() const;

  NativeCodec(int width, int height, Orientation orientation,
              std::shared_ptr<ColorSpace> colorSpace)
      : ImageCodec(width, height, orientation, std::move(colorSpace)) {
  }

  friend class ImageCodec;
};
}  // namespace tgfx
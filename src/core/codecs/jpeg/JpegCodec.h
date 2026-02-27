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

#include "tgfx/core/ImageCodec.h"

namespace tgfx {
class JpegCodec : public ImageCodec {
 public:
  static std::shared_ptr<ImageCodec> MakeFrom(const std::string& filePath);
  static std::shared_ptr<ImageCodec> MakeFrom(std::shared_ptr<Data> imageBytes);
  static bool IsJpeg(const std::shared_ptr<Data>& data);

#ifdef TGFX_USE_JPEG_ENCODE
  static std::shared_ptr<Data> Encode(const Pixmap& pixmap, int quality);
#endif

  uint32_t getScaledDimensions(int newWidth, int newHeight) const;

 protected:
  bool onReadPixels(ColorType colorType, AlphaType alphaType, size_t dstRowBytes,
                    std::shared_ptr<ColorSpace> colorSpace, void* dstPixels) const override;

  bool readPixels(const ImageInfo& dstInfo, void* dstPixels) const override;

  bool readScaledPixels(ColorType colorType, AlphaType alphaType, size_t dstRowBytes,
                        void* dstPixels, uint32_t scaleNum,
                        std::shared_ptr<ColorSpace> dstColorSpace) const;

  std::shared_ptr<Data> getEncodedData() const override;

 private:
  std::shared_ptr<Data> fileData;
  const std::string filePath;

  static std::shared_ptr<ImageCodec> MakeFromData(const std::string& filePath,
                                                  std::shared_ptr<Data> byteData);
  explicit JpegCodec(int width, int height, Orientation orientation, std::string filePath,
                     std::shared_ptr<Data> fileData, std::shared_ptr<ColorSpace> colorSpace)
      : ImageCodec(width, height, orientation, std::move(colorSpace)),
        fileData(std::move(fileData)), filePath(std::move(filePath)) {
  }
};

}  // namespace tgfx

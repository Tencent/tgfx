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
#include "webp/decode.h"
#include "webp/demux.h"
#include "webp/encode.h"
#include "webp/mux.h"
#include "webp/mux_types.h"

namespace tgfx {
class WebpCodec : public ImageCodec {
 public:
  static std::shared_ptr<ImageCodec> MakeFrom(const std::string& filePath);
  static std::shared_ptr<ImageCodec> MakeFrom(std::shared_ptr<Data> imageBytes);
  static bool IsWebp(const std::shared_ptr<Data>& data);

#ifdef TGFX_USE_WEBP_ENCODE
  static std::shared_ptr<Data> Encode(const Pixmap& pixmap, int quality);
#endif

 protected:
  bool onReadPixels(ColorType colorType, AlphaType alphaType, size_t dstRowBytes,
                    std::shared_ptr<ColorSpace> dstColorSpace, void* dstPixels) const override;

  std::shared_ptr<Data> getEncodedData() const override;

 private:
  std::shared_ptr<Data> fileData;
  std::string filePath;

  explicit WebpCodec(int width, int height, Orientation orientation, std::string filePath,
                     std::shared_ptr<Data> fileData, const std::shared_ptr<ColorSpace>& colorSpace)
      : ImageCodec(width, height, orientation, colorSpace), fileData(std::move(fileData)),
        filePath(std::move(filePath)) {
  }
};

}  // namespace tgfx
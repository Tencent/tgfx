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

#include "PixelBufferCodec.h"
#include "BoxFilterDownsample.h"
#include "PixelBuffer.h"

namespace tgfx {
std::shared_ptr<PixelBufferCodec> PixelBufferCodec::Make(std::shared_ptr<PixelBuffer> source) {
  if (!source) {
    return nullptr;
  }
  return std::make_shared<PixelBufferCodec>(std::move(source));
}

bool PixelBufferCodec::onReadPixels(ColorType colorType, AlphaType alphaType, size_t dstRowBytes,
                                    std::shared_ptr<ColorSpace> dstColorSpace,
                                    void* dstPixels) const {
  auto pixels = source->lockPixels();
  if (pixels == nullptr) {
    return false;
  }
  auto srcPixmap = Pixmap(source->info(), pixels);
  auto dstInfo = ImageInfo::Make(width(), height(), colorType, alphaType, dstRowBytes,
                                 std::move(dstColorSpace));
  auto result = srcPixmap.readPixels(dstInfo, dstPixels);
  source->unlockPixels();
  return result;
}

}  // namespace tgfx

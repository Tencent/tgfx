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

#include "BoxFilterDownsample.h"
#include "PixelBuffer.h"
#include "PixelBufferCodec.h"

namespace tgfx {
std::shared_ptr<PixelBufferCodec> PixelBufferCodec::Make(const std::shared_ptr<PixelBuffer>& source,
                                                         int width, int height) {
  if (!source || width <= 0 || height <= 0) {
    return nullptr;
  }
  auto image = std::make_shared<PixelBufferCodec>(source, width, height);
  return image;
}

bool PixelBufferCodec::onReadPixels(const ImageInfo& dstInfo, void* dstPixels) const {
  if (dstInfo.width() > source->width() || dstInfo.height() > source->height()) {
    return false;
  }
  auto pixels = source->lockPixels();
  if (pixels == nullptr) {
    return false;
  }
  if (dstInfo.width() == source->width() && dstInfo.height() == source->height()) {
    auto srcPixmap = Pixmap(source->info(), pixels);
    return srcPixmap.readPixels(dstInfo, dstPixels);
  }
  auto result = BoxFilterDownsample(pixels, source->info(), dstPixels, dstInfo);
  source->unlockPixels();
  return result;
}

}  // namespace tgfx
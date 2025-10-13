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

#include "RenderTarget.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/GPU.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Pixmap.h"

namespace tgfx {
static void CopyPixels(const ImageInfo& srcInfo, const void* srcPixels, const ImageInfo& dstInfo,
                       void* dstPixels, bool flipY) {
  auto pixels = srcPixels;
  Buffer tempBuffer = {};
  if (flipY) {
    tempBuffer.alloc(srcInfo.byteSize());
    auto rowCount = static_cast<size_t>(srcInfo.height());
    auto rowBytes = srcInfo.rowBytes();
    auto dst = tempBuffer.bytes();
    for (size_t i = 0; i < rowCount; i++) {
      auto src = reinterpret_cast<const uint8_t*>(srcPixels) + (rowCount - i - 1) * rowBytes;
      memcpy(dst, src, rowBytes);
      dst += rowBytes;
    }
    pixels = tempBuffer.data();
  }
  Pixmap pixmap(srcInfo, pixels);
  pixmap.readPixels(dstInfo, dstPixels);
}

bool RenderTarget::readPixels(const ImageInfo& dstInfo, void* dstPixels, int srcX, int srcY) const {
  dstPixels = dstInfo.computeOffset(dstPixels, -srcX, -srcY);
  auto outInfo = dstInfo.makeIntersect(-srcX, -srcY, width(), height());
  if (outInfo.isEmpty()) {
    return false;
  }
  auto colorType = PixelFormatToColorType(format());
  auto flipY = origin() == ImageOrigin::BottomLeft;
  auto srcInfo =
      ImageInfo::Make(outInfo.width(), outInfo.height(), colorType, AlphaType::Premultiplied);
  void* pixels = nullptr;
  size_t rowBytes = 0;
  Buffer tempBuffer = {};
  if (flipY || dstInfo.alphaType() != srcInfo.alphaType() ||
      dstInfo.colorType() != srcInfo.colorType()) {
    tempBuffer.alloc(srcInfo.byteSize());
    pixels = tempBuffer.data();
    rowBytes = srcInfo.rowBytes();
  } else {
    pixels = dstPixels;
    rowBytes = dstInfo.rowBytes();
  }
  auto readX = std::max(0, srcX);
  auto readY = std::max(0, srcY);
  if (flipY) {
    readY = height() - readY - outInfo.height();
  }
  auto gpu = getContext()->gpu();
  auto rect = Rect::MakeXYWH(readX, readY, outInfo.width(), outInfo.height());
  if (!gpu->queue()->readTexture(getSampleTexture(), rect, pixels, rowBytes)) {
    return false;
  }
  if (!tempBuffer.isEmpty()) {
    CopyPixels(srcInfo, tempBuffer.data(), outInfo, dstPixels, flipY);
  }
  return true;
}

}  // namespace tgfx
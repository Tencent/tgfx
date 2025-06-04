/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "core/PathRasterizer.h"
#include "core/ScalerContext.h"

namespace tgfx {
PathRasterizer::PathRasterizer(int width, int height, std::shared_ptr<Shape> shape, bool antiAlias,
                               bool needsGammaCorrection)
    : ImageCodec(width, height, Orientation::LeftTop), shape(std::move(shape)),
      antiAlias(antiAlias), needsGammaCorrection(needsGammaCorrection) {
}

void PathRasterizer::ClearPixels(const ImageInfo& dstInfo, void* dstPixels, const Rect& bounds,
                                 bool flipY) {
  auto dstBounds = Rect::MakeWH(dstInfo.width(), dstInfo.height());
  if (bounds.contains(dstBounds)) {
    memset(dstPixels, 0, dstInfo.byteSize());
    return;
  }
  dstBounds.intersect(bounds);
  auto left = static_cast<size_t>(dstBounds.left);
  auto width = static_cast<size_t>(dstBounds.width());
  size_t top = 0, bottom = 0;
  if (flipY) {
    top = static_cast<size_t>(dstInfo.height()) - static_cast<size_t>(dstBounds.bottom);
    bottom = top + static_cast<size_t>(dstBounds.height());
  } else {
    top = static_cast<size_t>(dstBounds.top);
    bottom = static_cast<size_t>(dstBounds.bottom);
  }
  for (auto y = top; y < bottom; ++y) {
    auto row =
        static_cast<uint8_t*>(dstPixels) + y * dstInfo.rowBytes() + left * dstInfo.bytesPerPixel();
    memset(row, 0, width * dstInfo.bytesPerPixel());
  }
}

}  //namespace tgfx

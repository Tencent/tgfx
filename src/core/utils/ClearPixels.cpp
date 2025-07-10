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

#include "ClearPixels.h"

namespace tgfx {
void ClearPixels(const ImageInfo& dstInfo, void* dstPixels) {
  if (dstInfo.rowBytes() == dstInfo.minRowBytes()) {
    memset(dstPixels, 0, dstInfo.byteSize());
    return;
  }
  auto height = static_cast<size_t>(dstInfo.height());
  for (size_t y = 0; y < height; ++y) {
    auto row = static_cast<uint8_t*>(dstPixels) + y * dstInfo.rowBytes();
    memset(row, 0, dstInfo.minRowBytes());
  }
}
}  // namespace tgfx

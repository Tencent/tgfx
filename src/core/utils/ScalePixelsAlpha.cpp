/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "ScalePixelsAlpha.h"
#include <cmath>

namespace tgfx {
void ScalePixelsAlpha(const ImageInfo& info, void* pixels, float alphaScale) {
  if (alphaScale >= 1.f) {
    return;
  }
  auto buffer = static_cast<unsigned char*>(pixels);
  for (size_t y = 0; y < static_cast<size_t>(info.height()); ++y) {
    for (size_t x = 0; x < static_cast<size_t>(info.width()); ++x) {
      auto& alpha = buffer[(y * info.rowBytes()) + x];
      alpha = static_cast<unsigned char>(std::lroundf(alpha * alphaScale));
    }
  }
}
}  // namespace tgfx

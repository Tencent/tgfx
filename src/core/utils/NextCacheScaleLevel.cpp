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

#include "NextCacheScaleLevel.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tgfx {

float NextCacheScaleLevel(float scale) {
  constexpr float MinAllowedImageScale = 1.0f / 8.0f;
  if (scale <= MinAllowedImageScale) {
    return MinAllowedImageScale;
  }
  if (scale > 0.5f) {
    return 1.0f;
  }
  float exactLevel = std::log2(1.0f / scale);
  auto scaleLevel = static_cast<uint32_t>(std::floor(exactLevel));
  return 1.0f / static_cast<float>(1 << scaleLevel);
}

}  // namespace tgfx

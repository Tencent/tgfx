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

#include "CacheScaleLevel.h"
#include <algorithm>

namespace tgfx {

uint32_t GetCacheScaleLevel(float scale) {
  scale = std::clamp(scale, 0.0f, 1.0f);
  if (scale > 0.5f) {
    return 0;
  }
  float exactLevel = std::log2(1.0f / scale);
  return static_cast<uint32_t>(std::floor(exactLevel));
}

}  // namespace tgfx
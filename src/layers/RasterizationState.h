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

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace tgfx {
// Present only during explicit pixel export. The regular display/cache paths retain their policy.
struct RasterizationState {
  bool failed = false;
  int textureLimit = 0;
  size_t surfacePixelLimit = 0;

  bool checkSize(double width, double height) {
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 ||
        width > textureLimit || height > textureLimit ||
        static_cast<uint64_t>(width) * static_cast<uint64_t>(height) > surfacePixelLimit) {
      failed = true;
      return false;
    }
    return true;
  }
};
}  // namespace tgfx

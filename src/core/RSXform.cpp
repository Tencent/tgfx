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

#include "tgfx/core/RSXform.h"
#include <cmath>

namespace tgfx {

RSXform RSXform::MakeFromRadians(float scale, float radians, float tx, float ty, float ax,
                                 float ay) {
  const float s = std::sin(radians) * scale;
  const float c = std::cos(radians) * scale;
  return Make(c, s, tx + -c * ax + s * ay, ty + -s * ax - c * ay);
}

}  // namespace tgfx

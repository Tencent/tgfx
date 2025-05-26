/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/RRect.h"

namespace tgfx {
bool RRect::isRect() const {
  return radii.x == 0.0f && radii.y == 0.0f;
}

bool RRect::isOval() const {
  return radii.x >= rect.width() * 0.5f && radii.y >= rect.height() * 0.5f;
}

void RRect::setRectXY(const Rect& r, float radiusX, float radiusY) {
  rect = r.makeSorted();
  if (radiusX < 0 || radiusY < 0) {
    radiusX = radiusY = 0;
  }
  if (r.width() < radiusX + radiusX || r.height() < radiusY + radiusY) {
    float scale = std::min(r.width() / (radiusX + radiusX), r.height() / (radiusY + radiusY));
    radiusX *= scale;
    radiusY *= scale;
  }
  radii = {radiusX, radiusY};
}

void RRect::setOval(const Rect& oval) {
  rect = oval.makeSorted();
  radii = {rect.width() / 2, rect.height() / 2};
}

void RRect::scale(float scaleX, float scaleY) {
  rect.scale(scaleX, scaleY);
  radii.x *= scaleX;
  radii.y *= scaleY;
}
}  // namespace tgfx

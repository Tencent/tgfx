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

#include "OpaqueBoundsHelper.h"
#include <algorithm>

namespace tgfx {

bool OpaqueBoundsHelper::Contains(const std::vector<Rect>& opaqueBounds, const Rect& bounds) {
  return bounds.isEmpty() || std::any_of(opaqueBounds.begin(), opaqueBounds.end(),
                                         [&](const Rect& rect) { return rect.contains(bounds); });
}

Rect OpaqueBoundsHelper::GetMaxOverlapRect(const Rect& rect1, const Rect& rect2) {
  auto intersect = rect1;
  if (intersect.intersect(rect2)) {
    float left = std::min(rect1.left, rect2.left);
    float top = std::min(rect1.top, rect2.top);
    float right = std::max(rect1.right, rect2.right);
    float bottom = std::max(rect1.bottom, rect2.bottom);
    auto overlap1 = Rect::MakeLTRB(intersect.left, top, intersect.right, bottom);
    auto overlap2 = Rect::MakeLTRB(left, intersect.top, right, intersect.bottom);
    return overlap1.area() > overlap2.area() ? overlap1 : overlap2;
  }
  return Rect::MakeEmpty();
}

void OpaqueBoundsHelper::Merge(std::vector<Rect>& opaqueBounds, const Rect& bounds) {
  if (opaqueBounds.size() < 3) {
    opaqueBounds.emplace_back(bounds);
    if (opaqueBounds.size() == 3) {
      std::sort(opaqueBounds.begin(), opaqueBounds.end(),
                [](const Rect& a, const Rect& b) { return a.area() > b.area(); });
    }
    return;
  }
  size_t bestIdx = 0;
  float maxOverlapArea = 0;
  Rect bestOverlap = Rect::MakeEmpty();
  for (size_t i = 0; i < opaqueBounds.size(); ++i) {
    const auto& r = opaqueBounds[i];
    auto overlap = GetMaxOverlapRect(r, bounds);
    float overlapArea = overlap.area();
    if (overlapArea < bounds.area()) {
      continue;
    }
    if (overlapArea > maxOverlapArea) {
      maxOverlapArea = overlapArea;
      bestIdx = i;
      bestOverlap = overlap;
    }
  }
  if (maxOverlapArea > 0) {
    opaqueBounds[bestIdx] = bestOverlap;
  } else {
    if (bounds.area() > opaqueBounds.back().area()) {
      opaqueBounds.back() = bounds;
    }
  }
  std::sort(opaqueBounds.begin(), opaqueBounds.end(),
            [](const Rect& a, const Rect& b) { return a.area() > b.area(); });
}

}  // namespace tgfx

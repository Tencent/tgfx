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

#include "RootLayer.h"
#include <limits>
#include "core/utils/DecomposeRects.h"
#include "core/utils/Log.h"
#include "layers/DrawArgs.h"

namespace tgfx {
// Areas below are accumulated in double: a float product overflows to infinity once the product of
// the two edges exceeds FLT_MAX, which used to turn merge costs into NaN and stall the dirty list
// convergence.
static double RectArea(const Rect& rect) {
  return (static_cast<double>(rect.right) - static_cast<double>(rect.left)) *
         (static_cast<double>(rect.bottom) - static_cast<double>(rect.top));
}

// Every rect in the dirty list is non-empty (filtered by invalidateRect), so join() yields the
// plain min/max union here.
static double UnionArea(const Rect& rect1, const Rect& rect2) {
  auto bounds = rect1;
  bounds.join(rect2);
  return RectArea(bounds);
}

std::shared_ptr<RootLayer> RootLayer::Make() {
  return std::shared_ptr<RootLayer>(new RootLayer());
}

RootLayer::~RootLayer() {
  // Immediately trigger onDetachFromRoot() for all children to prevent them from calling
  // root->invalidateRect() after this object has been destroyed.
  removeChildren();
}

void RootLayer::invalidateRect(const Rect& rect) {
  if (rect.isEmpty()) {
    return;
  }
  DEBUG_ASSERT(dirtyRects.size() <= MAX_DIRTY_REGIONS);
  dirtyRects.push_back(rect);
  dirtyAreas.push_back(RectArea(rect));
  mergeDirtyList(dirtyRects.size() == MAX_DIRTY_REGIONS + 1);
}

bool RootLayer::mergeDirtyList(bool forceMerge) {
  // Merge the pair of rectangles that increases the total area the least.
  auto dirtySize = dirtyRects.size();
  if (dirtySize <= 1) {
    return false;
  }
  auto bestDelta = forceMerge ? std::numeric_limits<double>::infinity() : 0.0;
  size_t mergeA = 0;
  // A forced merge must always produce a pair: non-finite rect coordinates can make every delta a
  // NaN, which loses every comparison below. Default to the first pair so the dirty list still
  // converges, while keeping the cheapest-merge choice intact whenever the deltas are comparable.
  size_t mergeB = forceMerge ? 1 : 0;
  for (size_t i = 0; i < dirtySize; i++) {
    for (size_t j = i + 1; j < dirtySize; j++) {
      auto delta = UnionArea(dirtyRects[i], dirtyRects[j]) - dirtyAreas[i] - dirtyAreas[j];
      if (bestDelta > delta) {
        mergeA = i;
        mergeB = j;
        bestDelta = delta;
      }
    }
  }
  if (mergeA != mergeB) {
    dirtyRects[mergeA].join(dirtyRects[mergeB]);
    dirtyAreas[mergeA] = RectArea(dirtyRects[mergeA]);
    dirtyRects.erase(dirtyRects.begin() + static_cast<long>(mergeB));
    dirtyAreas.erase(dirtyAreas.begin() + static_cast<long>(mergeB));
    return true;
  }
  return false;
}

bool RootLayer::invalidateBackground(const Rect& drawRect, LayerStyle* layerStyle,
                                     float contentScale, const std::vector<Rect>& sourceRects) {
  if (sourceRects.empty()) {
    return false;
  }
  std::vector<Rect> dirtyBackgrounds = {};
  dirtyBackgrounds.reserve(sourceRects.size());
  for (const auto& sourceRect : sourceRects) {
    auto background = sourceRect;
    if (background.intersect(drawRect)) {
      if (layerStyle == nullptr) {
        return true;
      }
      background = layerStyle->filterBackground(background, contentScale);
      if (background.intersect(drawRect)) {
        dirtyBackgrounds.push_back(background);
      }
    }
  }
  for (auto& rect : dirtyBackgrounds) {
    invalidateRect(rect);
  }
  return !dirtyBackgrounds.empty();
}

std::vector<Rect> RootLayer::updateDirtyRegions() {
  updateRenderBounds();
  while (mergeDirtyList(false)) {
  }
  dirtyAreas.clear();
  DecomposeRects(dirtyRects.data(), dirtyRects.size());
  return std::move(dirtyRects);
}

}  // namespace tgfx

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

#include "RootLayer.h"
#include <limits>
#include "core/utils/Log.h"

namespace tgfx {
static float UnionArea(const Rect& rect1, const Rect& rect2) {
  auto left = rect1.left < rect2.left ? rect1.left : rect2.left;
  auto right = rect1.right > rect2.right ? rect1.right : rect2.right;
  auto top = rect1.top < rect2.top ? rect1.top : rect2.top;
  auto bottom = rect1.bottom > rect2.bottom ? rect1.bottom : rect2.bottom;
  return (right - left) * (bottom - top);
}

static float RectArea(const Rect& rect) {
  return (rect.right - rect.left) * (rect.bottom - rect.top);
}

// Restructure two overlapping rectangles to remove their intersection while still covering the same
// or a larger area
static void DecomposeRect(Rect* rectA, Rect* rectB) {
  // Ensure rectA and rectB overlap
  DEBUG_ASSERT(rectA->intersects(*rectB));

  // Step 1: Build the 3 rect slabs on the y-axis
  Rect rects[3];
  if (rectA->top < rectB->top) {
    rects[0].top = rectA->top;
    rects[0].bottom = rectB->top;
    rects[0].left = rectA->left;
    rects[0].right = rectA->right;
  } else {
    rects[0].top = rectB->top;
    rects[0].bottom = rectA->top;
    rects[0].left = rectB->left;
    rects[0].right = rectB->right;
  }
  if (rectA->bottom < rectB->bottom) {
    rects[2].top = rectA->bottom;
    rects[2].bottom = rectB->bottom;
    rects[2].left = rectB->left;
    rects[2].right = rectB->right;
  } else {
    rects[2].top = rectB->bottom;
    rects[2].bottom = rectA->bottom;
    rects[2].left = rectA->left;
    rects[2].right = rectA->right;
  }
  rects[1].top = rects[0].bottom;
  rects[1].bottom = rects[2].top;
  rects[1].left = std::min(rectA->left, rectB->left);
  rects[1].right = std::max(rectA->right, rectB->right);

  // Step 2: Compute areas
  float areas[3];
  for (int i = 0; i < 3; ++i) {
    areas[i] = RectArea(rects[i]);
  }

  // Step 3: Union slabs and compute area deltas
  Rect u1 = rects[0];
  u1.join(rects[1]);
  Rect u2 = rects[1];
  u2.join(rects[2]);
  float delta0 = areas[0] + areas[1] - RectArea(u1);
  float delta1 = areas[1] + areas[2] - RectArea(u2);

  // Step 4: Choose the best decomposition
  if (delta0 > delta1) {
    *rectA = u1;
    *rectB = rects[2];
  } else {
    *rectA = rects[0];
    *rectB = u2;
  }
}

std::shared_ptr<RootLayer> RootLayer::Make() {
  auto layer = std::shared_ptr<RootLayer>(new RootLayer());
  layer->weakThis = layer;
  return layer;
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
  float bestDelta = forceMerge ? std::numeric_limits<float>::max() : 0;
  size_t mergeA = 0;
  size_t mergeB = 0;
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
                                     float contentScale) {
  if (dirtyRects.empty()) {
    return false;
  }
  auto dirtySize = dirtyRects.size();
  std::vector<Rect> dirtyBackgrounds = {};
  dirtyBackgrounds.reserve(dirtySize);
  for (size_t i = 0; i < dirtySize; i++) {
    auto background = dirtyRects[i];
    if (background.intersect(drawRect)) {
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
  auto rectSize = dirtyRects.size();
  for (size_t i = 0; i < rectSize; i++) {
    for (size_t j = i + 1; j < rectSize; j++) {
      if (dirtyRects[i].intersects(dirtyRects[j])) {
        DecomposeRect(&dirtyRects[i], &dirtyRects[j]);
      }
    }
  }
  return std::move(dirtyRects);
}

}  // namespace tgfx

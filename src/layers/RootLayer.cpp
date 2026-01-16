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
static float UnionArea(const Rect& rect1, const Rect& rect2) {
  auto left = rect1.left < rect2.left ? rect1.left : rect2.left;
  auto right = rect1.right > rect2.right ? rect1.right : rect2.right;
  auto top = rect1.top < rect2.top ? rect1.top : rect2.top;
  auto bottom = rect1.bottom > rect2.bottom ? rect1.bottom : rect2.bottom;
  return (right - left) * (bottom - top);
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
  dirtyAreas.push_back(rect.area());
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
    dirtyAreas[mergeA] = dirtyRects[mergeA].area();
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

bool RootLayer::drawLayer(const DrawArgs& args, Canvas* canvas, float alpha, BlendMode blendMode,
                          const Matrix3D* transform3D) {
  auto color = _backgroundColor;
  color.alpha = color.alpha * alpha;
  canvas->drawColor(color, blendMode);
  return Layer::drawLayer(args, canvas, alpha, blendMode, transform3D);
}

bool RootLayer::setBackgroundColor(const Color& color) {
  if (_backgroundColor == color) {
    return false;
  }
  _backgroundColor = color;
  invalidateContent();
  return true;
}

}  // namespace tgfx

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/layers/DisplayList.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "layers/DrawArgs.h"
#include "layers/RootLayer.h"

namespace tgfx {
static constexpr size_t MAX_DIRTY_REGION_FRAMES = 5;

DisplayList::DisplayList() : _root(RootLayer::Make()) {
  _root->_root = _root.get();
}

Layer* DisplayList::root() const {
  return _root.get();
}

void DisplayList::setZoomScale(float zoomScale) {
  if (zoomScale == _zoomScale) {
    return;
  }
  _hasContentChanged = true;
  _zoomScale = zoomScale;
}

void DisplayList::setContentOffset(float offsetX, float offsetY) {
  if (offsetX == _contentOffset.x && offsetY == _contentOffset.y) {
    return;
  }
  _hasContentChanged = true;
  _contentOffset.x = offsetX;
  _contentOffset.y = offsetY;
}

void DisplayList::setPartialRefreshEnabled(bool partialRefreshEnabled) {
  if (_partialRefreshEnabled == partialRefreshEnabled) {
    return;
  }
  _partialRefreshEnabled = partialRefreshEnabled;
  if (!_partialRefreshEnabled) {
    frameCache = nullptr;
  }
}

void DisplayList::showDirtyRegions(bool show) {
  if (_showDirtyRegions == show) {
    return;
  }
  _showDirtyRegions = show;
  if (!_showDirtyRegions) {
    lastDirtyRegions = {};
  }
}

bool DisplayList::hasContentChanged() const {
  if (_hasContentChanged || _root->bitFields.dirtyDescendents) {
    return true;
  }
  if (!_showDirtyRegions) {
    return false;
  }
  for (auto& regions : lastDirtyRegions) {
    if (!regions.empty()) {
      return true;
    }
  }
  return false;
}

void DisplayList::render(Surface* surface, bool autoClear) {
  if (!surface) {
    return;
  }
  _hasContentChanged = false;
  auto dirtyRegions = _root->updateDirtyRegions();
  if (FloatNearlyZero(_zoomScale)) {
    return;
  }
  if (_partialRefreshEnabled && renderPartially(surface, autoClear, std::move(dirtyRegions))) {
    return;
  }
  auto viewMatrix = getViewMatrix();
  auto inverse = Matrix::I();
  DEBUG_ASSERT(viewMatrix.invertible());
  viewMatrix.invert(&inverse);
  auto canvas = surface->getCanvas();
  canvas->setMatrix(viewMatrix);
  if (autoClear) {
    canvas->clear();
  }
  DrawArgs args(surface->getContext());
  auto renderRect = Rect::MakeWH(surface->width(), surface->height());
  renderRect = inverse.mapRect(renderRect);
  args.renderRect = &renderRect;
  _root->drawLayer(args, canvas, 1.0f, BlendMode::SrcOver);
}

bool DisplayList::renderPartially(Surface* surface, bool autoClear,
                                  std::vector<Rect> dirtyRegions) {
  auto context = surface->getContext();
  bool frameCacheChanged = false;
  if (frameCache == nullptr || frameCache->getContext() != context ||
      frameCache->width() != surface->width() || frameCache->height() != surface->height()) {
    frameCache = Surface::Make(context, surface->width(), surface->height(), ColorType::RGBA_8888,
                               1, false, surface->renderFlags());
    if (frameCache == nullptr) {
      return false;
    }
    frameCacheChanged = true;
  }
  auto viewMatrix = getViewMatrix();
  auto surfaceRect = Rect::MakeWH(surface->width(), surface->height());
  if (frameCacheChanged || lastZoomScale != _zoomScale || lastContentOffset != _contentOffset) {
    dirtyRegions = {surfaceRect};
    lastZoomScale = _zoomScale;
    lastContentOffset = _contentOffset;
  } else {
    for (auto& region : dirtyRegions) {
      viewMatrix.mapRect(&region);  // Map the region to the surface coordinate system.
      region.outset(0.5f, 0.5f);    // Expand by 0.5 pixels to preserve antialiasing results.
      region.roundOut();
      if (!region.intersect(surfaceRect)) {
        region.setEmpty();
      }
    }
  }
  auto inverse = Matrix::I();
  DEBUG_ASSERT(viewMatrix.invertible());
  viewMatrix.invert(&inverse);
  auto cacheCanvas = frameCache->getCanvas();
  auto canvas = surface->getCanvas();
  for (auto& region : dirtyRegions) {
    if (region.isEmpty()) {
      continue;
    }
    AutoCanvasRestore autoRestore(cacheCanvas);
    cacheCanvas->clipRect(region);
    cacheCanvas->clear();
    cacheCanvas->setMatrix(viewMatrix);
    DrawArgs args(context);
    auto renderRect = inverse.mapRect(region);
    args.renderRect = &renderRect;
    _root->drawLayer(args, cacheCanvas, 1.0f, BlendMode::SrcOver);
  }
  canvas->resetMatrix();
  Paint paint = {};
  paint.setAntiAlias(false);
  if (autoClear) {
    paint.setBlendMode(BlendMode::Src);
  }
  static SamplingOptions sampling(FilterMode::Nearest);
  canvas->drawImage(frameCache->makeImageSnapshot(), sampling, &paint);
  if (_showDirtyRegions) {
    renderDirtyRegions(canvas, std::move(dirtyRegions));
  }
  return true;
}

void DisplayList::renderDirtyRegions(Canvas* canvas, std::vector<Rect> dirtyRegions) {
  if (lastDirtyRegions.empty()) {
    for (size_t i = 0; i < MAX_DIRTY_REGION_FRAMES; ++i) {
      lastDirtyRegions.emplace_back();
    }
  }
  lastDirtyRegions.push_back(std::move(dirtyRegions));
  lastDirtyRegions.pop_front();
  Paint dirtyPaint = {};
  dirtyPaint.setAntiAlias(false);
  dirtyPaint.setColor(Color::Red());
  dirtyPaint.setStyle(PaintStyle::Stroke);
  for (auto& regions : lastDirtyRegions) {
    for (auto& region : regions) {
      if (region.isEmpty()) {
        continue;
      }
      region.inset(0.25f, 0.25f);
      canvas->drawRect(region, dirtyPaint);
    }
  }
}

Matrix DisplayList::getViewMatrix() const {
  auto viewMatrix = Matrix::MakeScale(_zoomScale);
  viewMatrix.postTranslate(_contentOffset.x, _contentOffset.y);
  return viewMatrix;
}

}  // namespace tgfx

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
#include "layers/DrawArgs.h"
#include "layers/RootLayer.h"

namespace tgfx {

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

bool DisplayList::hasContentChanged() const {
  return _hasContentChanged || _root->bitFields.dirtyDescendents;
}

void DisplayList::render(Surface* surface, bool autoClear) {
  if (!surface) {
    return;
  }
  _hasContentChanged = false;
  auto dirtyRegions = _root->updateDirtyRegions();
  auto viewMatrix = Matrix::MakeScale(_zoomScale);
  viewMatrix.postTranslate(_contentOffset.x, _contentOffset.y);
  auto inverse = Matrix::I();
  if (!viewMatrix.invert(&inverse)) {
    return;
  }
  auto renderRect = Rect::MakeWH(surface->width(), surface->height());
  renderRect = inverse.mapRect(renderRect);
  auto canvas = surface->getCanvas();
  canvas->setMatrix(viewMatrix);
  if (_partialRefreshEnabled &&
      renderPartially(surface, autoClear, renderRect, std::move(dirtyRegions))) {
    return;
  }
  if (autoClear) {
    canvas->clear();
  }
  DrawArgs args(surface->getContext());
  args.renderRect = &renderRect;
  _root->drawLayer(args, canvas, 1.0f, BlendMode::SrcOver);
}

bool DisplayList::renderPartially(Surface* surface, bool autoClear, const Rect& renderRect,
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
  if (frameCacheChanged || lastZoomScale != _zoomScale || lastContentOffset != _contentOffset) {
    dirtyRegions = {renderRect};
    lastZoomScale = _zoomScale;
    lastContentOffset = _contentOffset;
  } else {
    DEBUG_ASSERT(_zoomScale != 0);
    auto expand = 1.0f / _zoomScale;
    for (auto& region : dirtyRegions) {
      if (!region.intersects(renderRect)) {
        region.setEmpty();
        continue;
      }
      // Add a small margin to avoid artifacts caused by antialiasing.
      region.outset(expand, expand);
      region.intersect(renderRect);
    }
  }
  auto cacheCanvas = frameCache->getCanvas();
  auto canvas = surface->getCanvas();
  cacheCanvas->setMatrix(canvas->getMatrix());
  DrawArgs args(context);
  for (auto& region : dirtyRegions) {
    if (region.isEmpty()) {
      continue;
    }
    AutoCanvasRestore autoRestore(cacheCanvas);
    args.renderRect = &region;
    cacheCanvas->clipRect(region);
    cacheCanvas->clear();
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
  return true;
}

}  // namespace tgfx

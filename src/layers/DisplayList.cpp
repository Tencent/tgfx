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
#include "layers/DrawArgs.h"
#include "layers/RootLayer.h"

#ifdef TGFX_USE_INSPECTOR
#include "tgfx/layers/LayerInspector.h"
#endif

namespace tgfx {

DisplayList::DisplayList() : _root(RootLayer::Make()) {
  _root->_root = _root.get();
#ifdef TGFX_USE_INSPECTOR
  auto& layerInspector = LayerInspector::GetLayerInspector();
  layerInspector.setDisplayList(this);
#endif
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

bool DisplayList::hasContentChanged() const {
  return _hasContentChanged || _root->bitFields.dirtyDescendents;
}

void DisplayList::render(Surface* surface, bool autoClear) {
  if (!surface) {
    return;
  }
  _hasContentChanged = false;
  _root->updateDirtyRegions();
  auto canvas = surface->getCanvas();
  if (autoClear) {
    canvas->clear();
  }
  auto matrix = Matrix::MakeScale(_zoomScale);
  matrix.postTranslate(_contentOffset.x, _contentOffset.y);
  canvas->setMatrix(matrix);
  auto inverse = Matrix::I();
  if (!matrix.invert(&inverse)) {
    return;
  }
  auto renderRect = Rect::MakeWH(surface->width(), surface->height());
  renderRect = inverse.mapRect(renderRect);
  DrawArgs args(surface->getContext());
  args.renderRect = &renderRect;
  _root->drawLayer(args, canvas, 1.0f, BlendMode::SrcOver);

#ifdef TGFX_USE_INSPECTOR
  auto& layerInspector = LayerInspector::GetLayerInspector();
  layerInspector.setCallBack();
  layerInspector.RenderImageAndSend(surface->getContext());
#endif
}

}  // namespace tgfx

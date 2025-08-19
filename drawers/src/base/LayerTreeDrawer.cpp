/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "LayerTreeDrawer.h"
#include "tgfx/layers/DisplayList.h"

namespace drawers {
LayerTreeDrawer::LayerTreeDrawer(const std::string& name) : Drawer(name + "Drawer") {
}

void LayerTreeDrawer::onDraw(tgfx::Canvas* canvas, const AppHost* host) {
  if (!root) {
    root = buildLayerTree(host);
    displayList.root()->addChild(root);
    displayList.setRenderMode(tgfx::RenderMode::Tiled);
    displayList.setAllowZoomBlur(true);
    displayList.setMaxTileCount(512);
  }
  updateRootMatrix(host);
  displayList.setZoomScale(host->zoomScale());
  auto offset = host->contentOffset();
  displayList.setContentOffset(offset.x, offset.y);
  displayList.render(canvas->getSurface(), false);
}

void LayerTreeDrawer::updateRootMatrix(const AppHost* host) {
  auto padding = 30.0;
  auto bounds = root->getBounds(nullptr, true);
  auto totalScale = std::min(host->width() / (padding * 2 + bounds.width()),
                             host->height() / (padding * 2 + bounds.height()));

  auto rootMatrix = tgfx::Matrix::MakeScale(static_cast<float>(totalScale));
  rootMatrix.postTranslate(static_cast<float>((host->width() - bounds.width() * totalScale) / 2),
                           static_cast<float>((host->height() - bounds.height() * totalScale) / 2));

  root->setMatrix(rootMatrix);
}

std::vector<std::shared_ptr<tgfx::Layer>> LayerTreeDrawer::getLayersUnderPoint(float x,
                                                                               float y) const {
  return displayList.root()->getLayersUnderPoint(x, y);
}
}  // namespace drawers

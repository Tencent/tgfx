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

#include "drawers/LayerTreeDrawer.h"
#include "tgfx/layers/DisplayList.h"

namespace drawers {

LayerTreeDrawer::LayerTreeDrawer(const std::string& name) : Drawer(name + "Drawer") {
}

bool LayerTreeDrawer::render(tgfx::Surface* surface, const AppHost* host) {
  return renderInternal(surface, host, true);
}

void LayerTreeDrawer::onClick(float, float) {
}

void LayerTreeDrawer::onDraw(tgfx::Canvas* canvas, const AppHost* host) {
  renderInternal(canvas->getSurface(), host, false);
}

void LayerTreeDrawer::updateRootMatrix(const AppHost* host) {
  auto padding = 30.0;
  auto bounds = root->getBounds();
  auto totalScale = std::min(host->width() / (padding * 2 + bounds.width()),
                             host->height() / (padding * 2 + bounds.height()));

  auto rootMatrix = tgfx::Matrix::MakeScale(static_cast<float>(totalScale));
  rootMatrix.postTranslate(static_cast<float>((host->width() - bounds.width() * totalScale) / 2),
                           static_cast<float>((host->height() - bounds.height() * totalScale) / 2));

  root->setMatrix(rootMatrix);
}

bool LayerTreeDrawer::renderInternal(tgfx::Surface* surface, const AppHost* host, bool replaceAll) {
  if (!root) {
    root = buildLayerTree(host);
    displayList.root()->addChild(root);
  }
  updateRootMatrix(host);
  return displayList.render(surface, replaceAll);
}

std::vector<std::shared_ptr<tgfx::Layer>> LayerTreeDrawer::click(float x, float y) {
  onClick(x, y);
  return displayList.root()->getLayersUnderPoint(x, y);
}
}  // namespace drawers

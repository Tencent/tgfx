/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "drawers/Drawer.h"
#include <unordered_map>
#include "GridBackground.h"
#include "base/Drawers.h"
#include "tgfx/platform/Print.h"

namespace drawers {
static std::vector<Drawer*> drawers = { new SimpleText(),
                                       };

static std::vector<std::string> GetDrawerNames() {
  std::vector<std::string> names;
  for (const auto& drawer : drawers) {
    names.push_back(drawer->name());
  }
  return names;
}

static std::unordered_map<std::string, Drawer*> GetDrawerMap() {
  std::unordered_map<std::string, Drawer*> map;
  for (const auto& drawer : drawers) {
    map[drawer->name()] = drawer;
  }
  return map;
}

int Drawer::Count() {
  return static_cast<int>(drawers.size());
}

const std::vector<std::string>& Drawer::Names() {
  static auto names = GetDrawerNames();
  return names;
}

Drawer* Drawer::GetByIndex(int index) {
  if (index < 0 || index >= Count()) {
    return nullptr;
  }
  return drawers[static_cast<size_t>(index)];
}

Drawer* Drawer::GetByName(const std::string& name) {
  static auto drawerMap = GetDrawerMap();
  auto it = drawerMap.find(name);
  if (it == drawerMap.end()) {
    return nullptr;
  }
  return it->second;
}

void Drawer::DrawBackground(tgfx::Canvas* canvas, const AppHost* host) {
  auto layer = GridBackgroundLayer::Make();
  layer->setSize(static_cast<float>(host->width()), static_cast<float>(host->height()),
                 host->density());
  layer->draw(canvas);
}

Drawer::Drawer(std::string name) : _name(std::move(name)) {
}

void Drawer::build(const AppHost* host) {
  if (host == nullptr) {
    tgfx::PrintError("Drawer::draw() appHost is nullptr!");
    return;
  }
  if (!_root) {
    _root = buildLayerTree(host);
    displayList.root()->addChild(_root);
    displayList.setRenderMode(tgfx::RenderMode::Tiled);
    // Zoom blur is currently enabled because the Hello2D demo now support animation frame
    // rendering with displayList:
    displayList.setAllowZoomBlur(true);
    displayList.setMaxTileCount(512);
  }
  auto bounds = _root->getBounds(nullptr, true);
  auto totalScale = std::min(static_cast<float>(host->width()) / (padding * 2 + bounds.width()),
                             static_cast<float>(host->height()) / (padding * 2 + bounds.height()));

  auto rootMatrix = tgfx::Matrix::MakeScale(totalScale);
  rootMatrix.postTranslate((static_cast<float>(host->width()) - bounds.width() * totalScale) / 2,
                           (static_cast<float>(host->height()) - bounds.height() * totalScale) / 2);
  _root->setMatrix(rootMatrix);
}
}  // namespace drawers
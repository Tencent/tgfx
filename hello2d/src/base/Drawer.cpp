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

#include "hello2d/LayerBuilder.h"
#include <unordered_map>
#include "GridBackground.h"
#include "base/Drawers.h"
#include "tgfx/platform/Print.h"

namespace hello2d {
static std::vector<LayerBuilder*> builders = {new ConicGradient(), new ImageWithMipmap(),
                                       new ImageWithShadow(), new RichText(),
                                       new SimpleLayerTree(),
                                          };

static std::vector<std::string> GetBuilderNames() {
  std::vector<std::string> names;
  for (const auto& builder : builders) {
    names.push_back(builder->name());
  }
  return names;
}

static std::unordered_map<std::string, LayerBuilder*> GetBuilderMap() {
  std::unordered_map<std::string, LayerBuilder*> map;
  for (const auto& builder : builders) {
    map[builder->name()] = builder;
  }
  return map;
}

int LayerBuilder::Count() {
  return static_cast<int>(builders.size());
}

const std::vector<std::string>& LayerBuilder::Names() {
  static auto names = GetBuilderNames();
  return names;
}

LayerBuilder* LayerBuilder::GetByIndex(int index) {
  if (index < 0 || index >= Count()) {
    return nullptr;
  }
  return builders[static_cast<size_t>(index)];
}

LayerBuilder* LayerBuilder::GetByName(const std::string& name) {
  static auto builderMap = GetBuilderMap();
  auto it = builderMap.find(name);
  if (it == builderMap.end()) {
    return nullptr;
  }
  return it->second;
}

void LayerBuilder::DrawBackground(tgfx::Canvas* canvas, const AppHost* host) {
  auto layer = GridBackgroundLayer::Make();
  layer->setSize(static_cast<float>(host->width()), static_cast<float>(host->height()),
                 host->density());
  layer->draw(canvas);
}

LayerBuilder::LayerBuilder(std::string name) : _name(std::move(name)) {
}

void LayerBuilder::build(const AppHost* host) {
  if (host == nullptr) {
    tgfx::PrintError("Drawer::draw() appHost is nullptr!");
    return;
  }
  if (!_root) {
    _root = buildLayerTree(host);
    displayList.root()->addChild(_root);
    displayList.setRenderMode(tgfx::RenderMode::Tiled);
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

}  // namespace hello2d
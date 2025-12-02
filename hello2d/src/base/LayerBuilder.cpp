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
#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include "../SimpleTextLayer.h"
#include "GridBackground.h"
#include "base/LayerBuilders.h"

namespace hello2d {
static std::vector<LayerBuilder*> layerBuilders = {
    new ConicGradient(), new ImageWithMipmap(), new ImageWithShadow(),
    new RichText(),      new SimpleLayerTree(),
};

static std::vector<std::string> GetLayerBuilderNames() {
  std::vector<std::string> names;
  for (const auto& builder : layerBuilders) {
    names.push_back(builder->name());
  }
  return names;
}

static std::unordered_map<std::string, LayerBuilder*> GetLayerBuilderMap() {
  std::unordered_map<std::string, LayerBuilder*> map;
  for (const auto& builder : layerBuilders) {
    map[builder->name()] = builder;
  }
  return map;
}

int LayerBuilder::Count() {
  return static_cast<int>(layerBuilders.size());
}

const std::vector<std::string>& LayerBuilder::Names() {
  static auto names = GetLayerBuilderNames();
  return names;
}

LayerBuilder* LayerBuilder::GetByIndex(int index) {
  if (index < 0 || index >= Count()) {
    return nullptr;
  }
  return layerBuilders[static_cast<size_t>(index)];
}

LayerBuilder* LayerBuilder::GetByName(const std::string& name) {
  static auto builderMap = GetLayerBuilderMap();
  auto it = builderMap.find(name);
  if (it == builderMap.end()) {
    return nullptr;
  }
  return it->second;
}

LayerBuilder::LayerBuilder(std::string name) : _name(std::move(name)) {
}

std::shared_ptr<tgfx::Layer> LayerBuilder::buildLayerTree(const hello2d::AppHost* host) {
  auto layer = onBuildLayerTree(host);
  if (!layer) {
    return layer;
  }

  // Apply centering and scaling transformation
  applyCenteringTransform(layer);
  return layer;
}

void LayerBuilder::applyCenteringTransform(std::shared_ptr<tgfx::Layer> layer) {
  if (!layer) {
    return;
  }

  // Calculate centered matrix with padding
  auto bounds = layer->getBounds(nullptr, true);
  if (!bounds.isEmpty()) {
    static constexpr float CONTENT_WIDTH = 620.0f;
    static constexpr float PADDING = 50.0f;
    auto scale = std::min(CONTENT_WIDTH / bounds.width(), CONTENT_WIDTH / bounds.height());
    tgfx::Matrix matrix = tgfx::Matrix::MakeScale(scale);
    matrix.postTranslate((CONTENT_WIDTH - bounds.width() * scale) * 0.5f + PADDING,
                         (CONTENT_WIDTH - bounds.height() * scale) * 0.5f + PADDING);
    layer->setMatrix(matrix);
  }
}

void DrawBackground(tgfx::Canvas* canvas, int width, int height, float density) {
  static auto layer = GridBackgroundLayer::Make();
  layer->setSize(width, height, density);
  layer->draw(canvas);
}

}  // namespace hello2d

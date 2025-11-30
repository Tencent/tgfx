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
#include "base/LayerBuilders.h"
#include "tgfx/platform/Print.h"

namespace hello2d {
static std::vector<LayerBuilder*> layerBuilders = {
    new ConicGradient(), new ImageWithMipmap(), new ImageWithShadow(),
    new RichText(),      new SimpleLayerTree(),
};

static std::unordered_map<std::string, LayerBuilder*> GetLayerBuilderMap() {
  std::unordered_map<std::string, LayerBuilder*> map;
  map.reserve(layerBuilders.size());
  for (const auto& builder : layerBuilders) {
    map[builder->name()] = builder;
  }
  return map;
}

// LayerBuilder class implementation
LayerBuilder::LayerBuilder(const std::string& name) : _name(name) {
}

// Global functions implementation
int GetLayerBuilderCount() {
  return static_cast<int>(layerBuilders.size());
}

std::vector<std::string> GetLayerBuilderNames() {
  std::vector<std::string> names;
  names.reserve(layerBuilders.size());
  for (const auto& builder : layerBuilders) {
    names.push_back(builder->name());
  }
  return names;
}

LayerBuilder* GetLayerBuilderByIndex(int index) {
  if (index < 0 || index >= GetLayerBuilderCount()) {
    return nullptr;
  }
  return layerBuilders[static_cast<size_t>(index)];
}

LayerBuilder* GetLayerBuilderByName(const std::string& name) {
  auto builderMap = GetLayerBuilderMap();
  auto it = builderMap.find(name);
  if (it == builderMap.end()) {
    return nullptr;
  }
  return it->second;
}

std::shared_ptr<tgfx::Layer> BuildAndCenterLayer(int builderIndex, const AppHost* host) {
  auto builder = GetLayerBuilderByIndex(builderIndex);
  if (!builder || !host) {
    return nullptr;
  }

  auto layer = builder->buildLayerTree(host);
  if (!layer) {
    return nullptr;
  }

  // Calculate centered matrix with padding
  auto bounds = layer->getBounds(nullptr, true);
  if (!bounds.isEmpty()) {
    float padding = 30.0f;
    float scale = std::min(static_cast<float>(host->width()) / (padding * 2 + bounds.width()),
                           static_cast<float>(host->height()) / (padding * 2 + bounds.height()));
    tgfx::Matrix matrix = tgfx::Matrix::MakeScale(scale);
    matrix.postTranslate((static_cast<float>(host->width()) - bounds.width() * scale) * 0.5f,
                         (static_cast<float>(host->height()) - bounds.height() * scale) * 0.5f);
    layer->setMatrix(matrix);
  }

  return layer;
}

void DrawSampleBackground(tgfx::Canvas* canvas, const AppHost* host) {
  static auto layer = GridBackgroundLayer::Make();
  layer->setSize(host->width(), host->height(), host->density());
  layer->draw(canvas);
}

// Backward compatibility
int GetSampleCount() {
  return GetLayerBuilderCount();
}

std::vector<std::string> GetSampleNames() {
  return GetLayerBuilderNames();
}

Sample* GetSampleByIndex(int index) {
  return GetLayerBuilderByIndex(index);
}

Sample* GetSampleByName(const std::string& name) {
  return GetLayerBuilderByName(name);
}
}  // namespace hello2d

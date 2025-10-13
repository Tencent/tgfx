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

#include "hello2d/SampleBuilder.h"
#include <unordered_map>
#include "GridBackground.h"
#include "base/LayerBuilders.h"
#include "tgfx/platform/Print.h"

namespace hello2d {
static std::vector<SampleBuilder*> sampleBuilders = {
    new ConicGradient(), new ImageWithMipmap(), new ImageWithShadow(),
    new RichText(),      new SimpleLayerTree(),
};

static std::vector<std::string> GetSampleBuilderNames() {
  std::vector<std::string> names;
  names.reserve(sampleBuilders.size());
  for (const auto& builder : sampleBuilders) {
    names.push_back(builder->name());
  }
  return names;
}

static std::unordered_map<std::string, SampleBuilder*> GetSampleBuilderMap() {
  std::unordered_map<std::string, SampleBuilder*> map;
  map.reserve(sampleBuilders.size());
  for (const auto& builder : sampleBuilders) {
    map[builder->name()] = builder;
  }
  return map;
}

int SampleBuilder::Count() {
  return static_cast<int>(sampleBuilders.size());
}

std::vector<std::string> SampleBuilder::Names() {
  return GetSampleBuilderNames();
}

SampleBuilder* SampleBuilder::GetByIndex(int index) {
  if (index < 0 || index >= Count()) {
    return nullptr;
  }
  return sampleBuilders[static_cast<size_t>(index)];
}

SampleBuilder* SampleBuilder::GetByName(const std::string& name) {
  auto builderMap = GetSampleBuilderMap();
  auto it = builderMap.find(name);
  if (it == builderMap.end()) {
    return nullptr;
  }
  return it->second;
}

void SampleBuilder::DrawBackground(tgfx::Canvas* canvas, const AppHost* host) {
  static auto layer = GridBackgroundLayer::Make();
  layer->setSize(static_cast<float>(host->width()), static_cast<float>(host->height()),
                 host->density());
  layer->draw(canvas);
}

SampleBuilder::SampleBuilder(std::string name) : _name(std::move(name)) {
}

void SampleBuilder::build(const AppHost* host) {
  if (host == nullptr) {
    return;
  }
  if (!_root) {
    _root = buildLayerTree(host);
  }
}
std::vector<std::shared_ptr<tgfx::Layer>> SampleBuilder::getLayersUnderPoint(float x, float y) {
  return _root->getLayersUnderPoint(x,y);
}
}  // namespace hello2d
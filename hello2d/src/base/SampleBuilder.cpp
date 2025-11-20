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
static std::vector<Sample*> samples = {
    new ConicGradient(), new ImageWithMipmap(), new ImageWithShadow(),
    new RichText(),      new SimpleLayerTree(),
};

static std::vector<std::string> GetSampleNames() {
  std::vector<std::string> names;
  names.reserve(samples.size());
  for (const auto& sample : samples) {
    names.push_back(sample->name());
  }
  return names;
}

static std::unordered_map<std::string, Sample*> GetSampleMap() {
  std::unordered_map<std::string, Sample*> map;
  map.reserve(samples.size());
  for (const auto& sample : samples) {
    map[sample->name()] = sample;
  }
  return map;
}

// Sample class implementation
Sample::Sample(const std::string& name) : _name(name) {
}

void Sample::build(const AppHost* host) {
  if (host == nullptr) {
    return;
  }
  if (!_root) {
    _root = buildLayerTree(host);
  }
}

std::vector<std::shared_ptr<tgfx::Layer>> Sample::getLayersUnderPoint(float x, float y) {
  return _root->getLayersUnderPoint(x, y);
}

// SampleManager class implementation
int SampleManager::Count() {
  return static_cast<int>(samples.size());
}

std::vector<std::string> SampleManager::Names() {
  return GetSampleNames();
}

Sample* SampleManager::GetByIndex(int index) {
  if (index < 0 || index >= Count()) {
    return nullptr;
  }
  return samples[static_cast<size_t>(index)];
}

Sample* SampleManager::GetByName(const std::string& name) {
  auto sampleMap = GetSampleMap();
  auto it = sampleMap.find(name);
  if (it == sampleMap.end()) {
    return nullptr;
  }
  return it->second;
}

void SampleManager::DrawBackground(tgfx::Canvas* canvas, const AppHost* host) {
  static auto layer = GridBackgroundLayer::Make();
  layer->setSize(host->width(), host->height(), host->density());
  layer->draw(canvas);
}
}  // namespace hello2d
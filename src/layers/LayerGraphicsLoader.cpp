/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "LayerGraphicsLoader.h"
#include <unordered_set>
#include "contents/LayerContent.h"
#include "core/utils/Log.h"
#include "layers/RootLayer.h"

namespace tgfx {
static constexpr size_t MIN_ASYNC_LAYER_BATCH_SIZE = 10;

class ImageGraphic : public DeferredGraphic {
 public:
  ImageGraphic(std::shared_ptr<ImageGenerator> generator, bool tryHardware)
      : generator(std::move(generator)), tryHardware(tryHardware) {
  }

  void load() override {
    imageBuffer = generator->makeBuffer(tryHardware);
  }

  const void* getSource() const override {
    return generator.get();
  }

  std::shared_ptr<ImageBuffer> getBuffer() const override {
    return imageBuffer;
  }

 private:
  std::shared_ptr<ImageGenerator> generator = nullptr;
  bool tryHardware = true;
  std::shared_ptr<ImageBuffer> imageBuffer = nullptr;
};

class ShapeGraphic : public DeferredGraphic {
 public:
  explicit ShapeGraphic(std::shared_ptr<Shape> shape) : shape(std::move(shape)) {
  }

  void load() override {
    path = shape->getPath();
  }

  const void* getSource() const override {
    return shape.get();
  }

  Path getPath() const override {
    return path;
  }

 private:
  std::shared_ptr<Shape> shape = nullptr;
  Path path = {};
};

class GraphicLoadTask : public Task {
 public:
  explicit GraphicLoadTask(std::unique_ptr<DeferredGraphic> graphic) : graphic(std::move(graphic)) {
  }

  void onExecute() override {
    graphic->load();
  }

  std::unique_ptr<DeferredGraphic> graphic = nullptr;
};

LayerGraphicsLoader::~LayerGraphicsLoader() {
  for (auto& it : loadingTasks) {
    it.second->cancel();
  }
}

bool LayerGraphicsLoader::addAsyncContent(Layer* layer, LayerContent* content) {
  if (content == nullptr) {
    return false;
  }
  DEBUG_ASSERT(layer != nullptr);
  DEBUG_ASSERT(content != nullptr);
  DEBUG_ASSERT(layerToGraphics.find(layer) == layerToGraphics.end());
  currentLayer = layer;
  auto hasDeferredGraphics = content->collectDeferredGraphics(this, context);
  if (hasDeferredGraphics) {
    pendingLayers.push_back(layer);
  }
  currentLayer = nullptr;
  return hasDeferredGraphics;
}

void LayerGraphicsLoader::cancelAsyncContent(Layer* layer) {
  completeLayers.erase(layer);
  auto result = layerToGraphics.find(layer);
  if (result == layerToGraphics.end()) {
    return;
  }
  for (auto& graphic : result->second) {
    auto graphicResult = graphicToLayers.find(graphic);
    DEBUG_ASSERT(graphicResult != graphicToLayers.end());
    auto& layers = graphicResult->second;
    layers.remove(layer);
    if (!layers.empty()) {
      continue;
    }
    graphicToLayers.erase(graphicResult);
    pendingGraphics.erase(graphic);
    auto taskResult = loadingTasks.find(graphic);
    if (taskResult != loadingTasks.end()) {
      taskResult->second->cancel();
      loadingTasks.erase(taskResult);
    }
  }
  layerToGraphics.erase(result);
}

std::shared_ptr<ImageBuffer> LayerGraphicsLoader::loadImage(
    std::shared_ptr<ImageGenerator> generator, bool tryHardware) {
  DEBUG_ASSERT(generator != nullptr);
  auto result = completeGraphics.find(generator.get());
  if (result != completeGraphics.end()) {
    return result->second->getBuffer();
  }
  if (currentLayer != nullptr) {
    addDeferredGraphic(std::make_unique<ImageGraphic>(std::move(generator), tryHardware));
  }
  return nullptr;
}

bool LayerGraphicsLoader::loadShape(std::shared_ptr<Shape> shape, Path* path) {
  DEBUG_ASSERT(shape != nullptr);
  auto result = completeGraphics.find(shape.get());
  if (result != completeGraphics.end()) {
    if (path != nullptr) {
      *path = result->second->getPath();
    }
    return true;
  }
  if (currentLayer != nullptr) {
    addDeferredGraphic(std::make_unique<ShapeGraphic>(shape));
  }
  return false;
}

void LayerGraphicsLoader::addDeferredGraphic(std::unique_ptr<DeferredGraphic> graphic) {
  auto& graphics = layerToGraphics[currentLayer];
  auto source = graphic->getSource();
  graphics.push_back(source);
  graphicToLayers[source].push_back(currentLayer);
  if (pendingGraphics.find(source) == pendingGraphics.end() &&
      loadingTasks.find(source) == loadingTasks.end()) {
    pendingGraphics[source] = std::move(graphic);
  }
}

void LayerGraphicsLoader::onAttached(Context* current) {
  context = current;
  for (auto it = loadingTasks.begin(); it != loadingTasks.end();) {
    auto& graphic = it->first;
    auto& task = it->second;
    if (task->status() == TaskStatus::Finished) {
      auto& layers = graphicToLayers[graphic];
      for (auto& layer : layers) {
        auto& graphics = layerToGraphics[layer];
        graphics.remove(graphic);
        if (graphics.empty()) {
          layerToGraphics.erase(layer);
          completeLayers.insert(layer);
        }
      }
      graphicToLayers.erase(graphic);
      completeGraphics[graphic] = std::move(task->graphic);
      it = loadingTasks.erase(it);
    } else {
      ++it;
    }
  }
  if (!loadingTasks.empty() && completeLayers.size() < MIN_ASYNC_LAYER_BATCH_SIZE) {
    return;
  }
  for (auto& layer : completeLayers) {
    layer->invalidateAsyncContent();
  }
  completeLayers.clear();
}

void LayerGraphicsLoader::onDetached() {
  context = nullptr;
  while (!pendingLayers.empty() && loadingTasks.size() < _maxAsyncGraphicsPerFrame) {
    auto layer = pendingLayers.front();
    pendingLayers.pop_front();
    auto result = layerToGraphics.find(layer);
    if (result != layerToGraphics.end()) {
      for (auto& graphic : result->second) {
        auto graphicResult = pendingGraphics.find(graphic);
        if (graphicResult == pendingGraphics.end()) {
          continue;
        }
        auto task = std::make_shared<GraphicLoadTask>(std::move(graphicResult->second));
        Task::Run(task, TaskPriority::Low);
        loadingTasks[graphic] = std::move(task);
        pendingGraphics.erase(graphicResult);
      }
    }
  }
  if (loadingTasks.empty()) {
    // Clear completeTasks only when there are no async graphics need to load, to avoid losing
    // completed graphics that haven't been processed yet.
    completeGraphics.clear();
  }
}
}  // namespace tgfx

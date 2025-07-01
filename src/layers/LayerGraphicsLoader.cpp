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
class GraphicLoadTask : public Task {
 public:
  virtual std::shared_ptr<ImageBuffer> getBuffer() const {
    return nullptr;
  }

  virtual Path getPath() const {
    return {};
  }
};

class ImageLoadTask : public GraphicLoadTask {
 public:
  ImageLoadTask(std::shared_ptr<ImageGenerator> generator, bool tryHardware)
      : generator(std::move(generator)), tryHardware(tryHardware) {
  }

  void onExecute() override {
    DEBUG_ASSERT(generator != nullptr);
    imageBuffer = generator->makeBuffer(tryHardware);
    generator = nullptr;
  }

  void onCancel() override {
    generator = nullptr;
  }

  std::shared_ptr<ImageBuffer> getBuffer() const override {
    return imageBuffer;
  }

 private:
  std::shared_ptr<ImageGenerator> generator = nullptr;
  bool tryHardware = true;
  std::shared_ptr<ImageBuffer> imageBuffer = nullptr;
};

class ShapeLoadTask : public GraphicLoadTask {
 public:
  explicit ShapeLoadTask(std::shared_ptr<Shape> shape) : shape(shape) {
  }

  void onExecute() override {
    DEBUG_ASSERT(shape != nullptr);
    path = shape->getPath();
    shape = nullptr;
  }

  void onCancel() override {
    shape = nullptr;
  }

  Path getPath() const override {
    return path;
  }

 private:
  std::shared_ptr<Shape> shape = nullptr;
  Path path = {};
};

LayerGraphicsLoader::LayerGraphicsLoader(Layer* rootLayer) {
  DEBUG_ASSERT(rootLayer != nullptr);
  addLayerContents(rootLayer);
}

void LayerGraphicsLoader::addLayerContents(Layer* layer) {
  if (layer->layerContent) {
    updateLayerContent(layer, layer->layerContent.get());
  }
  for (auto& child : layer->_children) {
    addLayerContents(child.get());
  }
}

LayerGraphicsLoader::~LayerGraphicsLoader() {
  for (auto& it : pendingTasks) {
    it.second->cancel();
  }
}

void LayerGraphicsLoader::updateLayerContent(Layer* layer, LayerContent* content) {
  auto result = layerToGraphics.find(layer);
  if (result != layerToGraphics.end()) {
    for (auto& graphic : result->second) {
      auto graphicResult = graphicToLayers.find(graphic);
      DEBUG_ASSERT(graphicResult != graphicToLayers.end());
      auto& layers = graphicResult->second;
      layers.remove(layer);
      if (!layers.empty()) {
        continue;
      }
      graphicToLayers.erase(graphicResult);
      auto taskResult = pendingTasks.find(graphic);
      if (taskResult != pendingTasks.end()) {
        taskResult->second->cancel();
        pendingTasks.erase(taskResult);
      }
      auto loadedResult = completeTasks.find(graphic);
      if (loadedResult != completeTasks.end()) {
        completeTasks.erase(loadedResult);
      }
    }
    layerToGraphics.erase(result);
  }
  if (content != nullptr) {
    DeferredGraphics deferredGraphics = {};
    content->getDeferredGraphics(&deferredGraphics);
    auto& graphics = layerToGraphics[layer];
    for (auto& image : deferredGraphics.images) {
      graphics.push_back(image.get());
      graphicToLayers[image.get()].push_back(layer);
    }
    for (auto& shape : deferredGraphics.shapes) {
      graphics.push_back(shape.get());
      graphicToLayers[shape.get()].push_back(layer);
    }
  }
}

std::shared_ptr<ImageBuffer> LayerGraphicsLoader::loadImage(
    std::shared_ptr<ImageGenerator> generator, bool tryHardware) {
  auto result = completeTasks.find(generator.get());
  if (result != completeTasks.end()) {
    return result->second->getBuffer();
  }
  if (pendingTasks.size() < _maxAsyncGraphicsPerFrame &&
      pendingTasks.find(generator.get()) == pendingTasks.end()) {
    auto task = std::make_shared<ImageLoadTask>(generator, tryHardware);
    pendingTasks[generator.get()] = task;
    Task::Run(std::move(task), TaskPriority::Low);
  }
  return nullptr;
}

Path LayerGraphicsLoader::loadShape(std::shared_ptr<Shape> shape) {
  auto result = completeTasks.find(shape.get());
  if (result != completeTasks.end()) {
    return result->second->getPath();
  }
  if (pendingTasks.size() < _maxAsyncGraphicsPerFrame &&
      pendingTasks.find(shape.get()) == pendingTasks.end()) {
    auto task = std::make_shared<ShapeLoadTask>(shape);
    pendingTasks[shape.get()] = task;
    Task::Run(std::move(task), TaskPriority::Low);
  }
  return {};
}

void LayerGraphicsLoader::onAttached() {
  std::unordered_set<Layer*> invalidLayers = {};
  for (auto it = pendingTasks.begin(); it != pendingTasks.end();) {
    auto& task = it->second;
    if (task->status() == TaskStatus::Finished) {
      auto& layers = graphicToLayers[it->first];
      for (auto& layer : layers) {
        invalidLayers.insert(layer);
      }
      completeTasks[it->first] = task;
      it = pendingTasks.erase(it);
    } else {
      ++it;
    }
  }
  for (auto& layer : invalidLayers) {
    DEBUG_ASSERT(layer->_root != nullptr);
    if (layer->contentBounds != nullptr) {
      layer->_root->invalidateRect(*layer->contentBounds);
      layer->invalidateDescendents();
    }
  }
}

void LayerGraphicsLoader::onDetached() {
  completeTasks.clear();
}

}  // namespace tgfx

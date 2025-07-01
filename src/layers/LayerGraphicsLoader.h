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

#pragma once

#include <list>
#include <unordered_map>
#include "core/DeferredGraphics.h"
#include "gpu/GraphicsLoader.h"
#include "tgfx/core/Task.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {
class GraphicLoadTask;

/**
 * LayerGraphicsLoader is a GraphicsLoader that loads deferred graphics for layers.
 */
class LayerGraphicsLoader : public GraphicsLoader {
 public:
  explicit LayerGraphicsLoader(Layer* rootLayer);

  ~LayerGraphicsLoader() override;

  size_t maxAsyncGraphicsPerFrame() const {
    return _maxAsyncGraphicsPerFrame;
  }

  void setMaxAsyncGraphicsPerFrame(size_t count) {
    _maxAsyncGraphicsPerFrame = count;
  }

  void updateCompleteTasks();

  void updateLayerContent(Layer* layer, LayerContent* content);

  std::shared_ptr<ImageBuffer> loadImage(std::shared_ptr<ImageGenerator> generator,
                                         bool tryHardware) override;

  Path loadShape(std::shared_ptr<Shape> shape) override;

 private:
  size_t _maxAsyncGraphicsPerFrame = 0;
  std::unordered_map<Layer*, std::vector<const void*>> layerToGraphics = {};
  std::unordered_map<const void*, std::list<Layer*>> graphicToLayers = {};
  std::unordered_map<const void*, std::shared_ptr<GraphicLoadTask>> pendingTasks = {};
  std::unordered_map<const void*, std::shared_ptr<GraphicLoadTask>> completeTasks = {};

  void addLayerContents(Layer* layer);

  friend class RootLayer;
};
}  // namespace tgfx

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

#include <deque>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include "gpu/GraphicsLoader.h"
#include "tgfx/core/Task.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {
class GraphicLoadTask;

/**
 * AsyncGraphic is an interface for graphics that can be loaded asynchronously.
 */
class DeferredGraphic {
 public:
  virtual ~DeferredGraphic() = default;

  virtual void load() = 0;

  virtual const void* getSource() const = 0;

  virtual std::shared_ptr<ImageBuffer> getBuffer() const {
    return nullptr;
  }

  virtual Path getPath() const {
    return {};
  }
};

/**
 * LayerGraphicsLoader is a GraphicsLoader that loads deferred graphics for layers.
 */
class LayerGraphicsLoader : public GraphicsLoader {
 public:
  ~LayerGraphicsLoader() override;

  size_t maxAsyncGraphicsPerFrame() const {
    return _maxAsyncGraphicsPerFrame;
  }

  void setMaxAsyncGraphicsPerFrame(size_t count) {
    _maxAsyncGraphicsPerFrame = count;
  }

  /**
   * Adds LayerContent to the loader if it has deferred graphics. Returns true if added, or false if
   * it lacks deferred graphics or is nullptr.nullptr.
   */
  bool addAsyncContent(Layer* layer, LayerContent* content);

  /**
   * Cancels the asynchronous content loading for the specified layer.
   */
  void cancelAsyncContent(Layer* layer);

  std::shared_ptr<ImageBuffer> loadImage(std::shared_ptr<ImageGenerator> generator,
                                         bool tryHardware) override;

  bool loadShape(std::shared_ptr<Shape> shape, Path* path) override;

 protected:
  void onAttached(Context* current) override;

  void onDetached() override;

 private:
  Context* context = nullptr;
  Layer* currentLayer = nullptr;
  size_t _maxAsyncGraphicsPerFrame = 0;
  std::deque<Layer*> pendingLayers = {};
  std::unordered_map<Layer*, std::list<const void*>> layerToGraphics = {};
  std::unordered_map<const void*, std::list<Layer*>> graphicToLayers = {};
  std::unordered_map<const void*, std::unique_ptr<DeferredGraphic>> pendingGraphics = {};
  std::unordered_map<const void*, std::shared_ptr<GraphicLoadTask>> loadingTasks = {};
  std::unordered_set<Layer*> completeLayers = {};
  std::unordered_map<const void*, std::shared_ptr<DeferredGraphic>> completeGraphics = {};

  void addDeferredGraphic(std::unique_ptr<DeferredGraphic> graphic);

  friend class RootLayer;
};
}  // namespace tgfx

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include <unordered_map>
#include <vector>
#include "tgfx/layers/Layer.h"

namespace tgfx::debug {

class LayerViewerManager final {
 public:
  static LayerViewerManager& Get() {
    static LayerViewerManager instance;
    return instance;
  }

  void pickLayer(std::shared_ptr<Layer> layer);

  void setDisplayList(DisplayList* displayList);

  void serializingLayerTree();

  void serializingLayerAttribute(const std::shared_ptr<Layer>& layer);

  void feedBackDataProcess(const std::vector<uint8_t>& data);

  void renderImageAndSend(Context* context);

  LayerViewerManager(const LayerViewerManager&) = delete;

  LayerViewerManager(LayerViewerManager&&) = delete;

  LayerViewerManager& operator=(const LayerViewerManager&) = delete;

  LayerViewerManager& operator=(LayerViewerManager&&) = delete;

 private:
  void setCallBack();

  void addHighLightOverlay(Color color, std::shared_ptr<Layer> hovedLayer);

  void sendPickedLayerAddress(const std::shared_ptr<Layer>& layer);

  void sendFlushAttributeAck(uint64_t address);

  LayerViewerManager();

 private:
  std::unordered_map<uint64_t, std::shared_ptr<Layer>> layerMap = {};
  std::unordered_map<uint64_t, std::unordered_map<uint64_t, std::function<std::shared_ptr<Data>()>>>
      layerComplexObjMap = {};
  std::unordered_map<uint64_t,
                     std::unordered_map<uint64_t, std::function<std::shared_ptr<Data>(Context*)>>>
      layerRenderableObjMap = {};
  uint64_t hoveredAddress = 0;
  uint64_t selectedAddress = 0;
  uint64_t expandID = 0;
  std::shared_ptr<Layer> hoverdLayer = nullptr;
  int highLightLayerIndex = 0;
  bool hoverdSwitch = false;
  DisplayList* displayList = nullptr;
};

}  // namespace tgfx::debug

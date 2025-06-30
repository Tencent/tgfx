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
#include <unordered_map>
#include <vector>
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {

class LayerInspectorManager {
 public:
  static LayerInspectorManager& GetLayerInspectorManager() {
    static LayerInspectorManager instance;
    return instance;
  }

  void pickedLayer(float x, float y);
  void setLayerInspectorHoveredStateCallBack(std::function<void(bool)> callback);

  void setDisplayList(tgfx::DisplayList* displayList);
  void serializingLayerTree();
  void serializingLayerAttribute(const std::shared_ptr<tgfx::Layer>& layer);
  void FeedBackDataProcess(const std::vector<uint8_t>& data);
  void setCallBack();
  void RenderImageAndSend(Context* context);

 private:
  void AddHighLightOverlay(Color color, std::shared_ptr<Layer> hovedLayer);
  void SendPickedLayerAddress(const std::shared_ptr<tgfx::Layer>& layer);
  void SendFlushAttributeAck(uint64_t address);
  LayerInspectorManager();

 private:
  std::unordered_map<uint64_t, std::shared_ptr<tgfx::Layer>> layerMap;
  std::unordered_map<uint64_t, std::unordered_map<uint64_t, std::function<std::shared_ptr<Data>()>>>
      layerComplexObjMap;
  std::unordered_map<uint64_t,
                     std::unordered_map<uint64_t, std::function<std::shared_ptr<Data>(Context*)>>>
      layerRenderableObjMap;
  uint64_t hoveredAddress;
  uint64_t selectedAddress;
  uint64_t expandID;
  std::shared_ptr<tgfx::Layer> hoverdLayer;
  int highLightLayerIndex = 0;
  bool hoverdSwitch = false;
  tgfx::DisplayList* displayList;
  std::function<void(bool)> hoveredCallBack;
};

}  // namespace tgfx

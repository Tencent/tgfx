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
#ifdef TGFX_USE_INSPECTOR
#include "LayerInspectorManager.h"
#include <functional>
#include <string>
#include "LockFreeQueue.h"
#include "core/utils/Profiling.h"
#include "serialization/LayerSerialization.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidColor.h"

namespace tgfx {
extern const std::string HighLightLayerName = "HighLightLayer";
static inspector::LockFreeQueue<uint64_t> imageIDQueue;
void LayerInspectorManager::pickedLayer(float x, float y) {
  if (hoverdSwitch) {
    auto layers = displayList->root()->getLayersUnderPoint(x, y);
    for (auto layer : layers) {
      if (layer->name() != HighLightLayerName) {
        if (reinterpret_cast<uint64_t>(layer.get()) != selectedAddress) {
          SendPickedLayerAddress(layer);
        }
        AddHighLightOverlay(tgfx::Color::FromRGBA(111, 166, 219), layer);
        break;
      }
    }
  }
}

void LayerInspectorManager::setLayerInspectorHoveredStateCallBack(std::function<void(bool)> callback) {
  hoveredCallBack = callback;
}

void LayerInspectorManager::setCallBack() {
  [[maybe_unused]] std::function<void(const std::vector<uint8_t>&)> func =
      std::bind(&LayerInspectorManager::FeedBackDataProcess, this, std::placeholders::_1);
  LAYER_CALLBACK(func);
}

void LayerInspectorManager::RenderImageAndSend(Context* context) {
  if (!imageIDQueue.empty()) {
    auto id = imageIDQueue.pop();
    std::shared_ptr<Data> data = layerRenderableObjMap[selectedAddress][*id](context);
    if (!data->empty()) {
      std::vector<uint8_t> blob(data->bytes(), data->bytes() + data->size());
      LAYER_DATA(blob);
    }
  }
}

LayerInspectorManager::LayerInspectorManager() {
  hoverdLayer = nullptr;
}

void LayerInspectorManager::setDisplayList(tgfx::DisplayList* displayList) {
  this->displayList = displayList;
}

void LayerInspectorManager::serializingLayerTree() {
  layerMap.clear();

  std::shared_ptr<Data> data = tgfx::LayerSerialization::SerializeTreeNode(
      displayList->root()->shared_from_this(), layerMap);
  std::vector<uint8_t> blob(data->bytes(), data->bytes() + data->size());

  LAYER_DATA(blob);
}

void LayerInspectorManager::SendPickedLayerAddress(const std::shared_ptr<tgfx::Layer>& layer) {
  flexbuffers::Builder fbb;
  auto startMap = fbb.StartMap();
  fbb.String("Type", "PickedLayerAddress");
  fbb.Key("Content");
  auto contentMap = fbb.StartMap();
  fbb.UInt("Address", reinterpret_cast<uint64_t>(layer.get()));
  fbb.EndMap(contentMap);
  fbb.EndMap(startMap);
  fbb.Finish();
  LAYER_DATA(fbb.GetBuffer());
}

void LayerInspectorManager::SendFlushAttributeAck(uint64_t address) {
  flexbuffers::Builder fbb;
  auto startMap = fbb.StartMap();
  fbb.String("Type", "FlushAttributeAck");
  fbb.Key("Content");
  auto contentMap = fbb.StartMap();
  fbb.UInt("Address", address);
  fbb.EndMap(contentMap);
  fbb.EndMap(startMap);
  fbb.Finish();
  LAYER_DATA(fbb.GetBuffer());
}

void LayerInspectorManager::serializingLayerAttribute(const std::shared_ptr<tgfx::Layer>& layer) {
  if (!layer) return;
  auto& complexObjSerMap = layerComplexObjMap[reinterpret_cast<uint64_t>(layer.get())];
  auto& renderableObjSerMap = layerRenderableObjMap[reinterpret_cast<uint64_t>(layer.get())];
  auto data = LayerSerialization::SerializeLayer(layer.get(), &complexObjSerMap,
                                                 &renderableObjSerMap, "LayerAttribute");
  std::vector<uint8_t> blob(data->bytes(), data->bytes() + data->size());
  LAYER_DATA(blob);
}

void LayerInspectorManager::FeedBackDataProcess(const std::vector<uint8_t>& data) {
  if (data.size() == 0) {
    return;
  }
  auto map = flexbuffers::GetRoot(data.data(), data.size()).AsMap();
  auto type = map["Type"].AsString().str();
  if (type == "EnalbeLayerInspect") {
    hoverdSwitch = map["Value"].AsUInt64();
    if (!hoverdSwitch && hoverdLayer) {
      hoverdLayer->removeChildren(highLightLayerIndex);
      hoverdLayer = nullptr;
    }
    if (hoveredCallBack) {
      hoveredCallBack(hoverdSwitch);
    }
  } else if (type == "HoverLayerAddress") {
    if (hoverdSwitch) {
      hoveredAddress = map["Value"].AsUInt64();
      AddHighLightOverlay(tgfx::Color::FromRGBA(111, 166, 219), layerMap[hoveredAddress]);
    }
  } else if (type == "SelectedLayerAddress") {
    selectedAddress = map["Value"].AsUInt64();
  } else if (type == "SerializeAttribute") {
    serializingLayerAttribute(layerMap[selectedAddress]);
  } else if (type == "SerializeSubAttribute") {
    expandID = map["Value"].AsUInt64();
    std::shared_ptr<Data> data = layerComplexObjMap[selectedAddress][expandID]();
    std::vector<uint8_t> blob(data->bytes(), data->bytes() + data->size());
    LAYER_DATA(blob);
  } else if (type == "FlushAttribute") {
    uint64_t address = map["Value"].AsUInt64();
    if (layerComplexObjMap.find(address) != layerComplexObjMap.end()) {
      layerComplexObjMap.erase(address);
    }
    if (layerRenderableObjMap.find(address) != layerRenderableObjMap.end()) {
      layerRenderableObjMap.erase(address);
    }
    SendFlushAttributeAck(address);
  } else if (type == "FlushLayerTree") {
    serializingLayerTree();
  } else if (type == "FlushImage") {
    uint64_t imageId = map["Value"].AsUInt64();
    imageIDQueue.push(imageId);
  }
}

void LayerInspectorManager::AddHighLightOverlay(Color color, std::shared_ptr<Layer> hovedLayer) {
  if (!hovedLayer) {
    return;
  }

  if (hovedLayer == hoverdLayer) {
    return;
  }

  if (hoverdLayer) {
    hoverdLayer->removeChildren(highLightLayerIndex);
  }

  hoverdLayer = hovedLayer;
  auto highlightLayer = tgfx::ShapeLayer::Make();
  highlightLayer->setName(HighLightLayerName);
  highlightLayer->setBlendMode(tgfx::BlendMode::SrcOver);
  auto rectPath = tgfx::Path();
  rectPath.addRect(hoverdLayer->getBounds());
  highlightLayer->setFillStyle(tgfx::SolidColor::Make(color));
  highlightLayer->setPath(rectPath);
  highlightLayer->setAlpha(0.66f);
  hoverdLayer->addChild(highlightLayer);
  highLightLayerIndex = hoverdLayer->getChildIndex(highlightLayer);
}
}  // namespace tgfx
#endif

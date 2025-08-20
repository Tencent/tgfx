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
#include "LayerViewerManager.h"
#include <functional>
#include <string>
#include "LayerInspectorProtocol.h"
#include "LayerProfiler.h"
#include "concurrentqueue.h"
#include "debug/Mark.h"
#include "serialization/LayerSerialization.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidColor.h"

namespace tgfx::debug {
static moodycamel::ConcurrentQueue<uint64_t> imageIDQueue;
void LayerViewerManager::pickLayer(std::shared_ptr<Layer> layer) {
  if (layer->name() != HighLightLayerName) {
    if (reinterpret_cast<uint64_t>(layer.get()) != selectedAddress) {
      sendPickedLayerAddress(layer);
    }
    if (hoverdSwitch) {
      addHighLightOverlay(Color::FromRGBA(111, 166, 219), std::move(layer));
    }
  }
}

void LayerViewerManager::setCallBack() {
  std::function<void(const std::vector<uint8_t>&)> func = [this](const std::vector<uint8_t>& data) {
    this->feedBackDataProcess(data);
  };
  LAYER_CALLBACK(func);
}

void LayerViewerManager::renderImageAndSend(Context* context) {
  if (imageIDQueue.size_approx() != 0) {
    uint64_t id;
    if (imageIDQueue.try_dequeue(id)) {
      if (layerRenderableObjMap.find(selectedAddress) != layerRenderableObjMap.end() &&
          layerRenderableObjMap[selectedAddress].find(id) !=
              layerRenderableObjMap[selectedAddress].end()) {
        std::shared_ptr<Data> data = layerRenderableObjMap[selectedAddress][id](context);
        if (!data->empty()) {
          std::vector<uint8_t> blob(data->bytes(), data->bytes() + data->size());
          SEND_LAYER_DATA(blob);
        }
      }
    }
  }
}

LayerViewerManager::LayerViewerManager() {
  setCallBack();
}

void LayerViewerManager::setDisplayList(DisplayList* displayList) {
  this->displayList = displayList;
}

void LayerViewerManager::serializingLayerTree() {
  layerMap.clear();

  std::shared_ptr<Data> data =
      LayerSerialization::SerializeTreeNode(displayList->root()->shared_from_this(), layerMap);
  std::vector<uint8_t> blob(data->bytes(), data->bytes() + data->size());

  SEND_LAYER_DATA(blob);
}

void LayerViewerManager::sendPickedLayerAddress(const std::shared_ptr<Layer>& layer) {
  flexbuffers::Builder fbb;
  auto startMap = fbb.StartMap();
  fbb.UInt("Type", static_cast<uint8_t>(LayerInspectorMsgType::PickedLayerAddress));
  fbb.Key("Content");
  auto contentMap = fbb.StartMap();
  fbb.UInt("Address", reinterpret_cast<uint64_t>(layer.get()));
  fbb.EndMap(contentMap);
  fbb.EndMap(startMap);
  fbb.Finish();
  SEND_LAYER_DATA(fbb.GetBuffer());
}

void LayerViewerManager::sendFlushAttributeAck(uint64_t address) {
  flexbuffers::Builder fbb;
  auto startMap = fbb.StartMap();
  fbb.UInt("Type", static_cast<uint8_t>(LayerInspectorMsgType::FlushAttributeAck));
  fbb.Key("Content");
  auto contentMap = fbb.StartMap();
  fbb.UInt("Address", address);
  fbb.EndMap(contentMap);
  fbb.EndMap(startMap);
  fbb.Finish();
  SEND_LAYER_DATA(fbb.GetBuffer());
}

void LayerViewerManager::serializingLayerAttribute(const std::shared_ptr<Layer>& layer) {
  if (!layer) {
    return;
  }
  auto& complexObjSerMap = layerComplexObjMap[reinterpret_cast<uint64_t>(layer.get())];
  auto& renderableObjSerMap = layerRenderableObjMap[reinterpret_cast<uint64_t>(layer.get())];
  auto data = LayerSerialization::SerializeLayer(
      layer.get(), &complexObjSerMap, &renderableObjSerMap, LayerInspectorMsgType::LayerAttribute);
  std::vector<uint8_t> blob(data->bytes(), data->bytes() + data->size());
  SEND_LAYER_DATA(blob);
}

void LayerViewerManager::feedBackDataProcess(const std::vector<uint8_t>& data) {
  if (data.size() == 0) {
    return;
  }
  auto map = flexbuffers::GetRoot(data.data(), data.size()).AsMap();
  auto type = static_cast<LayerInspectorMsgType>(map["Type"].AsUInt8());
  switch (type) {
    case LayerInspectorMsgType::EnableLayerInspector: {
      hoverdSwitch = map["Value"].AsUInt64();
      if (!hoverdSwitch && hoverdLayer) {
        hoverdLayer->removeChildren(highLightLayerIndex);
        hoverdLayer = nullptr;
      }
      break;
    }
    case LayerInspectorMsgType::HoverLayerAddress: {
      if (hoverdSwitch) {
        hoveredAddress = map["Value"].AsUInt64();
        addHighLightOverlay(Color::FromRGBA(111, 166, 219), layerMap[hoveredAddress]);
      }
      break;
    }
    case LayerInspectorMsgType::SelectedLayerAddress: {
      selectedAddress = map["Value"].AsUInt64();
      break;
    }
    case LayerInspectorMsgType::SerializeAttribute: {
      serializingLayerAttribute(layerMap[selectedAddress]);
      break;
    }
    case LayerInspectorMsgType::SerializeSubAttribute: {
      expandID = map["Value"].AsUInt64();
      if (layerComplexObjMap.find(selectedAddress) != layerComplexObjMap.end() &&
          layerComplexObjMap[selectedAddress].find(expandID) !=
              layerComplexObjMap[selectedAddress].end()) {
        std::shared_ptr<Data> data = layerComplexObjMap[selectedAddress][expandID]();
        std::vector<uint8_t> blob(data->bytes(), data->bytes() + data->size());
        SEND_LAYER_DATA(blob);
      }
      break;
    }
    case LayerInspectorMsgType::FlushAttribute: {
      uint64_t address = map["Value"].AsUInt64();
      if (layerComplexObjMap.find(address) != layerComplexObjMap.end()) {
        layerComplexObjMap.erase(address);
      }
      if (layerRenderableObjMap.find(address) != layerRenderableObjMap.end()) {
        layerRenderableObjMap.erase(address);
      }
      sendFlushAttributeAck(address);
      break;
    }
    case LayerInspectorMsgType::FlushLayerTree: {
      serializingLayerTree();
      break;
    }
    case LayerInspectorMsgType::FlushImage: {
      uint64_t imageId = map["Value"].AsUInt64();
      imageIDQueue.enqueue(imageId);
      break;
    }
    default: {
      DEBUG_ASSERT(false);
    }
  }
}

void LayerViewerManager::addHighLightOverlay(Color color, std::shared_ptr<Layer> hovedLayer) {
  if (!hovedLayer) {
    return;
  }

  if (hovedLayer == this->hoverdLayer) {
    return;
  }

  if (hoverdLayer) {
    hoverdLayer->removeChildren(highLightLayerIndex);
  }

  hoverdLayer = std::move(hovedLayer);
  auto highlightLayer = ShapeLayer::Make();
  highlightLayer->setName(HighLightLayerName);
  highlightLayer->setBlendMode(BlendMode::SrcOver);
  auto rectPath = Path();
  rectPath.addRect(hoverdLayer->getBounds());
  highlightLayer->setFillStyle(SolidColor::Make(color));
  highlightLayer->setPath(rectPath);
  highlightLayer->setAlpha(0.66f);
  hoverdLayer->addChild(highlightLayer);
  highLightLayerIndex = hoverdLayer->getChildIndex(highlightLayer);
}
}  // namespace tgfx::debug

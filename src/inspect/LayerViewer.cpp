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
#include "LayerViewer.h"
#include <functional>
#include <string>
#include "LayerTree.h"
#include "Protocol.h"
#include "concurrentqueue.h"
#include "inspect/InspectorMark.h"
#include "serialization/LayerSerialization.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidColor.h"

namespace tgfx::inspect {
static moodycamel::ConcurrentQueue<uint64_t> imageIDQueue;
void LayerViewer::setSelectLayer(std::shared_ptr<Layer> layer) {
  if (layer->name() != HighLightLayerName) {
    if (reinterpret_cast<uint64_t>(layer.get()) != selectedAddress) {
      sendPickedLayerAddress(layer);
    }
    if (hoverdSwitch) {
      addHighLightOverlay(Color::FromRGBA(111, 166, 219), std::move(layer));
    }
  }
}

void LayerViewer::setCallBack() {
  std::function<void(const std::vector<uint8_t>&)> func = [this](const std::vector<uint8_t>& data) {
    feedBackDataProcess(data);
  };
  LAYER_CALLBACK(func);
}

void LayerViewer::renderImageAndSend(Context* context) {
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

LayerViewer::LayerViewer() {
  setCallBack();
}

void LayerViewer::setDisplayList(DisplayList* list) {
  displayList = list;
}

void LayerViewer::serializingLayerTree() {
  layerMap.clear();

  std::shared_ptr<Data> data =
      LayerSerialization::SerializeTreeNode(displayList->root()->shared_from_this(), layerMap);
  std::vector<uint8_t> blob(data->bytes(), data->bytes() + data->size());

  SEND_LAYER_DATA(blob);
}

void LayerViewer::sendPickedLayerAddress(const std::shared_ptr<Layer>& layer) {
  flexbuffers::Builder fbb;
  auto startMap = fbb.StartMap();
  fbb.UInt("Type", static_cast<uint8_t>(LayerViewerMessage::PickedLayerAddress));
  fbb.Key("Content");
  auto contentMap = fbb.StartMap();
  fbb.UInt("Address", reinterpret_cast<uint64_t>(layer.get()));
  fbb.EndMap(contentMap);
  fbb.EndMap(startMap);
  fbb.Finish();
  SEND_LAYER_DATA(fbb.GetBuffer());
}

void LayerViewer::sendFlushAttributeAck(uint64_t address) {
  flexbuffers::Builder fbb;
  auto startMap = fbb.StartMap();
  fbb.UInt("Type", static_cast<uint8_t>(LayerViewerMessage::FlushAttributeAck));
  fbb.Key("Content");
  auto contentMap = fbb.StartMap();
  fbb.UInt("Address", address);
  fbb.EndMap(contentMap);
  fbb.EndMap(startMap);
  fbb.Finish();
  SEND_LAYER_DATA(fbb.GetBuffer());
}

void LayerViewer::serializingLayerAttribute(const std::shared_ptr<Layer>& layer) {
  if (!layer) {
    return;
  }
  auto& complexObjSerMap = layerComplexObjMap[reinterpret_cast<uint64_t>(layer.get())];
  auto& renderableObjSerMap = layerRenderableObjMap[reinterpret_cast<uint64_t>(layer.get())];
  auto data = LayerSerialization::SerializeLayer(
      layer.get(), &complexObjSerMap, &renderableObjSerMap, LayerViewerMessage::LayerAttribute);
  std::vector<uint8_t> blob(data->bytes(), data->bytes() + data->size());
  SEND_LAYER_DATA(blob);
}

void LayerViewer::feedBackDataProcess(const std::vector<uint8_t>& data) {
  if (data.size() == 0) {
    return;
  }
  auto map = flexbuffers::GetRoot(data.data(), data.size()).AsMap();
  auto type = static_cast<LayerViewerMessage>(map["Type"].AsUInt8());
  switch (type) {
    case LayerViewerMessage::EnableLayerInspector: {
      hoverdSwitch = map["Value"].AsUInt64();
      if (!hoverdSwitch && hoverdLayer) {
        hoverdLayer->removeChildren(highLightLayerIndex);
        hoverdLayer = nullptr;
      }
      break;
    }
    case LayerViewerMessage::HoverLayerAddress: {
      if (hoverdSwitch) {
        hoveredAddress = map["Value"].AsUInt64();
        addHighLightOverlay(Color::FromRGBA(111, 166, 219), layerMap[hoveredAddress]);
      }
      break;
    }
    case LayerViewerMessage::SelectedLayerAddress: {
      selectedAddress = map["Value"].AsUInt64();
      break;
    }
    case LayerViewerMessage::SerializeAttribute: {
      serializingLayerAttribute(layerMap[selectedAddress]);
      break;
    }
    case LayerViewerMessage::SerializeSubAttribute: {
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
    case LayerViewerMessage::FlushAttribute: {
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
    case LayerViewerMessage::FlushLayerTree: {
      serializingLayerTree();
      break;
    }
    case LayerViewerMessage::FlushImage: {
      uint64_t imageId = map["Value"].AsUInt64();
      imageIDQueue.enqueue(imageId);
      break;
    }
    default: {
      DEBUG_ASSERT(false);
    }
  }
}

void LayerViewer::addHighLightOverlay(Color color, std::shared_ptr<Layer> layer) {
  if (!layer) {
    return;
  }

  if (layer == hoverdLayer) {
    return;
  }

  if (hoverdLayer) {
    hoverdLayer->removeChildren(highLightLayerIndex);
  }

  hoverdLayer = std::move(layer);
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
}  // namespace tgfx::inspect

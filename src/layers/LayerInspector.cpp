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
#include "tgfx/layers/LayerInspector.h"
#include <string>
#include <chrono>
#include <functional>
#include "core/utils/Profiling.h"
#include "serialization/LayerSerialization.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidColor.h"
#include "LockFreeQueue.h"

namespace tgfx {
  extern const std::string HighLightLayerName = "HighLightLayer";
  static inspector::LockFreeQueue<uint64_t> imageIDQueue;
  void LayerInspector::pickedLayer(float x, float y) {
    if (m_HoverdSwitch) {
      auto layers = m_DisplayList->root()->getLayersUnderPoint(x, y);
      for (auto layer : layers) {
        if (layer->name() != HighLightLayerName) {
          if (reinterpret_cast<uint64_t>(layer.get()) != m_SelectedAddress) {
            SendPickedLayerAddress(layer);
          }
          AddHighLightOverlay(tgfx::Color::FromRGBA(111, 166, 219), layer);
          break;
        }
      }
    }
  }

void LayerInspector::setLayerInspectorHoveredStateCallBack(std::function<void(bool)> callback) {
    hoveredCallBack = callback;
  }

void LayerInspector::setCallBack() {
  [[maybe_unused]] std::function<void(const std::vector<uint8_t>&)> func =
      std::bind(&LayerInspector::FeedBackDataProcess, this, std::placeholders::_1);
  LAYER_CALLBACK(func);
}

void LayerInspector::RenderImageAndSend(Context *context) {
  if(!imageIDQueue.empty()) {
    auto id = imageIDQueue.pop();
    std::shared_ptr<Data> data = m_LayerRenderableObjMap[m_SelectedAddress][*id](context);
    if(!data->empty()) {
      std::vector<uint8_t> blob(data->bytes(), data->bytes() + data->size());
      LAYER_DATA(blob);
    }
  }
}

LayerInspector::LayerInspector() {
  m_HoverdLayer = nullptr;
}

void LayerInspector::setDisplayList(tgfx::DisplayList* displayList) {
  m_DisplayList = displayList;
}

void LayerInspector::serializingLayerTree() {
  m_LayerMap.clear();

  std::shared_ptr<Data> data = tgfx::LayerSerialization::SerializeTreeNode(
      m_DisplayList->root()->shared_from_this(), m_LayerMap);
  std::vector<uint8_t> blob(data->bytes(), data->bytes() + data->size());

  LAYER_DATA(blob);
}

void LayerInspector::SendPickedLayerAddress(const std::shared_ptr<tgfx::Layer>& layer) {
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

void LayerInspector::SendFlushAttributeAck(uint64_t address) {
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

void LayerInspector::serializingLayerAttribute(const std::shared_ptr<tgfx::Layer>& layer) {
  if (!layer) return;
  auto& complexObjSerMap = m_LayerComplexObjMap[reinterpret_cast<uint64_t>(layer.get())];
  auto& renderableObjSerMap = m_LayerRenderableObjMap[reinterpret_cast<uint64_t>(layer.get())];
  auto data = LayerSerialization::SerializeLayer(layer.get(), &complexObjSerMap, &renderableObjSerMap, "LayerAttribute");
  std::vector<uint8_t> blob(data->bytes(), data->bytes() + data->size());
  LAYER_DATA(blob);
}

void LayerInspector::FeedBackDataProcess(const std::vector<uint8_t>& data) {
  if (data.size() == 0) {
    return;
  }
  auto map = flexbuffers::GetRoot(data.data(), data.size()).AsMap();
  auto type = map["Type"].AsString().str();
  if (type == "EnalbeLayerInspect") {
    m_HoverdSwitch = map["Value"].AsUInt64();
    if (!m_HoverdSwitch && m_HoverdLayer) {
      m_HoverdLayer->removeChildren(m_HighLightLayerIndex);
      m_HoverdLayer = nullptr;
    }
    if(hoveredCallBack) {
      hoveredCallBack(m_HoverdSwitch);
    }
  } else if (type == "HoverLayerAddress") {
    if (m_HoverdSwitch) {
      m_HoveredAddress = map["Value"].AsUInt64();
      AddHighLightOverlay(tgfx::Color::FromRGBA(111, 166, 219), m_LayerMap[m_HoveredAddress]);
    }
  } else if (type == "SelectedLayerAddress") {
    m_SelectedAddress = map["Value"].AsUInt64();
  } else if (type == "SerializeAttribute") {
    serializingLayerAttribute(m_LayerMap[m_SelectedAddress]);
  } else if (type == "SerializeSubAttribute") {
    m_ExpandID = map["Value"].AsUInt64();
    std::shared_ptr<Data> data = m_LayerComplexObjMap[m_SelectedAddress][m_ExpandID]();
    std::vector<uint8_t> blob(data->bytes(), data->bytes() + data->size());
    LAYER_DATA(blob);
  } else if (type == "FlushAttribute") {
    uint64_t address = map["Value"].AsUInt64();
    if (m_LayerComplexObjMap.find(address) != m_LayerComplexObjMap.end()) {
      m_LayerComplexObjMap.erase(address);
    }
    if(m_LayerRenderableObjMap.find(address)!=m_LayerRenderableObjMap.end()) {
      m_LayerRenderableObjMap.erase(address);
    }
    SendFlushAttributeAck(address);
  } else if (type == "FlushLayerTree") {
    serializingLayerTree();
  }else if(type == "FlushImage") {
    uint64_t imageId = map["Value"].AsUInt64();
    imageIDQueue.push(imageId);
  }
}

void LayerInspector::AddHighLightOverlay(Color color, std::shared_ptr<Layer> hovedLayer) {
  if (!hovedLayer) {
    return;
  }

  if (hovedLayer == m_HoverdLayer) {
    return;
  }

  if (m_HoverdLayer) {
    m_HoverdLayer->removeChildren(m_HighLightLayerIndex);
  }

  m_HoverdLayer = hovedLayer;
  auto highlightLayer = tgfx::ShapeLayer::Make();
  highlightLayer->setName(HighLightLayerName);
  highlightLayer->setBlendMode(tgfx::BlendMode::SrcOver);
  auto rectPath = tgfx::Path();
  rectPath.addRect(m_HoverdLayer->getBounds());
  highlightLayer->setFillStyle(tgfx::SolidColor::Make(color));
  highlightLayer->setPath(rectPath);
  highlightLayer->setAlpha(0.66f);
  m_HoverdLayer->addChild(highlightLayer);
  m_HighLightLayerIndex = m_HoverdLayer->getChildIndex(highlightLayer);
}
}  // namespace tgfx
#endif

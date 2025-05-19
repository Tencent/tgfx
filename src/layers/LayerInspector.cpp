/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <functional>
#include <chrono>
#include "tgfx/layers/LayerInspector.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidColor.h"
#include "serialization/LayerSerialization.h"
#include "core/utils/Profiling.h"

#pragma pack(push,1)
enum Type : uint8_t {
  None = 0,
  EnalbeLayerInspect,
  HoverLayerAddress,
  SelectedLayerAddress
};

struct FeedbackData {
  Type type;
  uint64_t address;
};
#pragma pack(pop)


namespace tgfx {
void LayerInspector::setCallBack() {
  [[maybe_unused]] std::function<void(const std::vector<uint8_t>&)> func =
      std::bind(&LayerInspector::FeedBackDataProcess, this, std::placeholders::_1);
  LAYER_CALLBACK(func);
}

void LayerInspector::pickedLayer(float x, float y) {
  if(m_HoverdSwitch) {
    auto layers = m_DisplayList->root()->getLayersUnderPoint(x, y);
    for(auto layer : layers) {
      if(layer->name() != "HighLightLayer") {
        AddHighLightOverlay(tgfx::Color::FromRGBA(111, 166, 219), layer);
        serializingLayerAttribute(layer);
        break;
      }
    }
  }
}

LayerInspector::LayerInspector() {
  m_HoverdLayer = nullptr;
}

void LayerInspector::setDisplayList(tgfx::DisplayList* displayList) {
  m_DisplayList = displayList;
}

void LayerInspector::setDirty(Layer* root, std::shared_ptr<Layer> child) {
  if(root == m_DisplayList->root() && child->name() != "HighLightLayer") {
    m_IsDirty = true;
  }
}

void LayerInspector::serializingLayerTree() {
    if(m_IsDirty) {
      m_LayerMap.clear();

      std::shared_ptr<Data> data = tgfx::LayerSerialization::SerializeTreeNode(m_DisplayList->_root, m_LayerMap);
      std::vector<uint8_t> blob(data->bytes(), data->bytes() + data->size());

      LAYER_DATA(blob);

      m_IsDirty = false;
    }
}

  void LayerInspector::serializingLayerAttribute(const std::shared_ptr<tgfx::Layer>& layer) {
    if(!layer) return;
    auto pathObjMap = m_LayerComplexObjMap[reinterpret_cast<uint64_t>(layer.get())];
    auto data = LayerSerialization::SerializeLayer(layer.get(), &pathObjMap);
    std::vector<uint8_t> blob(data->bytes(), data->bytes() + data->size());
    LAYER_DATA(blob);
  }

  void LayerInspector::FeedBackDataProcess(const std::vector<uint8_t>& data) {
    FeedbackData* feedbackData = (FeedbackData*)data.data();
    switch(feedbackData->type) {
      case None: {
        printf("Unknown feedback type! \n");
        break;
      }
      case EnalbeLayerInspect: {
        printf("Enable Layer inspect! \n");
        m_HoverdSwitch = feedbackData->address;
        if(!m_HoverdSwitch && m_HoverdLayer) {
          m_HoverdLayer->removeChildren(m_HighLightLayerIndex);
          m_HoverdLayer = nullptr;
        }
        break;
      }
      case HoverLayerAddress: {
        if(m_HoverdSwitch) {
          printf("Hover Layer Address: \n");
          printf("Address: %llu \n", feedbackData->address);
          m_HoveredAddress = feedbackData->address;
          AddHighLightOverlay(tgfx::Color::FromRGBA(111, 166, 219), m_LayerMap[m_HoveredAddress]);
        }
        break;
      }

      case SelectedLayerAddress: {
        printf("Clicked Layer Address: \n");
        printf("Address: %llu \n", feedbackData->address);
        m_SelectedAddress = feedbackData->address;
        serializingLayerAttribute(m_LayerMap[m_SelectedAddress]);
        break;
      }
    }
  }

  void LayerInspector::AddHighLightOverlay(Color color, std::shared_ptr<Layer> hovedLayer) {
    if(!hovedLayer) {
      return;
    }

    if(hovedLayer == m_HoverdLayer) {
      return;
    }

    if(m_HoverdLayer) {
      m_HoverdLayer->removeChildren(m_HighLightLayerIndex);
    }

    m_HoverdLayer = hovedLayer;
    auto highlightLayer = tgfx::ShapeLayer::Make();
    highlightLayer->setName("HighLightLayer");
    highlightLayer->setBlendMode(tgfx::BlendMode::SrcOver);
    auto rectPath = tgfx::Path();
    rectPath.addRect(m_HoverdLayer->getBounds());
    highlightLayer->setFillStyle(tgfx::SolidColor::Make(color));
    highlightLayer->setPath(rectPath);
    highlightLayer->setAlpha(0.66f);
    m_HoverdLayer->addChild(highlightLayer);
    m_HighLightLayerIndex = m_HoverdLayer->getChildIndex(highlightLayer);
  }
}


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
#include "tgfx/layers/LayerInspector.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidColor.h"
#include "serialization/LayerSerialization.h"
#include "layers/generate/SerializationStructure_generated.h"
#include "core/utils/Profiling.h"

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


namespace tgfx {
LayerInspector::LayerInspector() {
  [[maybe_unused]] std::function<void(const std::vector<uint8_t>&)> func =
    std::bind(&LayerInspector::FeedBackDataProcess, this, std::placeholders::_1);
  LAYER_CALLBACK(func);
  m_HoverdLayer = nullptr;
}

  void LayerInspector::serializingDisplayLists(const std::vector<tgfx::DisplayList>& displayLists) {
    m_LayerMap.clear();

    flatbuffers::FlatBufferBuilder builder(1024);
    auto displayListsName =  builder.CreateString("DisplayLists");
    std::vector<flatbuffers::Offset<fbs::TreeNode>> displayVector = {};
    for(auto displaylist : displayLists) {
      auto serializtor =  tgfx::LayerSerialization::GetSerialization(displaylist._root);
      auto displaydata =  serializtor->SerializationLayerTreeNode(builder, m_LayerMap);
      displayVector.push_back(displaydata);
    }
    auto displayListsData =  fbs::CreateTreeNode(builder, displayListsName, 0,
      builder.CreateVector(displayVector));
    auto finalData = fbs::CreateFinalData(builder, fbs::Type::Type_TreeData,
      fbs::Data::Data_TreeNode, displayListsData.Union());
    builder.Finish(finalData);
    std::vector<uint8_t> blob(builder.GetBufferPointer(),
      builder.GetBufferPointer() + builder.GetSize());

    LAYER_DATA(blob);
  }

  void LayerInspector::serializingLayerAttribute(const std::shared_ptr<tgfx::Layer>& layer) {
    if(!layer) return;
    auto blob1 = LayerSerialization::serializingLayerAttribute(layer);
    // websocket send
    LAYER_DATA(blob1);
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
        break;
      }
      case HoverLayerAddress: {
        printf("Hover Layer Address: \n");
        printf("Address: %llu \n", feedbackData->address);
        m_HoveredAddress = feedbackData->address;
        AddHighLightOverlay(tgfx::Color::FromRGBA(255, 0, 0), m_LayerMap[m_HoveredAddress]);
        if(m_update)
          m_update();
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
    if(m_HoverdLayer) {
      m_HoverdLayer->removeChildren(m_HighLightLayerIndex);
    }
    m_HoverdLayer = hovedLayer;
    auto highlightLayer = tgfx::ShapeLayer::Make();
    highlightLayer->setBlendMode(tgfx::BlendMode::Lighten);
    auto rectPath = tgfx::Path();
    rectPath.addRect(m_HoverdLayer->getBounds());
    highlightLayer->setFillStyle(tgfx::SolidColor::Make(color));
    highlightLayer->setPath(rectPath);
    m_HoverdLayer->addChild(highlightLayer);
    m_HighLightLayerIndex = m_HoverdLayer->getChildIndex(highlightLayer);
  }
}


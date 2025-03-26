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
#pragma once
#include <unordered_map>
#include <vector>
#include <functional>
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/DisplayList.h"

namespace tgfx {

class LayerInspector {
public:
    static LayerInspector& GetLayerInspector() {
        static LayerInspector instance;
        return instance;
    }
    LayerInspector(const LayerInspector&) = delete;
    LayerInspector(LayerInspector&&) = delete;
    LayerInspector& operator==(const LayerInspector&) = delete;
    LayerInspector& operator==(LayerInspector&&) = delete;
    ~LayerInspector() = default;
    void serializingDisplayLists(const std::vector<tgfx::DisplayList>& displayLists);
    void serializingLayerAttribute(const std::shared_ptr<tgfx::Layer>& layer);
    void FeedBackDataProcess(const std::vector<uint8_t>& data);
    void setUpdateCallBack(std::function<void()> func) {
        m_update = func;
    }
private:
    void AddHighLightOverlay(Color color, std::shared_ptr<Layer> hovedLayer);
    LayerInspector();
private:
    std::unordered_map<uint64_t, std::shared_ptr<tgfx::Layer>> m_LayerMap;
    uint64_t m_HoveredAddress;
    uint64_t m_SelectedAddress;
    std::shared_ptr<tgfx::Layer> m_HoverdLayer;
    int m_HighLightLayerIndex = 0;

    std::function<void()> m_update;
};

}



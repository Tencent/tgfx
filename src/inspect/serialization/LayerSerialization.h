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

#include <functional>
#include "SerializationUtils.h"
#include "inspect/Protocol.h"
#include "tgfx/core/Data.h"

namespace tgfx {
static const std::string HighLightLayerName = "HighLightLayer";
class Layer;
class LayerSerialization {
 public:
  static std::shared_ptr<Data> SerializeTreeNode(
      std::shared_ptr<Layer> layer,
      std::unordered_map<uint64_t, std::shared_ptr<tgfx::Layer>>& layerMap);

  static std::shared_ptr<Data> SerializeLayer(
      const Layer* layer, SerializeUtils::ComplexObjSerMap* map,
      SerializeUtils::RenderableObjSerMap* rosMap,
      tgfx::inspect::LayerTreeMessage type = tgfx::inspect::LayerTreeMessage::LayerSubAttribute);

 private:
  static void SerializeTreeNodeImpl(
      flexbuffers::Builder& fbb, std::shared_ptr<Layer> layer,
      std::unordered_map<uint64_t, std::shared_ptr<tgfx::Layer>>& layerMap);

  static void SerializeBasicLayerImpl(flexbuffers::Builder& fbb, const Layer* layer,
                                      SerializeUtils::ComplexObjSerMap* map,
                                      SerializeUtils::RenderableObjSerMap* rosMap);
};
}  // namespace tgfx

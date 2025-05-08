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

#include <tgfx/core/Data.h>
#include <functional>
#include <variant>
#include "SerializationUtils.h"
#include "flatbuffers/flatbuffers.h"
#include "flatbuffers/flexbuffers.h"

namespace tgfx {
class Layer;

class LayerSerialization {
 public:
  using memberType =
      std::variant<Matrix, std::vector<std::shared_ptr<LayerFilter>>, std::shared_ptr<Layer>,
                   Layer*, std::reference_wrapper<std::unique_ptr<Rect>>,
                   std::reference_wrapper<std::unique_ptr<LayerContent>>,
                   std::vector<std::shared_ptr<Layer>>, std::vector<std::shared_ptr<LayerStyle>>,
                   std::shared_ptr<LayerFilter>, std::shared_ptr<LayerStyle>>;

  static std::shared_ptr<Data> serializeTreeNode(
      std::shared_ptr<Layer> layer,
      std::unordered_map<uint64_t, std::shared_ptr<tgfx::Layer>>& layerMap);

  static std::shared_ptr<Data> serializeLayer(Layer* layer);

 private:
  static void serializeTreeNodeImpl(
      flexbuffers::Builder& fbb, std::shared_ptr<Layer> layer,
      std::unordered_map<uint64_t, std::shared_ptr<tgfx::Layer>>& layerMap);

  static void serializeBasicLayerImpl(flexbuffers::Builder& fbb, Layer* layer);
  static void serializeImageLayerImpl(flexbuffers::Builder& fbb, Layer* layer);
  static void serializeShapeLayerImpl(flexbuffers::Builder& fbb, Layer* layer);
  static void serializeSolidLayerImpl(flexbuffers::Builder& fbb, Layer* layer);
  static void serializeTextLayerImpl(flexbuffers::Builder& fbb, Layer* layer);
};
}  // namespace tgfx

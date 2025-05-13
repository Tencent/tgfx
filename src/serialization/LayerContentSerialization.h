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

#include <tgfx/core/Data.h>
#include "SerializationUtils.h"

namespace tgfx {

class LayerContent;

class LayerContentSerialization {
 public:
  static std::shared_ptr<Data> Serialize(LayerContent* layerContent);

 private:
  static void SerializeBasicLayerContentImpl(flexbuffers::Builder& fbb, LayerContent* layerContent);
  static void SerializeComposeContentImpl(flexbuffers::Builder& fbb, LayerContent* layerContent);
  static void SerializeImageContentImpl(flexbuffers::Builder& fbb, LayerContent* layerContent);
  static void SerializeRasterizedContentImpl(flexbuffers::Builder& fbb, LayerContent* layerContent);
  static void SerializeShapeContentImpl(flexbuffers::Builder& fbb, LayerContent* layerContent);
  static void SerializeSolidContentImpl(flexbuffers::Builder& fbb, LayerContent* layerContent);
  static void SerializeTextContentImpl(flexbuffers::Builder& fbb, LayerContent* layerContent);
};
}  // namespace tgfx

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
class ShapeStyle;
class ShapeStyleSerialization {
 public:
  static std::shared_ptr<Data> Serialize(ShapeStyle* shapeStyle);

 private:
  static void SerializeShapeStyleImpl(flexbuffers::Builder& fbb, ShapeStyle* shapeStyle);
  static void SerializeImagePatternImpl(flexbuffers::Builder& fbb, ShapeStyle* shapeStyle);
  static void SerializeGradientImpl(flexbuffers::Builder& fbb, ShapeStyle* shapeStyle);
  static void SerializeLinearGradientImpl(flexbuffers::Builder& fbb, ShapeStyle* shapeStyle);
  static void SerializeRadialGradientImpl(flexbuffers::Builder& fbb, ShapeStyle* shapeStyle);
  static void SerializeConicGradientImpl(flexbuffers::Builder& fbb, ShapeStyle* shapeStyle);
  static void SerializeDiamondGradientImpl(flexbuffers::Builder& fbb, ShapeStyle* shapeStyle);
  static void SerializeSolidColorImpl(flexbuffers::Builder& fbb, ShapeStyle* shapeStyle);
};

}  // namespace tgfx

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
  static std::shared_ptr<Data> serializeShapeStyle(ShapeStyle* shapeStyle);

 private:
  static void serializeShapeStyleImpl(flexbuffers::Builder& fbb, ShapeStyle* shapeStyle);
  static void serializeImagePatternImpl(flexbuffers::Builder& fbb, ShapeStyle* shapeStyle);
  static void serializeGradientImpl(flexbuffers::Builder& fbb, ShapeStyle* shapeStyle);
  static void serializeLinearGradientImpl(flexbuffers::Builder& fbb, ShapeStyle* shapeStyle);
  static void serializeRadialGradientImpl(flexbuffers::Builder& fbb, ShapeStyle* shapeStyle);
  static void serializeConicGradientImpl(flexbuffers::Builder& fbb, ShapeStyle* shapeStyle);
  static void serializeDiamondGradientImpl(flexbuffers::Builder& fbb, ShapeStyle* shapeStyle);
  static void serializeSolidColorImpl(flexbuffers::Builder& fbb, ShapeStyle* shapeStyle);
};

}  // namespace tgfx

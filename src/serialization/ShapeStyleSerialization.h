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

#include <tgfx/core/Data.h>
#include "SerializationUtils.h"

namespace tgfx {
class ShapeStyle;
class ShapeStyleSerialization {
 public:
  static std::shared_ptr<Data> Serialize(const ShapeStyle* shapeStyle, SerializeUtils::Map* map);

 private:
  static void SerializeShapeStyleImpl(flexbuffers::Builder& fbb, const ShapeStyle* shapeStyle,
                                      SerializeUtils::Map* map);
  static void SerializeImagePatternImpl(flexbuffers::Builder& fbb, const ShapeStyle* shapeStyle,
                                        SerializeUtils::Map* map);
  static void SerializeGradientImpl(flexbuffers::Builder& fbb, const ShapeStyle* shapeStyle,
                                    SerializeUtils::Map* map);
  static void SerializeLinearGradientImpl(flexbuffers::Builder& fbb, const ShapeStyle* shapeStyle,
                                          SerializeUtils::Map* map);
  static void SerializeRadialGradientImpl(flexbuffers::Builder& fbb, const ShapeStyle* shapeStyle,
                                          SerializeUtils::Map* map);
  static void SerializeConicGradientImpl(flexbuffers::Builder& fbb, const ShapeStyle* shapeStyle,
                                         SerializeUtils::Map* map);
  static void SerializeDiamondGradientImpl(flexbuffers::Builder& fbb, const ShapeStyle* shapeStyle,
                                           SerializeUtils::Map* map);
  static void SerializeSolidColorImpl(flexbuffers::Builder& fbb, const ShapeStyle* shapeStyle,
                                      SerializeUtils::Map* map);
};

}  // namespace tgfx

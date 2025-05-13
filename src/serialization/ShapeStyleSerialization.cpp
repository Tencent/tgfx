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

#include "ShapeStyleSerialization.h"
#include <tgfx/layers/Gradient.h>
#include <tgfx/layers/ImagePattern.h>
#include <tgfx/layers/ShapeStyle.h>
#include <tgfx/layers/SolidColor.h>
#include "core/utils/Log.h"

namespace tgfx {

std::shared_ptr<Data> ShapeStyleSerialization::Serialize(ShapeStyle* shapeStyle) {
  DEBUG_ASSERT(shapeStyle != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  auto type = Types::Get(shapeStyle);
  switch (type) {
    case Types::ShapeStyleType::Gradient:
      SerializeGradientImpl(fbb, shapeStyle);
      break;
    case Types::ShapeStyleType::ImagePattern:
      SerializeImagePatternImpl(fbb, shapeStyle);
      break;
    case Types::ShapeStyleType::SolidColor:
      SerializeSolidColorImpl(fbb, shapeStyle);
      break;
  }
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}
void ShapeStyleSerialization::SerializeShapeStyleImpl(flexbuffers::Builder& fbb,
                                                      ShapeStyle* shapeStyle) {
  SerializeUtils::SetFlexBufferMap(fbb, "ShapeStyleType",
                                   SerializeUtils::ShapeStyleTypeToString(Types::Get(shapeStyle)));
  SerializeUtils::SetFlexBufferMap(fbb, "Alpha", shapeStyle->alpha());
  SerializeUtils::SetFlexBufferMap(fbb, "BlendMode",
                                   SerializeUtils::BlendModeToString(shapeStyle->blendMode()));
  SerializeUtils::SetFlexBufferMap(fbb, "Matrix", "", false, true);
}
void ShapeStyleSerialization::SerializeImagePatternImpl(flexbuffers::Builder& fbb,
                                                        ShapeStyle* shapeStyle) {
  SerializeShapeStyleImpl(fbb, shapeStyle);
  ImagePattern* imagePattern = static_cast<ImagePattern*>(shapeStyle);
  SerializeUtils::SetFlexBufferMap(fbb, "Image",
                                   reinterpret_cast<uint64_t>(imagePattern->image.get()), true,
                                   imagePattern->image != nullptr);
  SerializeUtils::SetFlexBufferMap(fbb, "TileModeX",
                                   SerializeUtils::TileModeToString(imagePattern->tileModeX));
  SerializeUtils::SetFlexBufferMap(fbb, "TileModeY",
                                   SerializeUtils::TileModeToString(imagePattern->tileModeY));
  SerializeUtils::SetFlexBufferMap(fbb, "Sampling", "", false, true);
}
void ShapeStyleSerialization::SerializeGradientImpl(flexbuffers::Builder& fbb,
                                                    ShapeStyle* shapeStyle) {
  SerializeShapeStyleImpl(fbb, shapeStyle);
  Gradient* gradient = static_cast<Gradient*>(shapeStyle);
  auto colors = gradient->colors();
  auto colorsSize = static_cast<unsigned int>(colors.size());
  SerializeUtils::SetFlexBufferMap(fbb, "Colors", colorsSize, false, colorsSize);
  auto positions = gradient->positions();
  auto positionsSize = static_cast<unsigned int>(positions.size());
  SerializeUtils::SetFlexBufferMap(fbb, "Positions", positionsSize, false, positionsSize);
  auto type = gradient->type();
  SerializeUtils::SetFlexBufferMap(fbb, "GradientType", SerializeUtils::GradientTypeToString(type));
  switch (type) {
    case GradientType::Linear:
      SerializeLinearGradientImpl(fbb, shapeStyle);
      break;
    case GradientType::Conic:
      SerializeConicGradientImpl(fbb, shapeStyle);
      break;
    case GradientType::Diamond:
      SerializeDiamondGradientImpl(fbb, shapeStyle);
      break;
    case GradientType::Radial:
      SerializeRadialGradientImpl(fbb, shapeStyle);
      break;
    default:
      LOGE("Unknown layer type!");
  }
}
void ShapeStyleSerialization::SerializeLinearGradientImpl(flexbuffers::Builder& fbb,
                                                          ShapeStyle* shapeStyle) {
  LinearGradient* lineGradient = static_cast<LinearGradient*>(shapeStyle);
  (void)lineGradient;
  SerializeUtils::SetFlexBufferMap(fbb, "StartPoint", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "EndPoint", "", false, true);
}
void ShapeStyleSerialization::SerializeRadialGradientImpl(flexbuffers::Builder& fbb,
                                                          ShapeStyle* shapeStyle) {
  RadialGradient* radialGradient = static_cast<RadialGradient*>(shapeStyle);
  SerializeUtils::SetFlexBufferMap(fbb, "Center", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "Radius", radialGradient->_radius);
}
void ShapeStyleSerialization::SerializeConicGradientImpl(flexbuffers::Builder& fbb,
                                                         ShapeStyle* shapeStyle) {
  ConicGradient* conicGradient = static_cast<ConicGradient*>(shapeStyle);
  SerializeUtils::SetFlexBufferMap(fbb, "Center", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "StartAngle", conicGradient->_startAngle);
  SerializeUtils::SetFlexBufferMap(fbb, "EndAngle", conicGradient->_endAngle);
}
void ShapeStyleSerialization::SerializeDiamondGradientImpl(flexbuffers::Builder& fbb,
                                                           ShapeStyle* shapeStyle) {
  DiamondGradient* diamondGradient = static_cast<DiamondGradient*>(shapeStyle);
  SerializeUtils::SetFlexBufferMap(fbb, "Center", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "HalfDiagonal", diamondGradient->_halfDiagonal);
}
void ShapeStyleSerialization::SerializeSolidColorImpl(flexbuffers::Builder& fbb,
                                                      ShapeStyle* shapeStyle) {
  SerializeShapeStyleImpl(fbb, shapeStyle);
  SolidColor* solidColor = static_cast<SolidColor*>(shapeStyle);
  (void)solidColor;
  SerializeUtils::SetFlexBufferMap(fbb, "Color", "", false, true);
}
}  // namespace tgfx
#endif
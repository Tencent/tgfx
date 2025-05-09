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

std::shared_ptr<Data> ShapeStyleSerialization::serializeShapeStyle(ShapeStyle* shapeStyle) {
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::serializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  auto type = shapeStyle->getType();
  switch (type) {
    case ShapeStyle::ShapeStyleType::Gradient:
      serializeGradientImpl(fbb, shapeStyle);
      break;
    case ShapeStyle::ShapeStyleType::ImagePattern:
      serializeImagePatternImpl(fbb, shapeStyle);
      break;
    case ShapeStyle::ShapeStyleType::SolidColor:
      serializeSolidColorImpl(fbb, shapeStyle);
      break;
  }
  SerializeUtils::serializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}
void ShapeStyleSerialization::serializeShapeStyleImpl(flexbuffers::Builder& fbb,
                                                      ShapeStyle* shapeStyle) {
  SerializeUtils::setFlexBufferMap(fbb, "Alpha", shapeStyle->_alpha);
  SerializeUtils::setFlexBufferMap(fbb, "BlendMode",
                                   SerializeUtils::blendModeToString(shapeStyle->_blendMode));
  SerializeUtils::setFlexBufferMap(fbb, "Matrix", "", false, true);
}
void ShapeStyleSerialization::serializeImagePatternImpl(flexbuffers::Builder& fbb,
                                                        ShapeStyle* shapeStyle) {
  serializeShapeStyleImpl(fbb, shapeStyle);
  ImagePattern* imagePattern = static_cast<ImagePattern*>(shapeStyle);
  SerializeUtils::setFlexBufferMap(fbb, "Image",
                                   reinterpret_cast<uint64_t>(imagePattern->image.get()), true,
                                   imagePattern->image != nullptr);
  SerializeUtils::setFlexBufferMap(fbb, "TileModeX",
                                   SerializeUtils::tileModeToString(imagePattern->tileModeX));
  SerializeUtils::setFlexBufferMap(fbb, "TileModeY",
                                   SerializeUtils::tileModeToString(imagePattern->tileModeY));
  SerializeUtils::setFlexBufferMap(fbb, "Sampling", "", false, true);
}
void ShapeStyleSerialization::serializeGradientImpl(flexbuffers::Builder& fbb,
                                                    ShapeStyle* shapeStyle) {
  serializeShapeStyleImpl(fbb, shapeStyle);
  Gradient* gradient = static_cast<Gradient*>(shapeStyle);
  auto colorsSize = static_cast<unsigned int>(gradient->_colors.size());
  SerializeUtils::setFlexBufferMap(fbb, "Colors", colorsSize, false, colorsSize);
  auto positionsSize = static_cast<unsigned int>(gradient->_positions.size());
  SerializeUtils::setFlexBufferMap(fbb, "Positions", positionsSize, false, positionsSize);
  auto type = gradient->type();
  switch (type) {
    case GradientType::Linear:
      serializeLinearGradientImpl(fbb, shapeStyle);
      break;
    case GradientType::Conic:
      serializeConicGradientImpl(fbb, shapeStyle);
      break;
    case GradientType::Diamond:
      serializeDiamondGradientImpl(fbb, shapeStyle);
      break;
    case GradientType::Radial:
      serializeRadialGradientImpl(fbb, shapeStyle);
      break;
    default:
      LOGE("Unknown layer type!");
  }
}
void ShapeStyleSerialization::serializeLinearGradientImpl(flexbuffers::Builder& fbb,
                                                          ShapeStyle* shapeStyle) {
  LinearGradient* lineGradient = static_cast<LinearGradient*>(shapeStyle);
  (void)lineGradient;
  SerializeUtils::setFlexBufferMap(fbb, "StartPoint", "", false, true);
  SerializeUtils::setFlexBufferMap(fbb, "EndPoint", "", false, true);
}
void ShapeStyleSerialization::serializeRadialGradientImpl(flexbuffers::Builder& fbb,
                                                          ShapeStyle* shapeStyle) {
  RadialGradient* radialGradient = static_cast<RadialGradient*>(shapeStyle);
  SerializeUtils::setFlexBufferMap(fbb, "Center", "", false, true);
  SerializeUtils::setFlexBufferMap(fbb, "Radius", radialGradient->_radius);
}
void ShapeStyleSerialization::serializeConicGradientImpl(flexbuffers::Builder& fbb,
                                                         ShapeStyle* shapeStyle) {
  ConicGradient* conicGradient = static_cast<ConicGradient*>(shapeStyle);
  SerializeUtils::setFlexBufferMap(fbb, "Center", "", false, true);
  SerializeUtils::setFlexBufferMap(fbb, "StartAngle", conicGradient->_startAngle);
  SerializeUtils::setFlexBufferMap(fbb, "EndAngle", conicGradient->_endAngle);
}
void ShapeStyleSerialization::serializeDiamondGradientImpl(flexbuffers::Builder& fbb,
                                                           ShapeStyle* shapeStyle) {
  DiamondGradient* diamondGradient = static_cast<DiamondGradient*>(shapeStyle);
  SerializeUtils::setFlexBufferMap(fbb, "Center", "", false, true);
  SerializeUtils::setFlexBufferMap(fbb, "HalfDiagonal", diamondGradient->_halfDiagonal);
}
void ShapeStyleSerialization::serializeSolidColorImpl(flexbuffers::Builder& fbb,
                                                      ShapeStyle* shapeStyle) {
  serializeShapeStyleImpl(fbb, shapeStyle);
  SolidColor* solidColor = static_cast<SolidColor*>(shapeStyle);
  (void)solidColor;
  SerializeUtils::setFlexBufferMap(fbb, "Color", "", false, true);
}
}  // namespace tgfx
#endif
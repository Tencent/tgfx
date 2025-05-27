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

std::shared_ptr<Data> ShapeStyleSerialization::Serialize(const ShapeStyle* shapeStyle,
                                                         SerializeUtils::Map* map) {
  DEBUG_ASSERT(shapeStyle != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerSubAttribute", startMap, contentMap);
  auto type = Types::Get(shapeStyle);
  switch (type) {
    case Types::ShapeStyleType::Gradient:
      SerializeGradientImpl(fbb, shapeStyle, map);
      break;
    case Types::ShapeStyleType::ImagePattern:
      SerializeImagePatternImpl(fbb, shapeStyle, map);
      break;
    case Types::ShapeStyleType::SolidColor:
      SerializeSolidColorImpl(fbb, shapeStyle, map);
      break;
  }
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void ShapeStyleSerialization::SerializeShapeStyleImpl(flexbuffers::Builder& fbb,
                                                      const ShapeStyle* shapeStyle,
                                                      SerializeUtils::Map* map) {
  SerializeUtils::SetFlexBufferMap(fbb, "shapeStyleType",
                                   SerializeUtils::ShapeStyleTypeToString(Types::Get(shapeStyle)));
  SerializeUtils::SetFlexBufferMap(fbb, "alpha", shapeStyle->alpha());
  SerializeUtils::SetFlexBufferMap(fbb, "blendMode",
                                   SerializeUtils::BlendModeToString(shapeStyle->blendMode()));

  auto matrixID = SerializeUtils::GetObjID();
  auto matrix = shapeStyle->matrix();
  SerializeUtils::SetFlexBufferMap(fbb, "matrix", "", false, true, matrixID);
  SerializeUtils::FillMap(matrix, matrixID, map);
}

void ShapeStyleSerialization::SerializeImagePatternImpl(flexbuffers::Builder& fbb,
                                                        const ShapeStyle* shapeStyle,
                                                        SerializeUtils::Map* map) {
  SerializeShapeStyleImpl(fbb, shapeStyle, map);
  const ImagePattern* imagePattern = static_cast<const ImagePattern*>(shapeStyle);

  auto imageID = SerializeUtils::GetObjID();
  auto image = imagePattern->image();
  SerializeUtils::SetFlexBufferMap(fbb, "image", reinterpret_cast<uint64_t>(image.get()), true,
                                   image != nullptr, imageID);
  SerializeUtils::FillMap(image, imageID, map);

  SerializeUtils::SetFlexBufferMap(fbb, "tileModeX",
                                   SerializeUtils::TileModeToString(imagePattern->tileModeX()));
  SerializeUtils::SetFlexBufferMap(fbb, "tileModeY",
                                   SerializeUtils::TileModeToString(imagePattern->tileModeY()));

  auto samplingID = SerializeUtils::GetObjID();
  auto sampling = imagePattern->samplingOptions();
  SerializeUtils::SetFlexBufferMap(fbb, "sampling", "", false, true, samplingID);
  SerializeUtils::FillMap(sampling, samplingID, map);
}

void ShapeStyleSerialization::SerializeGradientImpl(flexbuffers::Builder& fbb,
                                                    const ShapeStyle* shapeStyle,
                                                    SerializeUtils::Map* map) {
  SerializeShapeStyleImpl(fbb, shapeStyle, map);
  const Gradient* gradient = static_cast<const Gradient*>(shapeStyle);

  auto colorsID = SerializeUtils::GetObjID();
  auto colors = gradient->colors();
  auto colorsSize = static_cast<unsigned int>(colors.size());
  SerializeUtils::SetFlexBufferMap(fbb, "colors", colorsSize, false, colorsSize, colorsID);
  SerializeUtils::FillMap(colors, colorsID, map);

  auto positionsID = SerializeUtils::GetObjID();
  auto positions = gradient->positions();
  auto positionsSize = static_cast<unsigned int>(positions.size());
  SerializeUtils::SetFlexBufferMap(fbb, "positions", positionsSize, false, positionsSize,
                                   positionsID);
  SerializeUtils::FillMap(positions, positionsID, map);

  auto type = gradient->type();
  SerializeUtils::SetFlexBufferMap(fbb, "gradientType", SerializeUtils::GradientTypeToString(type));
  switch (type) {
    case GradientType::Linear:
      SerializeLinearGradientImpl(fbb, shapeStyle, map);
      break;
    case GradientType::Conic:
      SerializeConicGradientImpl(fbb, shapeStyle, map);
      break;
    case GradientType::Diamond:
      SerializeDiamondGradientImpl(fbb, shapeStyle, map);
      break;
    case GradientType::Radial:
      SerializeRadialGradientImpl(fbb, shapeStyle, map);
      break;
    default:
      LOGE("Unknown layer type!");
  }
}

void ShapeStyleSerialization::SerializeLinearGradientImpl(flexbuffers::Builder& fbb,
                                                          const ShapeStyle* shapeStyle,
                                                          SerializeUtils::Map* map) {
  const LinearGradient* lineGradient = static_cast<const LinearGradient*>(shapeStyle);

  auto startPointID = SerializeUtils::GetObjID();
  auto startPoint = lineGradient->startPoint();
  SerializeUtils::SetFlexBufferMap(fbb, "startPoint", "", false, true, startPointID);
  SerializeUtils::FillMap(startPoint, startPointID, map);

  auto endPointID = SerializeUtils::GetObjID();
  auto endPoint = lineGradient->endPoint();
  SerializeUtils::SetFlexBufferMap(fbb, "endPoint", "", false, true, endPointID);
  SerializeUtils::FillMap(endPoint, endPointID, map);
}

void ShapeStyleSerialization::SerializeRadialGradientImpl(flexbuffers::Builder& fbb,
                                                          const ShapeStyle* shapeStyle,
                                                          SerializeUtils::Map* map) {
  const RadialGradient* radialGradient = static_cast<const RadialGradient*>(shapeStyle);

  auto centerID = SerializeUtils::GetObjID();
  auto center = radialGradient->center();
  SerializeUtils::SetFlexBufferMap(fbb, "center", "", false, true, centerID);
  SerializeUtils::FillMap(center, centerID, map);

  SerializeUtils::SetFlexBufferMap(fbb, "radius", radialGradient->radius());
}

void ShapeStyleSerialization::SerializeConicGradientImpl(flexbuffers::Builder& fbb,
                                                         const ShapeStyle* shapeStyle,
                                                         SerializeUtils::Map* map) {
  const ConicGradient* conicGradient = static_cast<const ConicGradient*>(shapeStyle);

  auto centerID = SerializeUtils::GetObjID();
  auto center = conicGradient->center();
  SerializeUtils::SetFlexBufferMap(fbb, "center", "", false, true, centerID);
  SerializeUtils::FillMap(center, centerID, map);

  SerializeUtils::SetFlexBufferMap(fbb, "startAngle", conicGradient->startAngle());
  SerializeUtils::SetFlexBufferMap(fbb, "endAngle", conicGradient->endAngle());
}

void ShapeStyleSerialization::SerializeDiamondGradientImpl(flexbuffers::Builder& fbb,
                                                           const ShapeStyle* shapeStyle,
                                                           SerializeUtils::Map* map) {
  const DiamondGradient* diamondGradient = static_cast<const DiamondGradient*>(shapeStyle);

  auto centerID = SerializeUtils::GetObjID();
  auto center = diamondGradient->center();
  SerializeUtils::SetFlexBufferMap(fbb, "center", "", false, true, centerID);
  SerializeUtils::FillMap(center, centerID, map);

  SerializeUtils::SetFlexBufferMap(fbb, "halfDiagonal", diamondGradient->halfDiagonal());
}

void ShapeStyleSerialization::SerializeSolidColorImpl(flexbuffers::Builder& fbb,
                                                      const ShapeStyle* shapeStyle,
                                                      SerializeUtils::Map* map) {
  SerializeShapeStyleImpl(fbb, shapeStyle, map);
  const SolidColor* solidColor = static_cast<const SolidColor*>(shapeStyle);

  auto colorID = SerializeUtils::GetObjID();
  auto color = solidColor->color();
  SerializeUtils::SetFlexBufferMap(fbb, "color", "", false, true, colorID);
  SerializeUtils::FillMap(color, colorID, map);
}
}  // namespace tgfx
#endif
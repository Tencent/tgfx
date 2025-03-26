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

#include "ShapeStyleSerialization.h"
#include <tgfx/layers/Gradient.h>
#include <tgfx/layers/ImagePattern.h>
#include <tgfx/layers/ShapeStyle.h>

#include "core/utils/Log.h"

namespace tgfx {

std::shared_ptr<ShapeStyleSerialization> ShapeStyleSerialization::GetSerialization(std::shared_ptr<ShapeStyle> shapeStyle)
{
  auto shapeStyleType = shapeStyle->getType();

  if(shapeStyleType == ShapeStyleType::Gradient) {
    auto ptr = std::static_pointer_cast<Gradient>(shapeStyle);
    auto type = ptr->type();
    switch(type) {
      case GradientType::Linear:
        return std::make_shared<LinearGradientSerialization>(shapeStyle);
      case GradientType::Radial:
        return std::make_shared<RadialGradientSerialization>(shapeStyle);
      case GradientType::Conic:
        return std::make_shared<ConicGradientSerialization>(shapeStyle);
      case GradientType::Diamond:
        return std::make_shared<DiamondGradientSerialization>(shapeStyle);
      default:
        LOGE("Unknown gradient type!");
        return nullptr;
    }
  }

  if(shapeStyleType == ShapeStyleType::ImagePattern) {
    return std::make_shared<ImagePatternStyleSerialization>(shapeStyle);
  }

  return nullptr;
}

ShapeStyleSerialization::ShapeStyleSerialization(std::shared_ptr<ShapeStyle> shapeStyle)
  :m_ShapeStyle(shapeStyle)
{

}

uint32_t ShapeStyleSerialization::Serialization(flatbuffers::FlatBufferBuilder& builder) {
  (void)builder;
  m_ShapeStyleCommonAttribute = fbs::ShapeStyleCommonAttribute(m_ShapeStyle->alpha(),
    static_cast<fbs::BlendMode>(m_ShapeStyle->blendMode()));
  return uint32_t(-1);
}

ImagePatternStyleSerialization::ImagePatternStyleSerialization(std::shared_ptr<ShapeStyle> shapeStyle)
  :ShapeStyleSerialization(shapeStyle)
{
}

uint32_t ImagePatternStyleSerialization::Serialization(flatbuffers::FlatBufferBuilder& builder) {
  ShapeStyleSerialization::Serialization(builder);
  std::shared_ptr<ImagePattern> ptr = std::static_pointer_cast<ImagePattern>(m_ShapeStyle);
  auto img = ptr->image;
  auto image = fbs::ImageAttribute(static_cast<fbs::ImageType>(img->type()), img->width(), img->height(),
    img->isAlphaOnly(), img->hasMipmaps(), img->isFullyDecoded(), img->isTextureBacked());
  auto imagePattern = fbs::CreateImagePatternAttribute(builder,
    &m_ShapeStyleCommonAttribute, static_cast<fbs::TileMode>(ptr->tileModeX),
    static_cast<fbs::TileMode>(ptr->tileModeY), static_cast<fbs::FilterMode>(ptr->sampling.filterMode),
    static_cast<fbs::MipmapMode>(ptr->sampling.mipmapMode), &image);
  auto shapeStyle = fbs::CreateShapeStyle(builder,
    fbs::ShapeStyleType::ShapeStyleType_ImagePattern,
    fbs::ShapeStyleAttribute::ShapeStyleAttribute_ImagePatternAttribute,
    imagePattern.Union());
  return shapeStyle.o;
}

GradientStyleSerialization::GradientStyleSerialization(std::shared_ptr<ShapeStyle> shapeStyle)
  :ShapeStyleSerialization(shapeStyle)
{
}

uint32_t GradientStyleSerialization::Serialization(flatbuffers::FlatBufferBuilder& builder) {
  std::shared_ptr<Gradient> ptr = std::static_pointer_cast<Gradient>(m_ShapeStyle);
  std::vector<fbs::Color> colors;
  for(auto color : ptr->colors()) {
    auto c = fbs::Color(color.red, color.green, color.blue, color.alpha);
    colors.push_back(c);
  }
  auto colorList = builder.CreateVectorOfStructs(colors);
  auto positions = builder.CreateVector(ptr->positions());
  auto gradient = fbs::CreateGradientAttribute(builder,
    &m_ShapeStyleCommonAttribute, static_cast<fbs::GradientType>(ptr->type()), colorList, positions);
  return gradient.o;
}

LinearGradientSerialization::LinearGradientSerialization(std::shared_ptr<ShapeStyle> shapeStyle)
  :GradientStyleSerialization(shapeStyle)
{
}

uint32_t LinearGradientSerialization::Serialization(flatbuffers::FlatBufferBuilder& builder) {
  auto gradient =  GradientStyleSerialization::Serialization(builder);
  std::shared_ptr<LinearGradient> ptr = std::static_pointer_cast<LinearGradient>(m_ShapeStyle);
  auto startPoint = fbs::Point(ptr->startPoint().x, ptr->startPoint().y);
  auto endPoint = fbs::Point(ptr->endPoint().x, ptr->endPoint().y);
  auto LinearGradient = fbs::CreateLinearGradientAttribute(builder,
    gradient, &startPoint, &endPoint);
  auto shapeStyle = fbs::CreateShapeStyle(builder,
    fbs::ShapeStyleType::ShapeStyleType_LinearGradient,
    fbs::ShapeStyleAttribute::ShapeStyleAttribute_LinearGradientAttribute,
    LinearGradient.Union());
  return shapeStyle.o;
}

RadialGradientSerialization::RadialGradientSerialization(std::shared_ptr<ShapeStyle> shapeStyle)
  :GradientStyleSerialization(shapeStyle)
{
}

uint32_t RadialGradientSerialization::Serialization(flatbuffers::FlatBufferBuilder& builder) {
  auto gradient = GradientStyleSerialization::Serialization(builder);
  std::shared_ptr<RadialGradient> ptr = std::static_pointer_cast<RadialGradient>(m_ShapeStyle);
  auto center = fbs::Point(ptr->center().x, ptr->center().y);
  auto radialGradient = fbs::CreateRadialGradientAttribute(builder,
    gradient, &center, ptr->radius());
  auto shapeStyle = fbs::CreateShapeStyle(builder,
    fbs::ShapeStyleType::ShapeStyleType_RadiusGradient,
    fbs::ShapeStyleAttribute::ShapeStyleAttribute_RadialGradientAttribute,
    radialGradient.Union());
  return shapeStyle.o;
}

ConicGradientSerialization::ConicGradientSerialization(std::shared_ptr<ShapeStyle> shapeStyle)
  :GradientStyleSerialization(shapeStyle)
{

}

uint32_t ConicGradientSerialization::Serialization(flatbuffers::FlatBufferBuilder& builder) {
  auto gradient = GradientStyleSerialization::Serialization(builder);
  std::shared_ptr<ConicGradient> ptr = std::static_pointer_cast<ConicGradient>(m_ShapeStyle);
  auto center = fbs::Point(ptr->center().x, ptr->center().y);
  auto conicGradient = fbs::CreateConicGradientAttribute(builder,
    gradient, &center, ptr->startAngle(), ptr->endAngle());
  auto shapeStyle = fbs::CreateShapeStyle(builder,
    fbs::ShapeStyleType::ShapeStyleType_ConicGradient,
    fbs::ShapeStyleAttribute::ShapeStyleAttribute_ConicGradientAttribute,
    conicGradient.Union());
  return shapeStyle.o;
}

DiamondGradientSerialization::DiamondGradientSerialization(std::shared_ptr<ShapeStyle> shapeStyle)
  :GradientStyleSerialization(shapeStyle)
{

}

uint32_t DiamondGradientSerialization::Serialization(flatbuffers::FlatBufferBuilder& builder) {
  auto gradient = GradientStyleSerialization::Serialization(builder);
  std::shared_ptr<DiamondGradient> ptr = std::static_pointer_cast<DiamondGradient>(m_ShapeStyle);
  auto center = fbs::Point(ptr->center().x, ptr->center().y);
  auto diamondGradient = fbs::CreateDiamondGradientAttribute(builder,
    gradient, &center, ptr->halfDiagonal());
  auto shapeStyle = fbs::CreateShapeStyle(builder,
    fbs::ShapeStyleType::ShapeStyleType_DiamondGradient,
    fbs::ShapeStyleAttribute::ShapeStyleAttribute_DiamondGradientAttribute,
    diamondGradient.Union());
  return shapeStyle.o;
}
}
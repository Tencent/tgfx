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

#include "tgfx/layers/Layer.h"
#include "LayerSerialization.h"
#include <tgfx/core/GradientType.h>
#include <tgfx/layers/ImageLayer.h>
#include <tgfx/layers/ShapeLayer.h>
#include <tgfx/layers/SolidLayer.h>
#include <tgfx/layers/TextLayer.h>
#include <tgfx/layers/filters/InnerShadowFilter.h>

#include "LayerFilterSerialization.h"
#include "LayerStyleSerialization.h"
#include "ShapeStyleSerialization.h"
#include "core/utils/Log.h"
#include "layers/generate/SerializationStructure_generated.h"

namespace tgfx {

std::vector<uint8_t> LayerSerialization::serializingLayerAttribute(std::shared_ptr<Layer> layer) {
  flatbuffers::FlatBufferBuilder builder(1024);
  auto serialization = LayerSerialization::GetSerialization(layer);
  uint32_t data = serialization->SerializationLayerAttribute(builder);
  auto finalData = fbs::CreateFinalData(builder, fbs::Type::Type_LayerData,
    fbs::Data::Data_Layer, data);
  builder.Finish(finalData);
  return std::vector<uint8_t>(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
}

std::vector<uint8_t> LayerSerialization::serializingTreeNode(std::shared_ptr<Layer> layer) {
  flatbuffers::FlatBufferBuilder builder(1024);
  auto treeNodeSerialization = LayerSerialization::GetSerialization(layer);
  uint32_t data = treeNodeSerialization->SerializationLayerTreeNode(builder);
  auto finalData = fbs::CreateFinalData(builder, fbs::Type::Type_TreeData,
    fbs::Data::Data_TreeNode, data);
  builder.Finish(finalData);
  return std::vector<uint8_t>(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
}

std::shared_ptr<LayerSerialization> LayerSerialization::GetSerialization(
    std::shared_ptr<Layer> layer) {
  auto type = layer->type();
  switch(type) {
    case LayerType::Image:
      return std::make_shared<ImageLayerSerialization>(layer);
    case LayerType::Shape:
      return std::make_shared<ShapeLayerSerialization>(layer);
    case LayerType::Text:
      return std::make_shared<TextLayerSerialization>(layer);
    case LayerType::Solid:
      return std::make_shared<SolidLayerSerialization>(layer);
    case LayerType::Layer:
      return std::make_shared<BasicLayerSerialization>(layer);
    case LayerType::Gradient:
      return nullptr;
    default:
      LOGE("Unknown layer type!");
      return nullptr;
  }
}

  LayerSerialization::LayerSerialization(std::shared_ptr<tgfx::Layer> layer)
    :m_Layer(layer)
  {
  }

  uint32_t LayerSerialization::SerializationLayerAttribute(flatbuffers::FlatBufferBuilder& builder) {
    auto layerName = builder.CreateString(m_Layer->name());
    auto position = fbs::Point(m_Layer->position().x, m_Layer->position().y);

    const auto& layerStyles = m_Layer->layerStyles();
    std::vector<flatbuffers::Offset<fbs::LayerStyle>> styles;
    for(const auto& layerStyle : layerStyles) {
      auto layerStyleSerialization = LayerStyleSerialization::GetSerialization(layerStyle);
      if(!layerStyleSerialization)
        continue;
      styles.emplace_back(layerStyleSerialization->Serialization(builder));
    }
    auto layer_styles = builder.CreateVector(styles);

    const auto& layerFilters = m_Layer->filters();
    std::vector<flatbuffers::Offset<fbs::LayerFilter>> filters;
    for(const auto& layerFilter : layerFilters) {
      auto layerFilterSerialization = LayerFilterSerialization::GetSerialization(layerFilter);
      if(!layerFilterSerialization)
        continue;
      filters.emplace_back(layerFilterSerialization->Serialization(builder));
    }
    auto layer_filters = builder.CreateVector(filters);

    auto layerCommonAttribute = tgfx::fbs::CreateLayerCommonAttribute(builder,
      static_cast<tgfx::fbs::LayerType>(m_Layer->type()), layerName, m_Layer->alpha(),
      static_cast<tgfx::fbs::BlendMode>(m_Layer->blendMode()), &position, m_Layer->visible(),
      m_Layer->shouldRasterize(), m_Layer->rasterizationScale(),
      m_Layer->allowsEdgeAntialiasing(), m_Layer->allowsGroupOpacity(), layer_styles, layer_filters);

    return layerCommonAttribute.o;
  }

  uint32_t LayerSerialization::SerializationLayerTreeNode(flatbuffers::FlatBufferBuilder& builder) {
    return createTreeNode(builder, m_Layer);
  }

  uint32_t LayerSerialization::createTreeNode(flatbuffers::FlatBufferBuilder& builder,
      std::shared_ptr<Layer> layer) {
    auto layerName = layer->name();
    auto name = builder.CreateString(layerName);
    std::vector<flatbuffers::Offset<fbs::TreeNode>> childrenList;
    auto children = layer->children();
    for(auto child : children) {
      childrenList.push_back(createTreeNode(builder, child));
    }
    auto treeNode = fbs::CreateTreeNode(builder, name,
      builder.CreateVector(childrenList));
    return treeNode.o;
  }

  BasicLayerSerialization::BasicLayerSerialization(std::shared_ptr<Layer> layer)
    :LayerSerialization(layer)
  {
  }

  uint32_t BasicLayerSerialization::SerializationLayerAttribute(flatbuffers::FlatBufferBuilder& builder) {
    auto basicLayer = LayerSerialization::SerializationLayerAttribute(builder);
    auto layer = fbs::CreateLayer(builder, fbs::LayerType::LayerType_Layer,
      fbs::LayerAttribute::LayerAttribute_LayerCommonAttribute, basicLayer);
    return layer.o;
  }


  ImageLayerSerialization::ImageLayerSerialization(std::shared_ptr<Layer> layer)
    :LayerSerialization(layer)
  {
  }

  uint32_t ImageLayerSerialization::SerializationLayerAttribute(flatbuffers::FlatBufferBuilder& builder) {
    auto basicLayer = LayerSerialization::SerializationLayerAttribute(builder);
    std::shared_ptr<ImageLayer> ptr = std::static_pointer_cast<ImageLayer>(m_Layer);
    auto image = ptr->image();
    auto img = fbs::ImageAttribute(static_cast<int>(image->type()), image->width(), image->height(),
      image->isAlphaOnly(), image->hasMipmaps(), image->isFullyDecoded(),
      image->isTextureBacked());
    auto ImageLayer = fbs::CreateImageLayerAttribute(builder, basicLayer,
      static_cast<fbs::FilterMode>(ptr->sampling().filterMode),
      static_cast<fbs::MipmapMode>(ptr->sampling().mipmapMode), &img);
    auto layer = fbs::CreateLayer(builder, fbs::LayerType::LayerType_Image,
      fbs::LayerAttribute::LayerAttribute_ImageLayerAttribute, ImageLayer.Union());
    return layer.o;
  }

  ShapeLayerSerialization::ShapeLayerSerialization(std::shared_ptr<Layer> layer)
    :LayerSerialization(layer)
  {
  }

  uint32_t ShapeLayerSerialization::SerializationLayerAttribute(flatbuffers::FlatBufferBuilder& builder) {
    auto basicLayer =  LayerSerialization::SerializationLayerAttribute(builder);
    std::shared_ptr<ShapeLayer> ptr = std::static_pointer_cast<ShapeLayer>(m_Layer);
    Path path = ptr->path();
    auto rect = fbs::Rect(path.getBounds().left, path.getBounds().top, path.getBounds().right,
      path.getBounds().bottom);

    const auto shapeStyles = ptr->fillStyles();
    std::vector<flatbuffers::Offset<fbs::ShapeStyle>> styles;
    for(const auto& shapeStyle : shapeStyles) {
      auto shapeStyleSerialization = ShapeStyleSerialization::GetSerialization(shapeStyle);
      if(!shapeStyleSerialization)
        continue;
      uint32_t shape_Style = shapeStyleSerialization->Serialization(builder);
      styles.emplace_back(shape_Style);
    }
    auto shapeStylesAttributes = builder.CreateVector(styles);
    auto shapelayer = fbs::CreateShapeLayerAttribute(builder, basicLayer,
      static_cast<fbs::PathFillType>(path.getFillType()), path.isLine(), path.isRect(),
      path.isOval(), path.isEmpty(), &rect, path.countPoints(), path.countVerbs(),
      shapeStylesAttributes);
    auto layer = fbs::CreateLayer(builder, fbs::LayerType::LayerType_Shape,
      fbs::LayerAttribute::LayerAttribute_ShapeLayerAttribute, shapelayer.Union());
    return layer.o;
  }

  SolidLayerSerialization::SolidLayerSerialization(std::shared_ptr<Layer> layer)
    :LayerSerialization(layer)
  {
  }

  uint32_t SolidLayerSerialization::SerializationLayerAttribute(flatbuffers::FlatBufferBuilder& builder) {
    auto basicLayer =  LayerSerialization::SerializationLayerAttribute(builder);
    std::shared_ptr<SolidLayer> ptr = std::static_pointer_cast<SolidLayer>(m_Layer);
    auto color = fbs::Color(ptr->color().red, ptr->color().green, ptr->color().blue, ptr->color().alpha);
    auto solidLayer = fbs::CreateSolidLayerAttribute(builder, basicLayer,
      ptr->width(), ptr->height(), ptr->radiusX(), ptr->radiusY(), &color);
  auto layer = fbs::CreateLayer(builder, fbs::LayerType::LayerType_Solid,
    fbs::LayerAttribute::LayerAttribute_SolidLayerAttribute, solidLayer.Union());
  return layer.o;
  }

  TextLayerSerialization::TextLayerSerialization(std::shared_ptr<Layer> layer)
    :LayerSerialization(layer)
  {
  }

  uint32_t TextLayerSerialization::SerializationLayerAttribute(flatbuffers::FlatBufferBuilder& builder) {
    auto basicLayer =  LayerSerialization::SerializationLayerAttribute(builder);
    std::shared_ptr<TextLayer> ptr = std::static_pointer_cast<TextLayer>(m_Layer);
    auto text_string = builder.CreateString(ptr->text());
    auto text_color = fbs::Color(ptr->textColor().red, ptr->textColor().green, ptr->textColor().blue,
      ptr->textColor().alpha);

    auto typeface = ptr->font().getTypeface();
    auto font_family = builder.CreateString(typeface->fontFamily());
    auto font_style = builder.CreateString(typeface->fontStyle());
    auto type_face = fbs::CreateTypeFace(builder, typeface->uniqueID(),
      font_family, font_style, (uint32_t)typeface->glyphsCount(), typeface->unitsPerEm(),
      typeface->hasColor(), typeface->hasOutlines());

    auto font = ptr->font();
    auto font_ = fbs::CreateFont(builder, font.hasColor(), font.hasOutlines(),
      font.getSize(), font.isFauxBold(), font.isFauxItalic(), type_face);

    auto fontMetrics = ptr->font().getMetrics();
    auto font_metrics = fbs::FontMetrics(fontMetrics.top, fontMetrics.ascent, fontMetrics.descent,
      fontMetrics.bottom, fontMetrics.leading, fontMetrics.xMin, fontMetrics.xMax,
      fontMetrics.xHeight, fontMetrics.capHeight, fontMetrics.underlineThickness,
      fontMetrics.underlinePosition);

    auto textLayer = fbs::CreateTextLayerAttribute(builder, basicLayer,
      text_string, &text_color, font_, &font_metrics, ptr->width(), ptr->height(),
      static_cast<fbs::TextAlign>(ptr->textAlign()), ptr->autoWrap());

  auto layer = fbs::CreateLayer(builder, fbs::LayerType::LayerType_Text,
      fbs::LayerAttribute::LayerAttribute_TextLayerAttribute, textLayer.Union());
  return layer.o;
  }
}

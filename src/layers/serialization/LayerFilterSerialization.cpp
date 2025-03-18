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

#include "LayerFilterSerialization.h"
#include <tgfx/layers/filters/BlendFilter.h>
#include <tgfx/layers/filters/BlurFilter.h>
#include <tgfx/layers/filters/ColorMatrixFilter.h>
#include <tgfx/layers/filters/DropShadowFilter.h>
#include <tgfx/layers/filters/InnerShadowFilter.h>

#include "core/utils/Log.h"
#include "tgfx/layers/filters/LayerFilter.h"

namespace tgfx {

std::shared_ptr<LayerFilterSerialization> LayerFilterSerialization::GetSerialization(std::shared_ptr<LayerFilter> LayerFilter) {
  auto type = LayerFilter->Type();
  switch(type) {
    case LayerFilterType::BlendFilter:
      return std::make_shared<BlendFilterSerialization>(LayerFilter);
    case LayerFilterType::BlurFilter:
      return std::make_shared<BlurFilterSerialization>(LayerFilter);
    case LayerFilterType::ColorMatrixFliter:
      return std::make_shared<ColorMatrixFilterSerialization>(LayerFilter);
    case LayerFilterType::DropShadowFilter:
      return std::make_shared<DropShadowFilterSerialization>(LayerFilter);
    case LayerFilterType::InnerShadowFilter:
      return std::make_shared<InnerShadowFilterSerialization>(LayerFilter);
    default:
      LOGE("Unknown layerfilter type!");
      return nullptr;
  }
}

LayerFilterSerialization::LayerFilterSerialization(std::shared_ptr<LayerFilter> layerFilter)
  :m_LayerFilter(layerFilter)
{
}

uint32_t LayerFilterSerialization::Serialization(flatbuffers::FlatBufferBuilder& builder) {
  (void)builder;
  m_LayerfilterCommonAttribute = fbs::LayerfilterCommonAttribute(static_cast<fbs::LayerFilterType>(
    m_LayerFilter->Type()));
  return uint32_t(-1);
}

BlendFilterSerialization::BlendFilterSerialization(std::shared_ptr<LayerFilter> layerFilter)
  :LayerFilterSerialization(layerFilter)
{
}

uint32_t BlendFilterSerialization::Serialization(flatbuffers::FlatBufferBuilder& builder) {
  LayerFilterSerialization::Serialization(builder);
  std::shared_ptr<BlendFilter> ptr = std::static_pointer_cast<BlendFilter>(m_LayerFilter);
  auto color = fbs::Color(ptr->color().red, ptr->color().green, ptr->color().blue,
    ptr->color().alpha);
  auto blendFilter = fbs::CreateBlendFilterAttribute(builder,
    &m_LayerfilterCommonAttribute, &color, static_cast<fbs::BlendMode>(ptr->blendMode()));
  auto layerFilter = fbs::CreateLayerFilter(builder,
    fbs::LayerFilterType::LayerFilterType_BlendFilter,
    fbs::LayerFilterAttribute::LayerFilterAttribute_BlendFilterAttribute,
    blendFilter.Union());
  return layerFilter.o;
}

BlurFilterSerialization::BlurFilterSerialization(std::shared_ptr<LayerFilter> layerFilter)
  :LayerFilterSerialization(layerFilter)
{
}

uint32_t BlurFilterSerialization::Serialization(flatbuffers::FlatBufferBuilder& builder) {
  LayerFilterSerialization::Serialization(builder);
  std::shared_ptr<BlurFilter> ptr = std::static_pointer_cast<BlurFilter>(m_LayerFilter);
  auto blurFilter = fbs::CreateBlurFilterAttribute(builder, &m_LayerfilterCommonAttribute,
    ptr->blurrinessX(), ptr->blurrinessY(), static_cast<fbs::TileMode>(ptr->tileMode()));
  auto layerFilter = fbs::CreateLayerFilter(builder,
    fbs::LayerFilterType::LayerFilterType_BlurFilter,
    fbs::LayerFilterAttribute::LayerFilterAttribute_BlurFilterAttribute,
    blurFilter.Union());
  return layerFilter.o;
}

ColorMatrixFilterSerialization::ColorMatrixFilterSerialization(std::shared_ptr<LayerFilter> layerFilter)
  :LayerFilterSerialization(layerFilter)
{
}

uint32_t ColorMatrixFilterSerialization::Serialization(flatbuffers::FlatBufferBuilder& builder) {
  LayerFilterSerialization::Serialization(builder);
  std::shared_ptr<ColorMatrixFilter> ptr = std::static_pointer_cast<ColorMatrixFilter>(m_LayerFilter);
  auto matrix = fbs::Matrix(ptr->matrix());
  auto colorMatrix = fbs::CreateColorFilterAttribute(builder,
    &m_LayerfilterCommonAttribute, &matrix);
  auto layerFilter = fbs::CreateLayerFilter(builder,
    fbs::LayerFilterType::LayerFilterType_ColorMatrixFliter,
    fbs::LayerFilterAttribute::LayerFilterAttribute_ColorFilterAttribute,
    colorMatrix.Union());
  return layerFilter.o;
}

DropShadowFilterSerialization::DropShadowFilterSerialization(std::shared_ptr<LayerFilter> layerFilter)
  :LayerFilterSerialization(layerFilter)
{
}

uint32_t DropShadowFilterSerialization::Serialization(flatbuffers::FlatBufferBuilder& builder) {
  LayerFilterSerialization::Serialization(builder);
  std::shared_ptr<DropShadowFilter> ptr = std::static_pointer_cast<DropShadowFilter>(m_LayerFilter);
  auto color = fbs::Color(ptr->color().red, ptr->color().green, ptr->color().blue, ptr->color().alpha);
  auto DropShadow = fbs::CreateDropShadowFilterAttribute(builder, &m_LayerfilterCommonAttribute,
    ptr->offsetX(), ptr->offsetY(), ptr->blurrinessX(), ptr->blurrinessY(),
    &color, ptr->dropsShadowOnly());
  auto layerFilter = fbs::CreateLayerFilter(builder,
    fbs::LayerFilterType::LayerFilterType_DropShadowFilter,
    fbs::LayerFilterAttribute::LayerFilterAttribute_DropShadowFilterAttribute,
    DropShadow.Union());
  return layerFilter.o;
}

InnerShadowFilterSerialization::InnerShadowFilterSerialization(std::shared_ptr<LayerFilter> layerFilter)
  :LayerFilterSerialization(layerFilter)
{
}

uint32_t InnerShadowFilterSerialization::Serialization(flatbuffers::FlatBufferBuilder& builder) {
  LayerFilterSerialization::Serialization(builder);
  std::shared_ptr<InnerShadowFilter> ptr = std::static_pointer_cast<InnerShadowFilter>(m_LayerFilter);
  auto color = fbs::Color(ptr->color().red, ptr->color().green, ptr->color().blue, ptr->color().alpha);
  auto InnerShadow = fbs::CreateInnerShadowFilterAttribute(builder, &m_LayerfilterCommonAttribute,
    ptr->offsetX(), ptr->offsetY(), ptr->blurrinessX(), ptr->blurrinessY(),
    &color, ptr->innerShadowOnly());
  auto layerFilter = fbs::CreateLayerFilter(builder,
    fbs::LayerFilterType::LayerFilterType_InnerShadowFilter,
    fbs::LayerFilterAttribute::LayerFilterAttribute_InnerShadowFilterAttribute,
    InnerShadow.Union());
  return layerFilter.o;
}
}
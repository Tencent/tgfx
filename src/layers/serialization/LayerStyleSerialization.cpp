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

#include "LayerStyleSerialization.h"
#include <tgfx/layers/layerstyles/DropShadowStyle.h>
#include <tgfx/layers/layerstyles/InnerShadowStyle.h>

#include "core/utils/Log.h"
#include "tgfx/layers/layerstyles/LayerStyle.h"
#include "tgfx/layers/layerstyles/BackgroundBlurStyle.h"

namespace tgfx {

std::shared_ptr<LayerStyleSerialization> LayerStyleSerialization::GetSerialization(
    std::shared_ptr<LayerStyle> layerStyle) {
  auto type = layerStyle->Type();
  switch (type)
  {
    case LayerStyleType::BackgroundBlur:
      return std::make_shared<BackGroundBlurStyleSerialization>(layerStyle);
    case LayerStyleType::DropShadow:
      return std::make_shared<DropShadowStyleSerialization>(layerStyle);
    case LayerStyleType::InnerShadow:
      return std::make_shared<InnerShadowStyleSerialization>(layerStyle);
    default:
      LOGE("Unknown layerstyle type!");
      return nullptr;
  }
}

LayerStyleSerialization::LayerStyleSerialization(std::shared_ptr<LayerStyle> layerStyle)
  :m_LayerStyle(layerStyle)
{
}

uint32_t LayerStyleSerialization::Serialization(flatbuffers::FlatBufferBuilder& builder) {
  (void)builder;
  m_CommonAttribute = tgfx::fbs::LayerStyleCommonAttribute(
    static_cast<tgfx::fbs::LayerStyleType>(m_LayerStyle->Type()),
    static_cast<tgfx::fbs::BlendMode>(m_LayerStyle->blendMode()),
    static_cast<tgfx::fbs::LayerStylePosition>(m_LayerStyle->position()),
    static_cast<tgfx::fbs::LayerStyleExtraSourceType>(m_LayerStyle->extraSourceType()));

  return uint32_t(-1);
}

BackGroundBlurStyleSerialization::BackGroundBlurStyleSerialization(
    std::shared_ptr<LayerStyle> layerStyle)
      :LayerStyleSerialization(layerStyle)
{
}

uint32_t BackGroundBlurStyleSerialization::Serialization(flatbuffers::FlatBufferBuilder& builder) {
  LayerStyleSerialization::Serialization(builder);
  std::shared_ptr<BackgroundBlurStyle> ptr = std::static_pointer_cast<BackgroundBlurStyle>(m_LayerStyle);
  auto backGroundBlur = tgfx::fbs::CreateBackGroundBlurStyleAttribute(
    builder, &m_CommonAttribute, ptr->blurrinessX(),
    ptr->blurrinessY(), static_cast<tgfx::fbs::TileMode>(ptr->tileMode()));
  auto layerStyle = fbs::CreateLayerStyle(builder,
    fbs::LayerStyleType::LayerStyleType_BackgroundBlur,
    fbs::LayerStyleAttribute::LayerStyleAttribute_BackGroundBlurStyleAttribute,
    backGroundBlur.Union());
  return layerStyle.o;
}

DropShadowStyleSerialization::DropShadowStyleSerialization(std::shared_ptr<LayerStyle> layerStyle)
  :LayerStyleSerialization(layerStyle)
{
}

uint32_t DropShadowStyleSerialization::Serialization(flatbuffers::FlatBufferBuilder& builder) {
  LayerStyleSerialization::Serialization(builder);
  std::shared_ptr<DropShadowStyle> ptr = std::static_pointer_cast<DropShadowStyle>(m_LayerStyle);
  auto color = tgfx::fbs::Color(ptr->color().red, ptr->color().green, ptr->color().blue,
    ptr->color().alpha);
  auto DropShadow = tgfx::fbs::CreateDropShadowStyleAttribute(builder,
    &m_CommonAttribute, ptr->offsetX(),
    ptr->offsetY(), ptr->blurrinessX(), ptr->blurrinessY(), &color,
    ptr->showBehindLayer());
  auto layerStyle = fbs::CreateLayerStyle(builder,
    fbs::LayerStyleType::LayerStyleType_DropShadow,
    fbs::LayerStyleAttribute::LayerStyleAttribute_DropShadowStyleAttribute,
    DropShadow.Union());
  return layerStyle.o;
}

InnerShadowStyleSerialization::InnerShadowStyleSerialization(std::shared_ptr<LayerStyle> layerStyle)
  :LayerStyleSerialization(layerStyle)
{
}

uint32_t InnerShadowStyleSerialization::Serialization(flatbuffers::FlatBufferBuilder& builder) {
  LayerStyleSerialization::Serialization(builder);
  std::shared_ptr<InnerShadowStyle> ptr = std::static_pointer_cast<InnerShadowStyle>(m_LayerStyle);
  auto color = tgfx::fbs::Color(ptr->color().red, ptr->color().green, ptr->color().blue,
  ptr->color().alpha);
  auto InnerShadow = tgfx::fbs::CreateInnerShadowStyleAttribute(builder, &m_CommonAttribute,
    ptr->offsetX(), ptr->offsetY(), ptr->blurrinessX(),
    ptr->blurrinessY(), &color);
  auto layerStyle = fbs::CreateLayerStyle(builder,
    fbs::LayerStyleType::LayerStyleType_InnerShadow,
    fbs::LayerStyleAttribute::LayerStyleAttribute_InnerShadowStyleAttribute,
    InnerShadow.Union());
  return layerStyle.o;
}
}
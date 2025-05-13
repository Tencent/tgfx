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

#include "LayerStyleSerialization.h"
#include <tgfx/layers/filters/DropShadowFilter.h>
#include <tgfx/layers/layerstyles/DropShadowStyle.h>
#include <tgfx/layers/layerstyles/InnerShadowStyle.h>
#include "core/utils/Log.h"
#include "tgfx/layers/layerstyles/BackgroundBlurStyle.h"
#include "tgfx/layers/layerstyles/LayerStyle.h"

namespace tgfx {
std::shared_ptr<Data> LayerStyleSerialization::Serialize(LayerStyle* layerStyle) {
  DEBUG_ASSERT(layerStyle != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  auto type = layerStyle->Type();
  switch (type) {
    case LayerStyleType::LayerStyle:
      SerializeBasicLayerStyleImpl(fbb, layerStyle);
      break;
    case LayerStyleType::BackgroundBlur:
      SerializeBackGroundBlurStyleImpl(fbb, layerStyle);
      break;
    case LayerStyleType::DropShadow:
      SerializeDropShadowStyleImpl(fbb, layerStyle);
      break;
    case LayerStyleType::InnerShadow:
      SerializeInnerShadowStyleImpl(fbb, layerStyle);
      break;
  }
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}
void LayerStyleSerialization::SerializeBasicLayerStyleImpl(flexbuffers::Builder& fbb,
                                                           LayerStyle* layerStyle) {
  SerializeUtils::SetFlexBufferMap(fbb, "type",
                                   SerializeUtils::LayerStyleTypeToString(layerStyle->Type()));
  SerializeUtils::SetFlexBufferMap(fbb, "blendMode",
                                   SerializeUtils::BlendModeToString(layerStyle->blendMode()));
  SerializeUtils::SetFlexBufferMap(
      fbb, "position", SerializeUtils::LayerStylePositionToString(layerStyle->position()));
  SerializeUtils::SetFlexBufferMap(
      fbb, "extraSourceType",
      SerializeUtils::LayerStyleExtraSourceTypeToString(layerStyle->extraSourceType()));
}
void LayerStyleSerialization::SerializeBackGroundBlurStyleImpl(flexbuffers::Builder& fbb,
                                                               LayerStyle* layerStyle) {
  SerializeBasicLayerStyleImpl(fbb, layerStyle);
  BackgroundBlurStyle* backgroundBlurStyle = static_cast<BackgroundBlurStyle*>(layerStyle);
  SerializeUtils::SetFlexBufferMap(fbb, "blurrinessX", backgroundBlurStyle->blurrinessX());
  SerializeUtils::SetFlexBufferMap(fbb, "blurrinessY", backgroundBlurStyle->blurrinessY());
  SerializeUtils::SetFlexBufferMap(
      fbb, "tileMode", SerializeUtils::TileModeToString(backgroundBlurStyle->tileMode()));
}
void LayerStyleSerialization::SerializeDropShadowStyleImpl(flexbuffers::Builder& fbb,
                                                           LayerStyle* layerStyle) {
  SerializeBasicLayerStyleImpl(fbb, layerStyle);
  DropShadowStyle* dropShadowStyle = static_cast<DropShadowStyle*>(layerStyle);
  SerializeUtils::SetFlexBufferMap(fbb, "offsetX", dropShadowStyle->offsetX());
  SerializeUtils::SetFlexBufferMap(fbb, "offsetY", dropShadowStyle->offsetY());
  SerializeUtils::SetFlexBufferMap(fbb, "blurrinessX", dropShadowStyle->blurrinessX());
  SerializeUtils::SetFlexBufferMap(fbb, "blurrinessY", dropShadowStyle->blurrinessY());
  SerializeUtils::SetFlexBufferMap(fbb, "color", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "showBehindLayer", dropShadowStyle->showBehindLayer());
}
void LayerStyleSerialization::SerializeInnerShadowStyleImpl(flexbuffers::Builder& fbb,
                                                            LayerStyle* layerStyle) {
  SerializeBasicLayerStyleImpl(fbb, layerStyle);
  InnerShadowStyle* innerShadowStyle = static_cast<InnerShadowStyle*>(layerStyle);
  SerializeUtils::SetFlexBufferMap(fbb, "offsetX", innerShadowStyle->offsetX());
  SerializeUtils::SetFlexBufferMap(fbb, "offsetY", innerShadowStyle->offsetY());
  SerializeUtils::SetFlexBufferMap(fbb, "blurrinessX", innerShadowStyle->blurrinessX());
  SerializeUtils::SetFlexBufferMap(fbb, "blurrinessY", innerShadowStyle->blurrinessY());
  SerializeUtils::SetFlexBufferMap(fbb, "color", "", false, true);
}
}  // namespace tgfx
#endif
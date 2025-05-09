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
std::shared_ptr<Data> LayerStyleSerialization::serializeLayerStyle(LayerStyle* layerStyle) {
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::serializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  auto type = layerStyle->Type();
  switch (type) {
    case LayerStyleType::LayerStyle:
      serializeBasicLayerStyleImpl(fbb, layerStyle);
      break;
    case LayerStyleType::BackgroundBlur:
      serializeBackGroundBlurStyleImpl(fbb, layerStyle);
      break;
    case LayerStyleType::DropShadow:
      serializeDropShadowStyleImpl(fbb, layerStyle);
      break;
    case LayerStyleType::InnerShadow:
      serializeInnerShadowStyleImpl(fbb, layerStyle);
      break;
  }
  SerializeUtils::serializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}
void LayerStyleSerialization::serializeBasicLayerStyleImpl(flexbuffers::Builder& fbb,
                                                           LayerStyle* layerStyle) {
  SerializeUtils::setFlexBufferMap(fbb, "BlendMode",
                                   SerializeUtils::blendModeToString(layerStyle->_blendMode));
}
void LayerStyleSerialization::serializeBackGroundBlurStyleImpl(flexbuffers::Builder& fbb,
                                                               LayerStyle* layerStyle) {
  serializeBasicLayerStyleImpl(fbb, layerStyle);
  BackgroundBlurStyle* backgroundBlurStyle = static_cast<BackgroundBlurStyle*>(layerStyle);
  SerializeUtils::setFlexBufferMap(fbb, "BlurrinessX", backgroundBlurStyle->_blurrinessX);
  SerializeUtils::setFlexBufferMap(fbb, "BlurrinessY", backgroundBlurStyle->_blurrinessY);
  SerializeUtils::setFlexBufferMap(
      fbb, "TileMode", SerializeUtils::tileModeToString(backgroundBlurStyle->_tileMode));
}
void LayerStyleSerialization::serializeDropShadowStyleImpl(flexbuffers::Builder& fbb,
                                                           LayerStyle* layerStyle) {
  serializeBasicLayerStyleImpl(fbb, layerStyle);
  DropShadowStyle* dropShadowStyle = static_cast<DropShadowStyle*>(layerStyle);
  SerializeUtils::setFlexBufferMap(fbb, "OffsetX", dropShadowStyle->_offsetX);
  SerializeUtils::setFlexBufferMap(fbb, "OffsetY", dropShadowStyle->_offsetY);
  SerializeUtils::setFlexBufferMap(fbb, "BlurrinessX", dropShadowStyle->_blurrinessX);
  SerializeUtils::setFlexBufferMap(fbb, "BlurrinessY", dropShadowStyle->_blurrinessY);
  SerializeUtils::setFlexBufferMap(fbb, "Color", "", false, true);
  SerializeUtils::setFlexBufferMap(fbb, "ShowBehindLayer", dropShadowStyle->_showBehindLayer);
  SerializeUtils::setFlexBufferMap(fbb, "CurrentScale", dropShadowStyle->currentScale);
  SerializeUtils::setFlexBufferMap(fbb, "ShadowFilter",
                                   reinterpret_cast<uint64_t>(dropShadowStyle->shadowFilter.get()),
                                   true, dropShadowStyle->shadowFilter != nullptr);
}
void LayerStyleSerialization::serializeInnerShadowStyleImpl(flexbuffers::Builder& fbb,
                                                            LayerStyle* layerStyle) {
  serializeBasicLayerStyleImpl(fbb, layerStyle);
  InnerShadowStyle* innerShadowStyle = static_cast<InnerShadowStyle*>(layerStyle);
  SerializeUtils::setFlexBufferMap(fbb, "OffsetX", innerShadowStyle->_offsetX);
  SerializeUtils::setFlexBufferMap(fbb, "OffsetY", innerShadowStyle->_offsetY);
  SerializeUtils::setFlexBufferMap(fbb, "BlurrinessX", innerShadowStyle->_blurrinessX);
  SerializeUtils::setFlexBufferMap(fbb, "BlurrinessY", innerShadowStyle->_blurrinessY);
  SerializeUtils::setFlexBufferMap(fbb, "Color", "", false, true);
  SerializeUtils::setFlexBufferMap(fbb, "ShadowFilter",
                                   reinterpret_cast<uint64_t>(innerShadowStyle->shadowFilter.get()),
                                   true, innerShadowStyle->shadowFilter != nullptr);
  SerializeUtils::setFlexBufferMap(fbb, "CurrentScale", innerShadowStyle->currentScale);
}
}  // namespace tgfx
#endif
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

#include "LayerFilterSerialization.h"
#include <tgfx/layers/filters/BlendFilter.h>
#include <tgfx/layers/filters/BlurFilter.h>
#include <tgfx/layers/filters/ColorMatrixFilter.h>
#include <tgfx/layers/filters/DropShadowFilter.h>
#include <tgfx/layers/filters/InnerShadowFilter.h>
#include "core/utils/Log.h"
#include "tgfx/layers/filters/LayerFilter.h"

namespace tgfx {

std::shared_ptr<Data> LayerFilterSerialization::Serialize(LayerFilter* layerFilter) {
  DEBUG_ASSERT(layerFilter != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  auto type = Types::Get(layerFilter);
  switch (type) {
    case Types::LayerFilterType::LayerFilter:
      SerializeBasicLayerFilterImpl(fbb, layerFilter);
      break;
    case Types::LayerFilterType::BlendFilter:
      SerializeBlendFilterImpl(fbb, layerFilter);
      break;
    case Types::LayerFilterType::BlurFilter:
      SerializeBlurFilterImpl(fbb, layerFilter);
      break;
    case Types::LayerFilterType::ColorMatrixFliter:
      SerializeColorMatrixFilterImpl(fbb, layerFilter);
      break;
    case Types::LayerFilterType::DropShadowFilter:
      SerializeDropShadowFilterImpl(fbb, layerFilter);
      break;
    case Types::LayerFilterType::InnerShadowFilter:
      SerializeInnerShadowFilterImpl(fbb, layerFilter);
      break;
  }
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}
void LayerFilterSerialization::SerializeBasicLayerFilterImpl(flexbuffers::Builder& fbb,
                                                             LayerFilter* layerFilter) {
  SerializeUtils::SetFlexBufferMap(
      fbb, "Type", SerializeUtils::LayerFilterTypeToString(Types::Get(layerFilter)));
}
void LayerFilterSerialization::SerializeBlendFilterImpl(flexbuffers::Builder& fbb,
                                                        LayerFilter* layerFilter) {
  SerializeBasicLayerFilterImpl(fbb, layerFilter);
  BlendFilter* blendFilter = static_cast<BlendFilter*>(layerFilter);
  SerializeUtils::SetFlexBufferMap(fbb, "Color", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "BlendMode",
                                   SerializeUtils::BlendModeToString(blendFilter->blendMode()));
}
void LayerFilterSerialization::SerializeBlurFilterImpl(flexbuffers::Builder& fbb,
                                                       LayerFilter* layerFilter) {
  SerializeBasicLayerFilterImpl(fbb, layerFilter);
  BlurFilter* blurFilter = static_cast<BlurFilter*>(layerFilter);
  SerializeUtils::SetFlexBufferMap(fbb, "BlurrinessX", blurFilter->blurrinessX());
  SerializeUtils::SetFlexBufferMap(fbb, "BlurrinessY", blurFilter->blurrinessY());
  SerializeUtils::SetFlexBufferMap(fbb, "TileMode",
                                   SerializeUtils::TileModeToString(blurFilter->tileMode()));
}
void LayerFilterSerialization::SerializeColorMatrixFilterImpl(flexbuffers::Builder& fbb,
                                                              LayerFilter* layerFilter) {
  SerializeBasicLayerFilterImpl(fbb, layerFilter);
  //ColorMatrixFilter* colorMatrixFilter = static_cast<ColorMatrixFilter*>(layerFilter);
  SerializeUtils::SetFlexBufferMap(fbb, "Matrix", "", false, true);
}
void LayerFilterSerialization::SerializeDropShadowFilterImpl(flexbuffers::Builder& fbb,
                                                             LayerFilter* layerFilter) {
  SerializeBasicLayerFilterImpl(fbb, layerFilter);
  DropShadowFilter* dropShadowFilter = static_cast<DropShadowFilter*>(layerFilter);
  SerializeUtils::SetFlexBufferMap(fbb, "OffsetX", dropShadowFilter->offsetX());
  SerializeUtils::SetFlexBufferMap(fbb, "OffsetY", dropShadowFilter->offsetY());
  SerializeUtils::SetFlexBufferMap(fbb, "BlurrinessX", dropShadowFilter->blurrinessX());
  SerializeUtils::SetFlexBufferMap(fbb, "BlurrinessY", dropShadowFilter->blurrinessY());
  SerializeUtils::SetFlexBufferMap(fbb, "Color", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "DropShadowOnly", dropShadowFilter->dropsShadowOnly());
}
void LayerFilterSerialization::SerializeInnerShadowFilterImpl(flexbuffers::Builder& fbb,
                                                              LayerFilter* layerFilter) {
  SerializeBasicLayerFilterImpl(fbb, layerFilter);
  InnerShadowFilter* innerShadowFilter = static_cast<InnerShadowFilter*>(layerFilter);
  SerializeUtils::SetFlexBufferMap(fbb, "OffsetX", innerShadowFilter->offsetX());
  SerializeUtils::SetFlexBufferMap(fbb, "OffsetY", innerShadowFilter->offsetY());
  SerializeUtils::SetFlexBufferMap(fbb, "BlurrinessX", innerShadowFilter->blurrinessX());
  SerializeUtils::SetFlexBufferMap(fbb, "BlurrinessY", innerShadowFilter->blurrinessY());
  SerializeUtils::SetFlexBufferMap(fbb, "Color", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "InnerShadowOnly", innerShadowFilter->innerShadowOnly());
}
}  // namespace tgfx
#endif
/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

std::shared_ptr<Data> LayerFilterSerialization::Serialize(const LayerFilter* layerFilter,
                                                          SerializeUtils::Map* map) {
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
      SerializeBlendFilterImpl(fbb, layerFilter, map);
      break;
    case Types::LayerFilterType::BlurFilter:
      SerializeBlurFilterImpl(fbb, layerFilter);
      break;
    case Types::LayerFilterType::ColorMatrixFilter:
      SerializeColorMatrixFilterImpl(fbb, layerFilter, map);
      break;
    case Types::LayerFilterType::DropShadowFilter:
      SerializeDropShadowFilterImpl(fbb, layerFilter, map);
      break;
    case Types::LayerFilterType::InnerShadowFilter:
      SerializeInnerShadowFilterImpl(fbb, layerFilter, map);
      break;
  }
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void LayerFilterSerialization::SerializeBasicLayerFilterImpl(flexbuffers::Builder& fbb,
                                                             const LayerFilter* layerFilter) {
  SerializeUtils::SetFlexBufferMap(
      fbb, "Type", SerializeUtils::LayerFilterTypeToString(Types::Get(layerFilter)));
}

void LayerFilterSerialization::SerializeBlendFilterImpl(flexbuffers::Builder& fbb,
                                                        const LayerFilter* layerFilter,
                                                        SerializeUtils::Map* map) {
  SerializeBasicLayerFilterImpl(fbb, layerFilter);
  const BlendFilter* blendFilter = static_cast<const BlendFilter*>(layerFilter);
  auto colorID = SerializeUtils::GetObjID();
  auto color = blendFilter->color();
  SerializeUtils::SetFlexBufferMap(fbb, "color", "", false, true, colorID);
  SerializeUtils::FillMap(color, colorID, map);
  SerializeUtils::SetFlexBufferMap(fbb, "blendMode",
                                   SerializeUtils::BlendModeToString(blendFilter->blendMode()));
}

void LayerFilterSerialization::SerializeBlurFilterImpl(flexbuffers::Builder& fbb,
                                                       const LayerFilter* layerFilter) {
  SerializeBasicLayerFilterImpl(fbb, layerFilter);
  const BlurFilter* blurFilter = static_cast<const BlurFilter*>(layerFilter);
  SerializeUtils::SetFlexBufferMap(fbb, "blurrinessX", blurFilter->blurrinessX());
  SerializeUtils::SetFlexBufferMap(fbb, "blurrinessY", blurFilter->blurrinessY());
  SerializeUtils::SetFlexBufferMap(fbb, "tileMode",
                                   SerializeUtils::TileModeToString(blurFilter->tileMode()));
}

void LayerFilterSerialization::SerializeColorMatrixFilterImpl(flexbuffers::Builder& fbb,
                                                              const LayerFilter* layerFilter,
                                                              SerializeUtils::Map* map) {
  SerializeBasicLayerFilterImpl(fbb, layerFilter);
  const ColorMatrixFilter* colorMatrixFilter = static_cast<const ColorMatrixFilter*>(layerFilter);

  auto matrixID = SerializeUtils::GetObjID();
  auto matrix = colorMatrixFilter->matrix();
  SerializeUtils::SetFlexBufferMap(fbb, "matrix", "", false, true, matrixID);
  SerializeUtils::FillMap(matrix, matrixID, map);
}

void LayerFilterSerialization::SerializeDropShadowFilterImpl(flexbuffers::Builder& fbb,
                                                             const LayerFilter* layerFilter,
                                                             SerializeUtils::Map* map) {
  SerializeBasicLayerFilterImpl(fbb, layerFilter);
  const DropShadowFilter* dropShadowFilter = static_cast<const DropShadowFilter*>(layerFilter);
  SerializeUtils::SetFlexBufferMap(fbb, "offsetX", dropShadowFilter->offsetX());
  SerializeUtils::SetFlexBufferMap(fbb, "offsetY", dropShadowFilter->offsetY());
  SerializeUtils::SetFlexBufferMap(fbb, "blurrinessX", dropShadowFilter->blurrinessX());
  SerializeUtils::SetFlexBufferMap(fbb, "blurrinessY", dropShadowFilter->blurrinessY());

  auto colorID = SerializeUtils::GetObjID();
  auto color = dropShadowFilter->color();
  SerializeUtils::SetFlexBufferMap(fbb, "color", "", false, true, colorID);
  SerializeUtils::FillMap(color, colorID, map);

  SerializeUtils::SetFlexBufferMap(fbb, "dropShadowOnly", dropShadowFilter->dropsShadowOnly());
}

void LayerFilterSerialization::SerializeInnerShadowFilterImpl(flexbuffers::Builder& fbb,
                                                              const LayerFilter* layerFilter,
                                                              SerializeUtils::Map* map) {
  SerializeBasicLayerFilterImpl(fbb, layerFilter);
  const InnerShadowFilter* innerShadowFilter = static_cast<const InnerShadowFilter*>(layerFilter);
  SerializeUtils::SetFlexBufferMap(fbb, "offsetX", innerShadowFilter->offsetX());
  SerializeUtils::SetFlexBufferMap(fbb, "offsetY", innerShadowFilter->offsetY());
  SerializeUtils::SetFlexBufferMap(fbb, "blurrinessX", innerShadowFilter->blurrinessX());
  SerializeUtils::SetFlexBufferMap(fbb, "blurrinessY", innerShadowFilter->blurrinessY());

  auto colorID = SerializeUtils::GetObjID();
  auto color = innerShadowFilter->color();
  SerializeUtils::SetFlexBufferMap(fbb, "color", "", false, true, colorID);
  SerializeUtils::FillMap(color, colorID, map);

  SerializeUtils::SetFlexBufferMap(fbb, "innerShadowOnly", innerShadowFilter->innerShadowOnly());
}
}  // namespace tgfx
#endif
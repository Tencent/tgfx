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

std::shared_ptr<Data> LayerFilterSerialization::serializeLayerFilter(LayerFilter* layerFilter) {
  DEBUG_ASSERT(layerFilter != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::serializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  auto type = layerFilter->type();
  switch (type) {
    case LayerFilter::Type::LayerFilter:
      serializeBasicLayerFilterImpl(fbb, layerFilter);
      break;
    case LayerFilter::Type::BlendFilter:
      serializeBlendFilterImpl(fbb, layerFilter);
      break;
    case LayerFilter::Type::BlurFilter:
      serializeBlurFilterImpl(fbb, layerFilter);
      break;
    case LayerFilter::Type::ColorMatrixFliter:
      serializeColorMatrixFilterImpl(fbb, layerFilter);
      break;
    case LayerFilter::Type::DropShadowFilter:
      serializeDropShadowFilterImpl(fbb, layerFilter);
      break;
    case LayerFilter::Type::InnerShadowFilter:
      serializeInnerShadowFilterImpl(fbb, layerFilter);
      break;
  }
  SerializeUtils::serializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}
void LayerFilterSerialization::serializeBasicLayerFilterImpl(flexbuffers::Builder& fbb,
                                                             LayerFilter* layerFilter) {
  SerializeUtils::setFlexBufferMap(fbb, "Dirty", layerFilter->dirty);
  SerializeUtils::setFlexBufferMap(fbb, "LastScale", layerFilter->lastScale);
  SerializeUtils::setFlexBufferMap(fbb, "ClipBounds",
                                   reinterpret_cast<uint64_t>(layerFilter->_clipBounds.get()),
                                   layerFilter->_clipBounds != nullptr);
  SerializeUtils::setFlexBufferMap(fbb, "LastFilter",
                                   reinterpret_cast<uint64_t>(layerFilter->lastFilter.get()),
                                   layerFilter->lastFilter != nullptr);
}
void LayerFilterSerialization::serializeBlendFilterImpl(flexbuffers::Builder& fbb,
                                                        LayerFilter* layerFilter) {
  serializeBasicLayerFilterImpl(fbb, layerFilter);
  BlendFilter* blendFilter = static_cast<BlendFilter*>(layerFilter);
  SerializeUtils::setFlexBufferMap(fbb, "Color", "", false, true);
  SerializeUtils::setFlexBufferMap(fbb, "BlendMode",
                                   SerializeUtils::blendModeToString(blendFilter->_blendMode));
}
void LayerFilterSerialization::serializeBlurFilterImpl(flexbuffers::Builder& fbb,
                                                       LayerFilter* layerFilter) {
  serializeBasicLayerFilterImpl(fbb, layerFilter);
  BlurFilter* blurFilter = static_cast<BlurFilter*>(layerFilter);
  SerializeUtils::setFlexBufferMap(fbb, "BlurrinessX", blurFilter->_blurrinessX);
  SerializeUtils::setFlexBufferMap(fbb, "BlurrinessY", blurFilter->_blurrinessY);
  SerializeUtils::setFlexBufferMap(fbb, "TileMode",
                                   SerializeUtils::tileModeToString(blurFilter->_tileMode));
}
void LayerFilterSerialization::serializeColorMatrixFilterImpl(flexbuffers::Builder& fbb,
                                                              LayerFilter* layerFilter) {
  serializeBasicLayerFilterImpl(fbb, layerFilter);
  //ColorMatrixFilter* colorMatrixFilter = static_cast<ColorMatrixFilter*>(layerFilter);
  SerializeUtils::setFlexBufferMap(fbb, "Matrix", "", false, true);
}
void LayerFilterSerialization::serializeDropShadowFilterImpl(flexbuffers::Builder& fbb,
                                                             LayerFilter* layerFilter) {
  serializeBasicLayerFilterImpl(fbb, layerFilter);
  DropShadowFilter* dropShadowFilter = static_cast<DropShadowFilter*>(layerFilter);
  SerializeUtils::setFlexBufferMap(fbb, "OffsetX", dropShadowFilter->_offsetX);
  SerializeUtils::setFlexBufferMap(fbb, "OffsetY", dropShadowFilter->_offsetY);
  SerializeUtils::setFlexBufferMap(fbb, "BlurrinessX", dropShadowFilter->_blurrinessX);
  SerializeUtils::setFlexBufferMap(fbb, "BlurrinessY", dropShadowFilter->_blurrinessY);
  SerializeUtils::setFlexBufferMap(fbb, "Color", "", false, true);
  SerializeUtils::setFlexBufferMap(fbb, "DropShadowOnly", dropShadowFilter->_dropsShadowOnly);
}
void LayerFilterSerialization::serializeInnerShadowFilterImpl(flexbuffers::Builder& fbb,
                                                              LayerFilter* layerFilter) {
  serializeBasicLayerFilterImpl(fbb, layerFilter);
  InnerShadowFilter* innerShadowFilter = static_cast<InnerShadowFilter*>(layerFilter);
  SerializeUtils::setFlexBufferMap(fbb, "OffsetX", innerShadowFilter->_offsetX);
  SerializeUtils::setFlexBufferMap(fbb, "OffsetY", innerShadowFilter->_offsetY);
  SerializeUtils::setFlexBufferMap(fbb, "BlurrinessX", innerShadowFilter->_blurrinessX);
  SerializeUtils::setFlexBufferMap(fbb, "BlurrinessY", innerShadowFilter->_blurrinessY);
  SerializeUtils::setFlexBufferMap(fbb, "Color", "", false, true);
  SerializeUtils::setFlexBufferMap(fbb, "InnerShadowOnly", innerShadowFilter->_innerShadowOnly);
}
}  // namespace tgfx
#endif
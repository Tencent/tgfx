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

#include "ImageFilterSerialization.h"
#include "core/filters/BlurImageFilter.h"
#include "core/filters/ColorImageFilter.h"
#include "core/filters/ComposeImageFilter.h"
#include "core/filters/DropShadowImageFilter.h"
#include "core/filters/InnerShadowImageFilter.h"
#include "core/filters/RuntimeImageFilter.h"
#include "core/utils/Types.h"

namespace tgfx {

std::shared_ptr<Data> ImageFilterSerialization::Serialize(ImageFilter* imageFilter) {
  DEBUG_ASSERT(imageFilter != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  auto type = Types::Get(imageFilter);
  switch (type) {
    case Types::ImageFilterType::Blur:
      serializeBlurImageFilter(fbb, imageFilter);
      break;
    case Types::ImageFilterType::DropShadow:
      serializeDropShadowImageFilter(fbb, imageFilter);
      break;
    case Types::ImageFilterType::InnerShadow:
      serializeInnerShadowImageFilter(fbb, imageFilter);
      break;
    case Types::ImageFilterType::Color:
      serializeColorImageFilter(fbb, imageFilter);
      break;
    case Types::ImageFilterType::Compose:
      serializeComposeImageFilter(fbb, imageFilter);
      break;
    case Types::ImageFilterType::Runtime:
      serializeRuntimeImageFilter(fbb, imageFilter);
      break;
  }
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}
void ImageFilterSerialization::serializeImageFilterImpl(flexbuffers::Builder& fbb,
                                                        ImageFilter* imageFilter) {
  SerializeUtils::SetFlexBufferMap(
      fbb, "type", SerializeUtils::ImageFilterTypeToString(Types::Get(imageFilter)));
}
void ImageFilterSerialization::serializeColorImageFilter(flexbuffers::Builder& fbb,
                                                         ImageFilter* imageFilter) {
  serializeImageFilterImpl(fbb, imageFilter);
  ColorImageFilter* colorImageFilter = static_cast<ColorImageFilter*>(imageFilter);
  SerializeUtils::SetFlexBufferMap(fbb, "filter",
                                   reinterpret_cast<uint64_t>(colorImageFilter->filter.get()), true,
                                   colorImageFilter->filter != nullptr);
}
void ImageFilterSerialization::serializeBlurImageFilter(flexbuffers::Builder& fbb,
                                                        ImageFilter* imageFilter) {
  serializeImageFilterImpl(fbb, imageFilter);
  BlurImageFilter* blurImageFilter = static_cast<BlurImageFilter*>(imageFilter);
  SerializeUtils::SetFlexBufferMap(fbb, "blurrinessX", blurImageFilter->blurrinessX);
  SerializeUtils::SetFlexBufferMap(fbb, "blurrinessY", blurImageFilter->blurrinessY);
  SerializeUtils::SetFlexBufferMap(fbb, "tileMode",
                                   SerializeUtils::TileModeToString(blurImageFilter->tileMode));
}
void ImageFilterSerialization::serializeComposeImageFilter(flexbuffers::Builder& fbb,
                                                           ImageFilter* imageFilter) {
  serializeImageFilterImpl(fbb, imageFilter);
  ComposeImageFilter* composeImageFilter = static_cast<ComposeImageFilter*>(imageFilter);
  auto filtersSize = static_cast<unsigned int>(composeImageFilter->filters.size());
  SerializeUtils::SetFlexBufferMap(fbb, "filters", filtersSize, false, filtersSize);
}
void ImageFilterSerialization::serializeDropShadowImageFilter(flexbuffers::Builder& fbb,
                                                              ImageFilter* imageFilter) {
  serializeImageFilterImpl(fbb, imageFilter);
  DropShadowImageFilter* dropShadowImageFilter = static_cast<DropShadowImageFilter*>(imageFilter);
  SerializeUtils::SetFlexBufferMap(fbb, "dx", dropShadowImageFilter->dx);
  SerializeUtils::SetFlexBufferMap(fbb, "dy", dropShadowImageFilter->dy);
  SerializeUtils::SetFlexBufferMap(
      fbb, "blurFilter", reinterpret_cast<uint64_t>(dropShadowImageFilter->blurFilter.get()), true,
      dropShadowImageFilter->blurFilter != nullptr);
  SerializeUtils::SetFlexBufferMap(fbb, "color", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "shadowOnly", dropShadowImageFilter->shadowOnly);
}
void ImageFilterSerialization::serializeInnerShadowImageFilter(flexbuffers::Builder& fbb,
                                                               ImageFilter* imageFilter) {
  serializeImageFilterImpl(fbb, imageFilter);
  InnerShadowImageFilter* innerShadowImageFilter =
      static_cast<InnerShadowImageFilter*>(imageFilter);
  SerializeUtils::SetFlexBufferMap(fbb, "dx", innerShadowImageFilter->dx);
  SerializeUtils::SetFlexBufferMap(fbb, "dy", innerShadowImageFilter->dy);
  SerializeUtils::SetFlexBufferMap(
      fbb, "blurFilter", reinterpret_cast<uint64_t>(innerShadowImageFilter->blurFilter.get()), true,
      innerShadowImageFilter->blurFilter != nullptr);
  SerializeUtils::SetFlexBufferMap(fbb, "color", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "shadowOnly", innerShadowImageFilter->shadowOnly);
}
void ImageFilterSerialization::serializeRuntimeImageFilter(flexbuffers::Builder& fbb,
                                                           ImageFilter* imageFilter) {
  serializeImageFilterImpl(fbb, imageFilter);
  RuntimeImageFilter* runtimeImageFilter = static_cast<RuntimeImageFilter*>(imageFilter);
  SerializeUtils::SetFlexBufferMap(fbb, "effect",
                                   reinterpret_cast<uint64_t>(runtimeImageFilter->effect.get()),
                                   true, runtimeImageFilter->effect != nullptr);
}
}  // namespace tgfx
#endif
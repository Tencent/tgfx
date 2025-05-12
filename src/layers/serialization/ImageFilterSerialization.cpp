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

namespace tgfx {

std::shared_ptr<Data> ImageFilterSerialization::serializeImageFilter(ImageFilter* imageFilter) {
  DEBUG_ASSERT(imageFilter != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::serializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  auto type = imageFilter->type();
  switch (type) {
    case ImageFilter::Type::Blur:
      serializeBlurImageFilter(fbb, imageFilter);
      break;
    case ImageFilter::Type::DropShadow:
      serializeDropShadowImageFilter(fbb, imageFilter);
      break;
    case ImageFilter::Type::InnerShadow:
      serializeInnerShadowImageFilter(fbb, imageFilter);
      break;
    case ImageFilter::Type::Color:
      serializeColorImageFilter(fbb, imageFilter);
      break;
    case ImageFilter::Type::Compose:
      serializeComposeImageFilter(fbb, imageFilter);
      break;
    case ImageFilter::Type::Runtime:
      serializeRuntimeImageFilter(fbb, imageFilter);
      break;
  }
  SerializeUtils::serializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}
void ImageFilterSerialization::serializeImageFilterImpl(flexbuffers::Builder& fbb,
                                                        ImageFilter* imageFilter) {
  SerializeUtils::setFlexBufferMap(fbb, "Type",
                                   SerializeUtils::imageFilterTypeToString(imageFilter->type()));
}
void ImageFilterSerialization::serializeColorImageFilter(flexbuffers::Builder& fbb,
                                                         ImageFilter* imageFilter) {
  serializeImageFilterImpl(fbb, imageFilter);
  ColorImageFilter* colorImageFilter = static_cast<ColorImageFilter*>(imageFilter);
  SerializeUtils::setFlexBufferMap(fbb, "Filter",
                                   reinterpret_cast<uint64_t>(colorImageFilter->filter.get()), true,
                                   colorImageFilter->filter != nullptr);
}
void ImageFilterSerialization::serializeBlurImageFilter(flexbuffers::Builder& fbb,
                                                        ImageFilter* imageFilter) {
  serializeImageFilterImpl(fbb, imageFilter);
  BlurImageFilter* blurImageFilter = static_cast<BlurImageFilter*>(imageFilter);
  SerializeUtils::setFlexBufferMap(fbb, "BlurrinessX", blurImageFilter->blurrinessX);
  SerializeUtils::setFlexBufferMap(fbb, "BlurrinessY", blurImageFilter->blurrinessY);
  SerializeUtils::setFlexBufferMap(fbb, "TileMode",
                                   SerializeUtils::tileModeToString(blurImageFilter->tileMode));
}
void ImageFilterSerialization::serializeComposeImageFilter(flexbuffers::Builder& fbb,
                                                           ImageFilter* imageFilter) {
  serializeImageFilterImpl(fbb, imageFilter);
  ComposeImageFilter* composeImageFilter = static_cast<ComposeImageFilter*>(imageFilter);
  auto filtersSize = static_cast<unsigned int>(composeImageFilter->filters.size());
  SerializeUtils::setFlexBufferMap(fbb, "Filters", filtersSize, false, filtersSize);
}
void ImageFilterSerialization::serializeDropShadowImageFilter(flexbuffers::Builder& fbb,
                                                              ImageFilter* imageFilter) {
  serializeImageFilterImpl(fbb, imageFilter);
  DropShadowImageFilter* dropShadowImageFilter = static_cast<DropShadowImageFilter*>(imageFilter);
  SerializeUtils::setFlexBufferMap(fbb, "DX", dropShadowImageFilter->dx);
  SerializeUtils::setFlexBufferMap(fbb, "DY", dropShadowImageFilter->dy);
  SerializeUtils::setFlexBufferMap(
      fbb, "BlurFilter", reinterpret_cast<uint64_t>(dropShadowImageFilter->blurFilter.get()), true,
      dropShadowImageFilter->blurFilter != nullptr);
  SerializeUtils::setFlexBufferMap(fbb, "Color", "", false, true);
  SerializeUtils::setFlexBufferMap(fbb, "ShadowOnly", dropShadowImageFilter->shadowOnly);
}
void ImageFilterSerialization::serializeInnerShadowImageFilter(flexbuffers::Builder& fbb,
                                                               ImageFilter* imageFilter) {
  serializeImageFilterImpl(fbb, imageFilter);
  InnerShadowImageFilter* innerShadowImageFilter =
      static_cast<InnerShadowImageFilter*>(imageFilter);
  SerializeUtils::setFlexBufferMap(fbb, "DX", innerShadowImageFilter->dx);
  SerializeUtils::setFlexBufferMap(fbb, "DY", innerShadowImageFilter->dy);
  SerializeUtils::setFlexBufferMap(
      fbb, "BlurFilter", reinterpret_cast<uint64_t>(innerShadowImageFilter->blurFilter.get()), true,
      innerShadowImageFilter->blurFilter != nullptr);
  SerializeUtils::setFlexBufferMap(fbb, "Color", "", false, true);
  SerializeUtils::setFlexBufferMap(fbb, "ShadowOnly", innerShadowImageFilter->shadowOnly);
}
void ImageFilterSerialization::serializeRuntimeImageFilter(flexbuffers::Builder& fbb,
                                                           ImageFilter* imageFilter) {
  serializeImageFilterImpl(fbb, imageFilter);
  RuntimeImageFilter* runtimeImageFilter = static_cast<RuntimeImageFilter*>(imageFilter);
  SerializeUtils::setFlexBufferMap(fbb, "Effect",
                                   reinterpret_cast<uint64_t>(runtimeImageFilter->effect.get()),
                                   true, runtimeImageFilter->effect != nullptr);
}
}  // namespace tgfx
#endif
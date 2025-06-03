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

std::shared_ptr<Data> ImageFilterSerialization::Serialize(const ImageFilter* imageFilter,
                                                          SerializeUtils::ComplexObjSerMap* map) {
  DEBUG_ASSERT(imageFilter != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerSubAttribute", startMap, contentMap);
  auto type = Types::Get(imageFilter);
  switch (type) {
    case Types::ImageFilterType::Blur:
      serializeBlurImageFilter(fbb, imageFilter);
      break;
    case Types::ImageFilterType::DropShadow:
      serializeDropShadowImageFilter(fbb, imageFilter, map);
      break;
    case Types::ImageFilterType::InnerShadow:
      serializeInnerShadowImageFilter(fbb, imageFilter, map);
      break;
    case Types::ImageFilterType::Color:
      serializeColorImageFilter(fbb, imageFilter, map);
      break;
    case Types::ImageFilterType::Compose:
      serializeComposeImageFilter(fbb, imageFilter, map);
      break;
    case Types::ImageFilterType::Runtime:
      serializeRuntimeImageFilter(fbb, imageFilter, map);
      break;
  }
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void ImageFilterSerialization::serializeImageFilterImpl(flexbuffers::Builder& fbb,
                                                        const ImageFilter* imageFilter) {
  SerializeUtils::SetFlexBufferMap(
      fbb, "type", SerializeUtils::ImageFilterTypeToString(Types::Get(imageFilter)));
}

void ImageFilterSerialization::serializeColorImageFilter(flexbuffers::Builder& fbb,
                                                         const ImageFilter* imageFilter,
                                                         SerializeUtils::ComplexObjSerMap* map) {
  serializeImageFilterImpl(fbb, imageFilter);
  const ColorImageFilter* colorImageFilter = static_cast<const ColorImageFilter*>(imageFilter);

  auto filterID = SerializeUtils::GetObjID();
  auto filter = colorImageFilter->filter;
  SerializeUtils::SetFlexBufferMap(fbb, "filter", reinterpret_cast<uint64_t>(filter.get()), true,
                                   filter != nullptr, filterID);
  SerializeUtils::FillComplexObjSerMap(filter, filterID, map);
}

void ImageFilterSerialization::serializeBlurImageFilter(flexbuffers::Builder& fbb,
                                                        const ImageFilter* imageFilter) {
  serializeImageFilterImpl(fbb, imageFilter);
  const BlurImageFilter* blurImageFilter = static_cast<const BlurImageFilter*>(imageFilter);
  SerializeUtils::SetFlexBufferMap(fbb, "blurrinessX", blurImageFilter->blurrinessX);
  SerializeUtils::SetFlexBufferMap(fbb, "blurrinessY", blurImageFilter->blurrinessY);
  SerializeUtils::SetFlexBufferMap(fbb, "tileMode",
                                   SerializeUtils::TileModeToString(blurImageFilter->tileMode));
}

void ImageFilterSerialization::serializeComposeImageFilter(flexbuffers::Builder& fbb,
                                                           const ImageFilter* imageFilter,
                                                           SerializeUtils::ComplexObjSerMap* map) {
  serializeImageFilterImpl(fbb, imageFilter);
  const ComposeImageFilter* composeImageFilter =
      static_cast<const ComposeImageFilter*>(imageFilter);

  auto filtersID = SerializeUtils::GetObjID();
  auto filters = composeImageFilter->filters;
  auto filtersSize = static_cast<unsigned int>(filters.size());
  SerializeUtils::SetFlexBufferMap(fbb, "filters", filtersSize, false, filtersSize, filtersID);
  SerializeUtils::FillComplexObjSerMap(filters, filtersID, map);
}

void ImageFilterSerialization::serializeDropShadowImageFilter(flexbuffers::Builder& fbb,
                                                              const ImageFilter* imageFilter,
                                                              SerializeUtils::ComplexObjSerMap* map) {
  serializeImageFilterImpl(fbb, imageFilter);
  const DropShadowImageFilter* dropShadowImageFilter =
      static_cast<const DropShadowImageFilter*>(imageFilter);
  SerializeUtils::SetFlexBufferMap(fbb, "dx", dropShadowImageFilter->dx);
  SerializeUtils::SetFlexBufferMap(fbb, "dy", dropShadowImageFilter->dy);

  auto blurFilterID = SerializeUtils::GetObjID();
  auto blurFilter = dropShadowImageFilter->blurFilter;
  SerializeUtils::SetFlexBufferMap(fbb, "blurFilter", reinterpret_cast<uint64_t>(blurFilter.get()),
                                   true, blurFilter != nullptr, blurFilterID);
  SerializeUtils::FillComplexObjSerMap(blurFilter, blurFilterID, map);

  auto colorID = SerializeUtils::GetObjID();
  auto color = dropShadowImageFilter->color;
  SerializeUtils::SetFlexBufferMap(fbb, "color", "", false, true, colorID);
  SerializeUtils::FillComplexObjSerMap(color, colorID, map);

  SerializeUtils::SetFlexBufferMap(fbb, "shadowOnly", dropShadowImageFilter->shadowOnly);
}

void ImageFilterSerialization::serializeInnerShadowImageFilter(flexbuffers::Builder& fbb,
                                                               const ImageFilter* imageFilter,
                                                               SerializeUtils::ComplexObjSerMap* map) {
  serializeImageFilterImpl(fbb, imageFilter);
  const InnerShadowImageFilter* innerShadowImageFilter =
      static_cast<const InnerShadowImageFilter*>(imageFilter);
  SerializeUtils::SetFlexBufferMap(fbb, "dx", innerShadowImageFilter->dx);
  SerializeUtils::SetFlexBufferMap(fbb, "dy", innerShadowImageFilter->dy);

  auto blurFilterID = SerializeUtils::GetObjID();
  auto blurFilter = innerShadowImageFilter->blurFilter;
  SerializeUtils::SetFlexBufferMap(fbb, "blurFilter", reinterpret_cast<uint64_t>(blurFilter.get()),
                                   true, blurFilter != nullptr, blurFilterID);
  SerializeUtils::FillComplexObjSerMap(blurFilter, blurFilterID, map);

  auto colorID = SerializeUtils::GetObjID();
  auto color = innerShadowImageFilter->color;
  SerializeUtils::SetFlexBufferMap(fbb, "color", "", false, true, colorID);
  SerializeUtils::FillComplexObjSerMap(color, colorID, map);

  SerializeUtils::SetFlexBufferMap(fbb, "shadowOnly", innerShadowImageFilter->shadowOnly);
}

void ImageFilterSerialization::serializeRuntimeImageFilter(flexbuffers::Builder& fbb,
                                                           const ImageFilter* imageFilter,
                                                           SerializeUtils::ComplexObjSerMap* map) {
  serializeImageFilterImpl(fbb, imageFilter);
  const RuntimeImageFilter* runtimeImageFilter =
      static_cast<const RuntimeImageFilter*>(imageFilter);

  auto effectID = SerializeUtils::GetObjID();
  auto effect = runtimeImageFilter->effect;
  SerializeUtils::SetFlexBufferMap(fbb, "effect", reinterpret_cast<uint64_t>(effect.get()), true,
                                   effect != nullptr, effectID);
  SerializeUtils::FillComplexObjSerMap(effect, effectID, map);
}
}  // namespace tgfx
#endif
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

#include "FontMetricsSerialization.h"

namespace tgfx {

std::shared_ptr<Data> FontMetricsSerialization::Serialize(const FontMetrics* fontMerics) {
  DEBUG_ASSERT(fontMerics != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  SerializeScalerContextImpl(fbb, fontMerics);
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void FontMetricsSerialization::SerializeScalerContextImpl(flexbuffers::Builder& fbb,
                                                          const FontMetrics* fontMetrics) {
  SerializeUtils::SetFlexBufferMap(fbb, "top", fontMetrics->top);
  SerializeUtils::SetFlexBufferMap(fbb, "ascent", fontMetrics->ascent);
  SerializeUtils::SetFlexBufferMap(fbb, "descent", fontMetrics->descent);
  SerializeUtils::SetFlexBufferMap(fbb, "bottom", fontMetrics->bottom);
  SerializeUtils::SetFlexBufferMap(fbb, "leading", fontMetrics->leading);
  SerializeUtils::SetFlexBufferMap(fbb, "xMin", fontMetrics->xMin);
  SerializeUtils::SetFlexBufferMap(fbb, "xMax", fontMetrics->xMax);
  SerializeUtils::SetFlexBufferMap(fbb, "xHeight", fontMetrics->xHeight);
  SerializeUtils::SetFlexBufferMap(fbb, "capHeight", fontMetrics->capHeight);
  SerializeUtils::SetFlexBufferMap(fbb, "underlineThickness", fontMetrics->underlineThickness);
  SerializeUtils::SetFlexBufferMap(fbb, "underlinePosition", fontMetrics->underlinePosition);
}
}  // namespace tgfx
#endif
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
#ifdef TGFX_ENABLE_PROFILING

#include "FontMetricsSerialization.h"

namespace tgfx {

std::shared_ptr<Data> FontMetricsSerialization::serializeScalerContext(FontMetrics* fontMerics) {
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::serializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  serializeScalerContext(fbb, fontMerics);
  SerializeUtils::serializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
  ;
}
void FontMetricsSerialization::serializeScalerContext(flexbuffers::Builder& fbb,
                                                      FontMetrics* fontMetrics) {
  SerializeUtils::setFlexBufferMap(fbb, "Top", fontMetrics->top);
  SerializeUtils::setFlexBufferMap(fbb, "Ascent", fontMetrics->ascent);
  SerializeUtils::setFlexBufferMap(fbb, "Descent", fontMetrics->descent);
  SerializeUtils::setFlexBufferMap(fbb, "Bottom", fontMetrics->bottom);
  SerializeUtils::setFlexBufferMap(fbb, "Leading", fontMetrics->leading);
  SerializeUtils::setFlexBufferMap(fbb, "xMin", fontMetrics->xMin);
  SerializeUtils::setFlexBufferMap(fbb, "xMax", fontMetrics->xMax);
  SerializeUtils::setFlexBufferMap(fbb, "xHeight", fontMetrics->xHeight);
  SerializeUtils::setFlexBufferMap(fbb, "CapHeight", fontMetrics->capHeight);
  SerializeUtils::setFlexBufferMap(fbb, "UnderlineThickness", fontMetrics->underlineThickness);
  SerializeUtils::setFlexBufferMap(fbb, "UnderlinePosition", fontMetrics->underlinePosition);
}
}  // namespace tgfx
#endif
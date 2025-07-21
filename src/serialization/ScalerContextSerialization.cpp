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

#include "ScalerContextSerialization.h"

namespace tgfx {

std::shared_ptr<Data> ScalerContextSerialization::Serialize(const ScalerContext* scalerContext,
                                                            SerializeUtils::Map* map) {
  DEBUG_ASSERT(scalerContext != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  SerializeScalerContextImpl(fbb, scalerContext, map);
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void ScalerContextSerialization::SerializeScalerContextImpl(flexbuffers::Builder& fbb,
                                                            const ScalerContext* scaler_context,
                                                            SerializeUtils::Map* map) {
  auto typeFace = scaler_context->getTypeface();
  auto typeFaceID = SerializeUtils::GetObjID();
  SerializeUtils::SetFlexBufferMap(fbb, "typeFace", reinterpret_cast<uint64_t>(typeFace.get()),
                                   true, typeFace != nullptr, typeFaceID);
  SerializeUtils::FillMap(typeFace, typeFaceID, map);

  SerializeUtils::SetFlexBufferMap(fbb, "size", scaler_context->getSize());

  auto fontMetricsID = SerializeUtils::GetObjID();
  auto fontMetrics = scaler_context->getFontMetrics();
  SerializeUtils::SetFlexBufferMap(fbb, "fontMetrics", "", false, true, fontMetricsID);
  SerializeUtils::FillMap(fontMetrics, fontMetricsID, map);
}
}  // namespace tgfx
#endif
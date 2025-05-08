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

#include "ScalerContextSerialization.h"

namespace tgfx {

std::shared_ptr<Data> ScalerContextSerialization::serializeScalerContext(
    ScalerContext* scalerContext) {
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::serializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  serializeScalerContextImpl(fbb, scalerContext);
  SerializeUtils::serializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}
void ScalerContextSerialization::serializeScalerContextImpl(flexbuffers::Builder& fbb,
                                                            ScalerContext* scaler_context) {
  SerializeUtils::setFlexBufferMap(fbb, "TypeFace",
                                   reinterpret_cast<uint64_t>(scaler_context->typeface.get()), true,
                                   scaler_context->typeface != nullptr);
  SerializeUtils::setFlexBufferMap(fbb, "TextSize", scaler_context->textSize);
  SerializeUtils::setFlexBufferMap(fbb, "FontMetrics", "", false, true);
}
}  // namespace tgfx
#endif
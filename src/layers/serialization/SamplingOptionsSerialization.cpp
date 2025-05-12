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
#include "SamplingOptionsSerialization.h"

namespace tgfx {

std::shared_ptr<Data> SamplingOptionsSerialization::serializeSamplingOptions(
    SamplingOptions* samplingOptions) {
  DEBUG_ASSERT(samplingOptions != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::serializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  serializeSamplingOptionsImpl(fbb, samplingOptions);
  SerializeUtils::serializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void SamplingOptionsSerialization::serializeSamplingOptionsImpl(flexbuffers::Builder& fbb,
                                                                SamplingOptions* samplingOptions) {
  SerializeUtils::setFlexBufferMap(fbb, "FilterMode",
                                   SerializeUtils::filterModeToString(samplingOptions->filterMode));
  SerializeUtils::setFlexBufferMap(fbb, "MipmapMode",
                                   SerializeUtils::mipmapModeToString(samplingOptions->mipmapMode));
}
}  // namespace tgfx
#endif
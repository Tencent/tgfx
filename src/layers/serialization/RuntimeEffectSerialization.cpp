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

#include "RuntimeEffectSerialization.h"

namespace tgfx {

std::shared_ptr<Data> RuntimeEffectSerialization::serializeRuntimeEffect(
    RuntimeEffect* runtimeEffet) {
  DEBUG_ASSERT(runtimeEffet != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::serializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  serializeRuntimeEffectImpl(fbb, runtimeEffet);
  SerializeUtils::serializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void RuntimeEffectSerialization::serializeRuntimeEffectImpl(flexbuffers::Builder& fbb,
                                                            RuntimeEffect* runtimeEffet) {
  auto extrInputsSize = static_cast<unsigned int>(runtimeEffet->extraInputs.size());
  SerializeUtils::setFlexBufferMap(fbb, "ExtraInputs", extrInputsSize, false, extrInputsSize);
}
}  // namespace tgfx
#endif
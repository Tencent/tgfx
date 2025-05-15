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

#include "StrokeSerialization.h"

namespace tgfx {

std::shared_ptr<Data> StrokeSerialization::Serialize(const Stroke* stroke) {
  DEBUG_ASSERT(stroke != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  SerializeStrokeImpl(fbb, stroke);
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void StrokeSerialization::SerializeStrokeImpl(flexbuffers::Builder& fbb, const Stroke* stroke) {
  SerializeUtils::SetFlexBufferMap(fbb, "width", stroke->width);
  SerializeUtils::SetFlexBufferMap(fbb, "cap", SerializeUtils::LineCapToString(stroke->cap));
  SerializeUtils::SetFlexBufferMap(fbb, "join", SerializeUtils::LineJoinToString(stroke->join));
  SerializeUtils::SetFlexBufferMap(fbb, "miterLimit", stroke->miterLimit);
}
}  // namespace tgfx
#endif
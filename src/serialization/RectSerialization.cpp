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

#include "RectSerialization.h"

namespace tgfx {

std::shared_ptr<Data> RectSerialization::Serialize(Rect* rect) {
  DEBUG_ASSERT(rect != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  SerializeRectImpl(fbb, rect);
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void RectSerialization::SerializeRectImpl(flexbuffers::Builder& fbb, Rect* rect) {
  SerializeUtils::SetFlexBufferMap(fbb, "left", rect->left);
  SerializeUtils::SetFlexBufferMap(fbb, "top", rect->top);
  SerializeUtils::SetFlexBufferMap(fbb, "right", rect->right);
  SerializeUtils::SetFlexBufferMap(fbb, "bottom", rect->bottom);
}
}  // namespace tgfx
#endif
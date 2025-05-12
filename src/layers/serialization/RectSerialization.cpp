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

std::shared_ptr<Data> RectSerialization::serializeRect(Rect* rect) {
  DEBUG_ASSERT(rect != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::serializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  serializeRectImpl(fbb, rect);
  SerializeUtils::serializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void RectSerialization::serializeRectImpl(flexbuffers::Builder& fbb, Rect* rect) {
  SerializeUtils::setFlexBufferMap(fbb, "Left", rect->left);
  SerializeUtils::setFlexBufferMap(fbb, "Top", rect->top);
  SerializeUtils::setFlexBufferMap(fbb, "Right", rect->right);
  SerializeUtils::setFlexBufferMap(fbb, "Bottom", rect->bottom);
}
}  // namespace tgfx
#endif
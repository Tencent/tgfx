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
#include "PathSerialization.h"

namespace tgfx {

std::shared_ptr<Data> PathSerialization::Serialize(const Path* path, SerializeUtils::MapRef map) {
  DEBUG_ASSERT(path != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  SerializePathImpl(fbb, path, map);
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void PathSerialization::SerializePathImpl(flexbuffers::Builder& fbb, const Path* path,
                                          SerializeUtils::MapRef map) {
  SerializeUtils::SetFlexBufferMap(fbb, "fillType",
                                   SerializeUtils::PathFillTypeToString(path->getFillType()));
  SerializeUtils::SetFlexBufferMap(fbb, "isInverseFillType", path->isInverseFillType());
  SerializeUtils::SetFlexBufferMap(fbb, "isLine", path->isLine());
  SerializeUtils::SetFlexBufferMap(fbb, "isRect", path->isRect());
  SerializeUtils::SetFlexBufferMap(fbb, "isOval", path->isOval());

  auto boundsID = SerializeUtils::GetObjID();
  auto bounds = path->getBounds();
  SerializeUtils::SetFlexBufferMap(fbb, "bounds", "", false, true, boundsID);
  SerializeUtils::FillMap(bounds, boundsID, map);

  SerializeUtils::SetFlexBufferMap(fbb, "isEmpty", path->isEmpty());
  SerializeUtils::SetFlexBufferMap(fbb, "countPoints", path->countPoints());
  SerializeUtils::SetFlexBufferMap(fbb, "countVerbs", path->countVerbs());
}
}  // namespace tgfx
#endif

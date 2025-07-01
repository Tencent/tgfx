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

#include "PictureSerialization.h"

namespace tgfx {

std::shared_ptr<Data> PictureSerialization::Serialize(const Picture* pic) {
  DEBUG_ASSERT(pic != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, inspector::LayerInspectorMsgType::LayerSubAttribute, startMap,
                                 contentMap);
  SerializePictureImpl(fbb, pic);
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void PictureSerialization::SerializePictureImpl(flexbuffers::Builder&, const Picture*) {
  // noop
}
}  // namespace tgfx

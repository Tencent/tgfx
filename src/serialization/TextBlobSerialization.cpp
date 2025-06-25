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

#include "TextBlobSerialization.h"

namespace tgfx {

std::shared_ptr<Data> TextBlobSerialization::Serialize(
    const TextBlob* textBlob, SerializeUtils::ComplexObjSerMap* map,
    SerializeUtils::RenderableObjSerMap* rosMap) {
  DEBUG_ASSERT(textBlob != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerSubAttribute", startMap, contentMap);
  SerializeTextBlobImpl(fbb, textBlob, map, rosMap);
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void TextBlobSerialization::SerializeTextBlobImpl(flexbuffers::Builder& fbb,
                                                  const TextBlob* textBlob,
                                                  SerializeUtils::ComplexObjSerMap* map,
                                                  SerializeUtils::RenderableObjSerMap* rosMap) {
  auto boundsID = SerializeUtils::GetObjID();
  auto bounds = textBlob->getBounds();
  SerializeUtils::SetFlexBufferMap(fbb, "bounds", "", false, true, boundsID);
  SerializeUtils::FillComplexObjSerMap(bounds, boundsID, map);

  Path path;
  if (textBlob->getPath(&path)) {
    auto pathID = SerializeUtils::GetObjID();
    SerializeUtils::SetFlexBufferMap(fbb, "path", "", false, true, pathID, true);
    SerializeUtils::FillComplexObjSerMap(path, pathID, map);
    SerializeUtils::FillRenderableObjSerMap(path, pathID, rosMap);
  }
}

}  // namespace tgfx
#endif
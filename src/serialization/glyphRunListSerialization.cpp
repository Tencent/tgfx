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

#include "glyphRunListSerialization.h"
#include "core/GlyphRunList.h"

namespace tgfx {

std::shared_ptr<Data> glyphRunListSerialization::Serialize(GlyphRunList* glyphRunList) {
  DEBUG_ASSERT(glyphRunList != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  SerializeglyphRunListImpl(fbb, glyphRunList);
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void glyphRunListSerialization::SerializeglyphRunListImpl(flexbuffers::Builder& fbb,
                                                          GlyphRunList* glyphRunList) {
  SerializeUtils::SetFlexBufferMap(fbb, "hasColor", glyphRunList->hasColor());
  SerializeUtils::SetFlexBufferMap(fbb, "hasOutlines", glyphRunList->hasOutlines());
  auto glyphRuns = glyphRunList->glyphRuns();
  auto glyhRunsSize = static_cast<unsigned int>(glyphRuns.size());
  SerializeUtils::SetFlexBufferMap(fbb, "glyphRuns", glyhRunsSize, false, glyhRunsSize);
}
}  // namespace tgfx
#endif
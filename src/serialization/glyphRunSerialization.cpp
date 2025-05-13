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

#include "glyphRunSerialization.h"

namespace tgfx {

std::shared_ptr<Data> glyphRunSerialization::Serialize(GlyphRun* glyphRun) {
  DEBUG_ASSERT(glyphRun != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  SerializeGlyphRunImpl(fbb, glyphRun);
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void glyphRunSerialization::SerializeGlyphRunImpl(flexbuffers::Builder& fbb, GlyphRun* glyphRun) {
  SerializeUtils::SetFlexBufferMap(fbb, "GlyphFace",
                                   reinterpret_cast<uint64_t>(glyphRun->glyphFace.get()), true,
                                   glyphRun->glyphFace != nullptr);
  auto glyphsSize = static_cast<unsigned int>(glyphRun->glyphs.size());
  SerializeUtils::SetFlexBufferMap(fbb, "Glyphs", glyphsSize, false, glyphsSize);
  auto positionsSize = static_cast<unsigned int>(glyphRun->positions.size());
  SerializeUtils::SetFlexBufferMap(fbb, "Positions", positionsSize, false, positionsSize);
}
}  // namespace tgfx
#endif

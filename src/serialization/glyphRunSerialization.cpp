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

std::shared_ptr<Data> glyphRunSerialization::Serialize(const GlyphRun* glyphRun,
                                                       SerializeUtils::Map* map) {
  DEBUG_ASSERT(glyphRun != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  SerializeGlyphRunImpl(fbb, glyphRun, map);
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void glyphRunSerialization::SerializeGlyphRunImpl(flexbuffers::Builder& fbb,
                                                  const GlyphRun* glyphRun,
                                                  SerializeUtils::Map* map) {
  auto glyphFaceID = SerializeUtils::GetObjID();
  const auto& font = glyphRun->font;
  SerializeUtils::SetFlexBufferMap(fbb, "font", reinterpret_cast<uint64_t>(&font), true,
                                   font.getTypeface() != nullptr, glyphFaceID);
  SerializeUtils::FillMap(font, glyphFaceID, map);

  auto glyphsID = SerializeUtils::GetObjID();
  auto glyphs = glyphRun->glyphs;
  auto glyphsSize = static_cast<unsigned int>(glyphs.size());
  SerializeUtils::SetFlexBufferMap(fbb, "glyphs", glyphsSize, false, glyphsSize, glyphsID);
  SerializeUtils::FillMap(glyphs, glyphsID, map);

  auto positionsID = SerializeUtils::GetObjID();
  auto positions = glyphRun->positions;
  auto positionsSize = static_cast<unsigned int>(positions.size());
  SerializeUtils::SetFlexBufferMap(fbb, "positions", positionsSize, false, positionsSize,
                                   positionsID);
  SerializeUtils::FillMap(positions, positionsID, map);
}
}  // namespace tgfx
#endif

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

#include "PathTypeface.h"
#include "PathScalerContext.h"

namespace tgfx {
PathTypeface::PathTypeface(uint32_t builderID, const std::string& fontFamily,
                           const std::string& fontStyle, const FontMetrics& metrics,
                           const VectorRecordType& glyphRecords)
    : _builderID(builderID), _fontFamily(fontFamily), _fontStyle(fontStyle), _fontMetrics(metrics),
      glyphRecords(glyphRecords) {
  initCharGlyphIDMap();
}

void PathTypeface::initCharGlyphIDMap() {
  GlyphID glyphID = 0;
  for (const auto& record : glyphRecords) {
    ++glyphID;
    unicharToGlyphIDMap[record->unichar] = glyphID;
  }
}

size_t PathTypeface::glyphsCount() const {
  return glyphRecords.size();
}

int PathTypeface::unitsPerEm() const {
  return 2048;
}

bool PathTypeface::hasColor() const {
  return false;
}

bool PathTypeface::hasOutlines() const {
  return true;
}

GlyphID PathTypeface::getGlyphID(Unichar unichar) const {
  if (auto iter = unicharToGlyphIDMap.find(unichar); iter != unicharToGlyphIDMap.end()) {
    return iter->second;
  }
  return 0;
}

std::shared_ptr<Data> PathTypeface::getBytes() const {
  // PathTypeface does not have byte data.
  return nullptr;
}

std::shared_ptr<Data> PathTypeface::copyTableData(FontTableTag) const {
  // PathTypeface does not support font tables.
  return nullptr;
}

#ifdef TGFX_USE_GLYPH_TO_UNICODE
std::vector<Unichar> PathTypeface::getGlyphToUnicodeMap() const {
  std::vector<Unichar> returnMap(glyphRecords.size(), 0);
  for (size_t i = 0; i < glyphRecords.size(); ++i) {
    returnMap[i] = glyphRecords[i]->unichar;
  }
  return returnMap;
}
#endif

std::shared_ptr<ScalerContext> PathTypeface::onCreateScalerContext(float size) const {
  return std::make_shared<PathScalerContext>(weakThis.lock(), size);
}

std::shared_ptr<PathTypefaceBuilder::GlyphRecord> PathTypeface::getGlyphRecord(
    GlyphID glyphID) const {
  if (glyphID == 0 || static_cast<size_t>(glyphID) > glyphRecords.size()) {
    return nullptr;
  }
  return glyphRecords[glyphID - 1];
}
}  // namespace tgfx

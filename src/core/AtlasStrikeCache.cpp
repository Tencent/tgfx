/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "AtlasStrikeCache.h"
#include "AtlasTypes.h"

namespace tgfx {
AtlasStrike::AtlasStrike(const BytesKey& key) : key(key) {
}

AtlasGlyph* AtlasStrike::getGlyph(GlyphID glyphID) {
  if (const auto iter = glyphMap.find(glyphID); iter != glyphMap.end()) {
    return iter->second;
  }
  auto* memory = allocator.allocate(sizeof(AtlasGlyph));
  auto* glyph = new (memory) AtlasGlyph();
  glyphMap.emplace(glyphID, glyph);
  return glyph;
}

AtlasStrikeCache::~AtlasStrikeCache() {
  releaseAll();
}

void AtlasStrikeCache::releaseAll() {
  strikes.clear();
}

std::shared_ptr<AtlasStrike> AtlasStrikeCache::findOrCreateStrike(const BytesKey& key) {
  if (const auto iter = strikes.find(key); iter != strikes.end()) {
    return iter->second;
  }
  auto strike = std::make_shared<AtlasStrike>(key);
  strikes.emplace(key, strike);
  return strike;
}

}  // namespace tgfx

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
AtlasStrike::AtlasStrike(AtlasStrikeCache* strikeCache, const BytesKey& key)
    : strikeCache(strikeCache), key(key) {

}

AtlasGlyph* AtlasStrike::getGlyph(GlyphID glyphID) {
  if (const auto iter = glyphMap.find(glyphID); iter != glyphMap.end()) {
    return iter->second;
  }

  auto* memory = allocator.allocate(sizeof(AtlasGlyph));
  auto* glyph = new (memory) AtlasGlyph();
  glyphMap.emplace(glyphID, glyph);
  memoryUsed += sizeof(AtlasGlyph);
  strikeCache->totalMemoryUsed += sizeof(AtlasGlyph);
  return glyph;
}

void AtlasStrikeCache::releaseAll() {
  strikes.clear();
  lruList.clear();
  lruMap.clear();
  totalMemoryUsed = 0;
}

std::shared_ptr<AtlasStrike> AtlasStrikeCache::findOrCreateStrike(const BytesKey& key) {
  auto strike = findStrikeOrNull(key);
  if (strike == nullptr) {
    strike = createStrike(key);
  }
  purgeIfNeeded();
  return strike;
}

void AtlasStrikeCache::markStrikeAsRecentlyUsed(const BytesKey& key) {
  auto iter = lruMap.find(key);
  if (iter == lruMap.end()) {
    return;
  }
  lruList.splice(lruList.begin(), lruList, iter->second);
}

void AtlasStrikeCache::purgeOldestStrike() {
  if (lruList.empty()) {
    return;
  }
  const auto& strike = lruList.back();
  const auto& key = strike->getKey();
  totalMemoryUsed -= strike->memoryUsed;
  strikes.erase(key);
  lruMap.erase(key);
  lruList.pop_back();
}

void AtlasStrikeCache::purgeIfNeeded() {
  if (totalMemoryUsed < MemorySizeLimit && strikes.size() < StrikeCountLimit) {
    return;
  }

  const auto bytesNeeded = totalMemoryUsed >= MemorySizeLimit ? MemorySizeLimit >> 2 : 0;
  const auto countNeeded = strikes.size() >= StrikeCountLimit ? StrikeCountLimit >> 2 : 0;
  size_t bytesFreed = 0;
  size_t countFreed = 0;
  while (!lruList.empty() && (bytesFreed < bytesNeeded || countFreed < countNeeded)) {
    const auto& strike = lruList.back();
    bytesFreed += strike->memoryUsed;
    countFreed++;
    purgeOldestStrike();
  }
}

std::shared_ptr<AtlasStrike> AtlasStrikeCache::findStrikeOrNull(const BytesKey& key) {
  if (const auto iter = strikes.find(key); iter != strikes.end()) {
    markStrikeAsRecentlyUsed(key);
    return iter->second;
  }
  return nullptr;
}

std::shared_ptr<AtlasStrike> AtlasStrikeCache::createStrike(const BytesKey& key) {
  auto strike = std::make_shared<AtlasStrike>(this, key);
  strikes.emplace(key, strike);
  lruList.push_front(strike);
  lruMap.emplace(key, lruList.begin());
  return strike;
}
}  // namespace tgfx

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

#pragma once

#include <set>
#include "core/utils/BlockAllocator.h"
#include "tgfx/core/BytesKey.h"
#include "tgfx/core/Typeface.h"

namespace tgfx {
struct AtlasGlyph;

/**
 * The AtlasStrike manages an Alloc for AtlasGlyphs. It is created by and owned by a AtlasStrikeCache.
 */
class AtlasStrike {
 public:
  explicit AtlasStrike(const BytesKey& key);

  AtlasGlyph* getGlyph(GlyphID glyphID);

  const BytesKey& getKey() const {
    return key;
  };

  /**
   * Returns true if the glyph has no visible content (e.g., space, newline).
   */
  bool isEmptyGlyph(GlyphID glyphID) const {
    return emptyGlyphs.find(glyphID) != emptyGlyphs.end();
  }

  /**
   * Marks a glyph as empty to avoid repeated lookups.
   */
  void markEmptyGlyph(GlyphID glyphID) {
    emptyGlyphs.insert(glyphID);
  }

 private:
  const BytesKey key;
  BlockAllocator allocator{512};
  std::unordered_map<GlyphID, AtlasGlyph*> glyphMap;
  // Cache for glyphs with no visible content to avoid repeated font queries.
  // Uses std::set instead of std::unordered_set because the dataset is very small
  // (typically 1-5 elements like space, tab, newline), making red-black tree's
  // direct integer comparison faster than hash table overhead for high-frequency lookups.
  std::set<GlyphID> emptyGlyphs;

  friend class AtlasStrikeCache;
};

class AtlasStrikeCache {
 public:
  ~AtlasStrikeCache();

  std::shared_ptr<AtlasStrike> findOrCreateStrike(const BytesKey& key);

  void releaseAll();

 private:
  BytesKeyMap<std::shared_ptr<AtlasStrike>> strikes;
};
}  // namespace tgfx

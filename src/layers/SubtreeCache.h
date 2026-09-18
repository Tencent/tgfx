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

#include <memory>
#include <unordered_map>
#include "gpu/resources/ResourceKey.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Image.h"

namespace tgfx {
class TextureProxy;
class ColorSpace;

class SubtreeCache {
 public:
  explicit SubtreeCache(int maxSize) : _maxSize(maxSize) {
  }

  const UniqueKey& uniqueKey() const {
    return _uniqueKey;
  }

  int maxSize() const {
    return _maxSize;
  }

  void addCache(Context* context, int longEdge, std::shared_ptr<TextureProxy> textureProxy,
                const Matrix& imageMatrix, const std::shared_ptr<ColorSpace>& colorSpace,
                float contentScale);

  bool hasCache(Context* context, int longEdge) const;

  /**
   * Same as hasCache(), but additionally requires that the texture was rasterized at a
   * contentScale within drift tolerance of the given contentScale. Subtree content is rasterized
   * at a fixed scale, so scale-dependent results (such as hairline stroke coverage) are baked
   * into the texture and must not be reused once the actual contentScale drifts too far.
   */
  bool hasCache(Context* context, int longEdge, float contentScale) const;

  void draw(Context* context, int longEdge, Canvas* canvas, const Paint& paint) const;

 private:
  struct CacheEntry {
    Matrix imageMatrix = {};
    std::shared_ptr<ColorSpace> colorSpace = nullptr;
    float contentScale = 0.0f;
  };

  const CacheEntry* getValidEntry(Context* context, int longEdge) const;

  int _maxSize = 0;
  UniqueKey _uniqueKey = UniqueKey::Make();
  ResourceKeyMap<CacheEntry> cacheEntries = {};

  UniqueKey makeSizeKey(int longEdge) const;
};
}  // namespace tgfx

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
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

// EXPERIMENTAL (review option b): caches rasterized subtree textures keyed by the mip bucket
// (longEdge) AND a quantized contentScale, so multiple densities can coexist within one bucket
// instead of a single entry being overwritten. Textures are baked at the actual contentScale,
// so scale-dependent rasterization (such as hairline coverage) stays faithful to a direct
// render; reuse only happens within one quantization step, which bounds the resampling error
// of the drawn blit.
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

  void addCache(Context* context, int longEdge, float contentScale,
                std::shared_ptr<TextureProxy> textureProxy, const Matrix& imageMatrix,
                const std::shared_ptr<ColorSpace>& colorSpace);

  /**
   * Returns true if a texture rasterized at a contentScale within one quantization step of
   * the given contentScale is cached for the given mip bucket.
   */
  bool hasCache(Context* context, int longEdge, float contentScale) const;

  void draw(Context* context, int longEdge, float contentScale, Canvas* canvas,
            const Paint& paint) const;

  /**
   * Quantizes a contentScale to a 5% step index. Entries are keyed by the quantized index so
   * that slightly different scales (pinch-noise jitter) share one entry, while larger drifts
   * get their own entry instead of overwriting the previous density.
   */
  static uint32_t QuantizeContentScale(float contentScale);

 private:
  struct CacheEntry {
    Matrix imageMatrix = {};
    std::shared_ptr<ColorSpace> colorSpace = nullptr;
    uint32_t stamp = 0;
  };

  int _maxSize = 0;
  uint32_t _stampCounter = 0;
  UniqueKey _uniqueKey = UniqueKey::Make();
  ResourceKeyMap<CacheEntry> cacheEntries = {};

  UniqueKey makeScaleKey(int longEdge, uint32_t scaleIndex) const;
};
}  // namespace tgfx

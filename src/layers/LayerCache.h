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

#include <list>
#include <map>
#include <memory>
#include <vector>
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

class Layer;
class RasterizedContent;
class Image;
class Context;
class Matrix;

class TextureAtlas;
struct CacheEntry;

/**
 * LayerCache manages RasterizedContent caches for layers using simple texture atlas packing.
 * Images are packed into large shared Surfaces (atlases) using linear row-based allocation.
 * When an atlas is full, new atlases are created. When the cache size exceeds the limit,
 * least recently used entries are evicted. Atlases with low utilization are automatically removed.
 *
 * Features:
 * - Simple linear packing: Efficient O(1) allocation without fragmentation tracking
 * - Automatic atlas creation: New atlases created when existing ones are full
 * - LRU eviction: Least recently used entries are removed when memory limit is reached
 * - Automatic cleanup: Atlases with very low utilization are removed to free memory
 */
class LayerCache {
 public:
  /**
   * Creates a new LayerCache instance with atlas-based storage.
   * @param maxCacheSize The maximum size of cached content in bytes. Default is 64MB.
   * @param atlasSize The size of each atlas texture (width and height). Default is 2048x2048.
   * @param colorSpace The color space for cached images.
   */
  explicit LayerCache(size_t maxCacheSize = 64 * 1024 * 1024, int atlasSize = 2048,
                      std::shared_ptr<ColorSpace> colorSpace = nullptr);

  ~LayerCache();

  /**
   * Sets the maximum cache size in bytes. If the current cache size exceeds the new maximum,
   * LRU entries will be evicted until the cache size is within limits.
   */
  void setMaxCacheSize(size_t maxSize);

  /**
   * Returns the maximum cache size in bytes.
   */
  size_t maxCacheSize() const;

  /**
   * Returns the current total size of cached content in bytes.
   */
  size_t currentCacheSize() const;

  /**
   * Gets cached RasterizedContent for the specified layer if it exists and content scale matches.
   * Returns the cached RasterizedContent if available and the content scale matches exactly.
   * This method marks the cached entry as recently used.
   *
   * @param layer The layer to get cached content for
   * @param contentScale The desired content scale to match
   * @return Pointer to RasterizedContent if found and scale matches, nullptr otherwise
   */
  RasterizedContent* getCachedImage(const Layer* layer, float contentScale);

  /**
   * Caches the given image as RasterizedContent for the specified layer.
   * The image will be packed into an atlas. If the image is too large for the atlas,
   * it will be stored separately without atlas packing.
   * Replaces any existing cache for this layer at any scale.
   *
   * @param layer The layer to cache content for
   * @param contentScale The content scale used to generate the image
   * @param image The image to cache
   * @param imageMatrix The transformation matrix for the image
   */
  void cacheImage(const Layer* layer, float contentScale, std::shared_ptr<Image> image,
                  const Matrix& imageMatrix);

  /**
   * Removes the cached RasterizedContent for the specified layer.
   * If this was the last entry in an atlas, the atlas will be released.
   */
  void invalidateLayer(const Layer* layer);

  /**
   * Clears all cached content and releases all atlas surfaces.
   */
  void clear();

  /**
   * Triggers defragmentation of atlas textures.
   * This can be called periodically to reduce fragmentation and improve atlas utilization.
   * @param forceDefrag If true, forces defragmentation even if fragmentation is low.
   */
  void defragment(bool forceDefrag = false);

  /**
   * Internal: Sets the GPU context for the cache. Called automatically when needed.
   */
  void setContext(Context* context) {
    if (_context == context) {
      return;
    }
    _context = context;
    clear();
  }

 private:
  Context* _context = nullptr;
  size_t _maxCacheSize = 0;
  size_t _currentCacheSize = 0;
  int _atlasSize = 2048;
  std::shared_ptr<ColorSpace> _colorSpace;

  std::map<const Layer*, CacheEntry> _cacheMap;
  std::list<const Layer*> _accessList;
  std::vector<std::unique_ptr<TextureAtlas>> _atlases;

  Rect allocateAtlasRegion(int width, int height, const Layer* owner);
  TextureAtlas* findAtlasContainingRegion(const Rect& region, const Layer* owner);
  void evictLRU();
  void removeEmptyAtlases();
};

}  // namespace tgfx

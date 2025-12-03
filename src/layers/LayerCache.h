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

#include <deque>
#include <list>
#include <map>
#include <memory>

namespace tgfx {

class Layer;
class RasterizedContent;
class Image;
class ColorSpace;
class Matrix;
class Context;

struct CacheEntry;

/**
 * LayerCache manages RasterizedContent caches for layers with LRU eviction policy.
 * When the cache size exceeds the maximum limit, least recently used entries are evicted.
 * Additionally, cache entries that haven't been accessed for more than a specified number of frames
 * are automatically purged.
 *
 * This cache stores RasterizedContent directly (wrapping Image + Matrix).
 * Each layer can have at most one cached RasterizedContent per content scale.
 *
 * Performance Note:
 * While using Atlas can reduce the peak number of drawCalls, under the current cacheImage behavior,
 * performance may be degraded due to eviction mechanisms and frequent offscreen rendering.
 * The LRU eviction policy combined with multiple offscreen rendering operations may cause
 * more overhead than the benefit gained from reduced drawCalls. Future optimization should
 * focus on improving cache reuse strategies and reducing unnecessary evictions.
 */
class LayerCache {
 public:
  /**
   * Creates a new LayerCache instance with the specified maximum size in bytes.
   * @param maxCacheSize The maximum size of cached content in bytes. Default is 64MB.
   * @param colorSpace The color space for cached images.
   */
  explicit LayerCache(size_t maxCacheSize = 64 * 1024 * 1024);

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
   * Returns the number of frames after which unused cache entries are considered expired.
   * Default value is 10 frames.
   */
  size_t expirationFrames() const;

  /**
   * Sets the number of frames after which unused cache entries are considered expired.
   * When advanceFrame() is called, entries that haven't been accessed within this number
   * of frames will be automatically purged from the cache.
   */
  void setExpirationFrames(size_t frames);

  /**
   * Advances the frame counter and purges cache entries that have expired.
   * This should be called once per frame/render cycle.
   */
  void advanceFrame();

  /**
   * Gets cached RasterizedContent for the specified layer if it exists and the cached content scale
   * is greater than or equal to the requested content scale.
   * Returns the cached RasterizedContent if available and the cached scale is >= the requested scale.
   * This method marks the cached entry as recently used.
   *
   * @param layer The layer to get cached content for
   * @param contentScale The desired minimum content scale
   * @return Pointer to RasterizedContent if found and scale is >= requested, nullptr otherwise
   */
  RasterizedContent* getCachedImage(const Layer* layer, float contentScale);

  /**
   * Caches the given image as RasterizedContent for the specified layer.
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
   */
  void invalidateLayer(const Layer* layer);

  /**
   * Clears all cached content.
   */
  void clear();

  /**
   * Sets the maximum size of rasterized layer content (in pixels) that can be cached.
   * Layers with rasterized bounds larger than this size will not be cached.
   * @param maxSize Maximum width or height in pixels. Default is 64 pixels.
   */
  void setMaxCacheContentSize(int maxSize) {
    if (maxSize < 0) {
      maxSize = 0;
    }
    _maxCacheContentSize = maxSize;
  }

  /**
   * Returns the maximum size of rasterized layer content that can be cached.
   */
  int maxCacheContentSize() const {
    return _maxCacheContentSize;
  }

  bool canCacheLayer( Layer* layer, float contentScale) const;

 private:
  size_t _maxCacheSize = 0;
  size_t _currentCacheSize = 0;
  size_t _expirationFrames = 100;  // Default: 10 frames before expiration
  int _maxCacheContentSize = 256.f;

  std::deque<std::shared_ptr<void>> _frameTokens;
  std::shared_ptr<void> _currentFrameToken;

  std::map<const Layer*, CacheEntry> _cacheMap;
  std::list<const Layer*> _accessList;

  void evictLRU();
  void purgeExpiredEntries();
};

}  // namespace tgfx

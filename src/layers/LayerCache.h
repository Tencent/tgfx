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
 *
 * This cache stores RasterizedContent directly (wrapping Image + Matrix).
 * Each layer can have at most one cached RasterizedContent per content scale.
 */
class LayerCache {
 public:
  /**
   * Creates a new LayerCache instance with the specified maximum size in bytes.
   * @param maxCacheSize The maximum size of cached content in bytes. Default is 64MB.
   * @param colorSpace The color space for cached images.
   */
  explicit LayerCache(size_t maxCacheSize = 64 * 1024 * 1024,
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

  /*
   * Sets the context for the cache.
   */
  void setContext(Context* context);

  /**
   * Sets the maximum size of rasterized layer content (in pixels) that can be cached.
   * Layers with rasterized bounds larger than this size will not be cached.
   * @param maxSize Maximum width or height in pixels. Default is 64 pixels.
   */
  void setMaxCacheContentSize(float maxSize) {
    _maxCacheContentSize = maxSize;
  }

  /**
   * Returns the maximum size of rasterized layer content that can be cached.
   */
  float maxCacheContentSize() const {
    return _maxCacheContentSize;
  }

  /**
   * Sets the maximum content scale for layer caching. Layers with content scale greater than
   * this value will not be cached to avoid excessive memory usage at high zoom levels.
   * @param maxScale Maximum content scale. Default is 0.3.
   */
  void setMaxCacheContentScale(float maxScale) {
    _maxCacheContentScale = maxScale;
  }

  /**
   * Returns the maximum content scale for layer caching.
   */
  float maxCacheContentScale() const {
    return _maxCacheContentScale;
  }

 private:
  Context* _context = nullptr;
  size_t _maxCacheSize = 0;
  size_t _currentCacheSize = 0;
  std::shared_ptr<ColorSpace> _colorSpace;
  float _maxCacheContentSize = 64.0f;
  float _maxCacheContentScale = 0.3f;

  std::map<const Layer*, CacheEntry> _cacheMap;
  std::list<const Layer*> _accessList;

  void evictLRU();
};

}  // namespace tgfx

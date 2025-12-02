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
class Surface;

struct CacheEntry;
struct AtlasRegion;

/**
 * LayerCache manages RasterizedContent caches for layers with LRU eviction policy.
 * When the cache size exceeds the maximum limit, least recently used entries are evicted.
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
   * Sets the context for the cache.
   */
  void setContext(Context* context);

  /**
   * Returns the number of frames after which unused cached layers are considered expired.
   * The default value is 120 frames. This is similar to ResourceCache's expiration mechanism.
   */
  size_t expirationFrames() const {
    return _expirationFrames;
  }

  /**
   * Sets the number of frames after which unused cached layers are considered expired.
   * Cached entries that haven't been accessed for more than this many frames will be removed
   * during the next call to advanceFrameAndPurge().
   */
  void setExpirationFrames(size_t frames);

  /**
   * Advances the frame counter and removes expired cached entries. This should be called once per
   * frame to maintain proper cache expiration. Entries that haven't been accessed for more than
   * expirationFrames() frames will be removed. Also reorganizes and recycles atlas surfaces if
   * they become too sparse with unused tiles.
   */
  void advanceFrameAndPurge();

   int atlasTileSize() const;

  /**
   * Sets the tile size for the atlas. This should be set to match the DisplayList's tile size
   * to optimize cache efficiency. Default is 256 pixels. If the computed atlas size is less than
   * tileSize, atlas caching will be disabled.
   */
  void setAtlasTileSize(int tileSize);

  /**
   * Checks if the cache can continue caching more images. Returns false if no free tiles are
   * available in the atlas or if atlas caching is disabled. This method does not modify the cache
   * state and assumes all images are smaller than the tile size.
   *
   * @return true if at least one free tile is available, false otherwise
   */
  bool canContinueCaching() const;

 private:
  Context* _context = nullptr;
  size_t _maxCacheSize = 0;
  size_t _currentCacheSize = 0;
  std::shared_ptr<ColorSpace> _colorSpace;
  size_t _expirationFrames = 120;
  uint64_t _frameCounter = 0;

  std::map<const Layer*, CacheEntry> _cacheMap;
  std::list<const Layer*> _accessList;

  // Atlas related members
  static constexpr int MAX_ATLAS_SIZE = 2048;
  int _tileSize = 256;
  size_t _usedTileCount = 0;  // Track number of used tiles for fast canContinueCaching check
  int _atlasWidth = 0;  // Configured atlas width, 0 means not initialized
  int _atlasHeight = 0;  // Configured atlas height, 0 means not initialized
  struct AtlasInfo {
    std::shared_ptr<Surface> surface;
    std::vector<bool> tileMap;
  };
  std::vector<AtlasInfo> _atlases = {};

  void evictLRU();
  void purgeExpiredEntries();
  void compactAtlases();
  void calculateAtlasConfiguration();
  void clearAtlases();
  std::shared_ptr<Image> getAtlasRegionImage(size_t atlasIndex, int tileX, int tileY);
  bool allocateAtlasTile(size_t* outAtlasIndex, int* outTileX, int* outTileY);
  void freeAtlasTile(size_t atlasIndex, int tileX, int tileY);
  void calculateAtlasGridSize(int* width, int* height);
};

}  // namespace tgfx

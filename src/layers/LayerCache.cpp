/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "layers/LayerCache.h"
#include <list>
#include <map>
#include <memory>
#include "contents/RasterizedContent.h"
#include "core/utils/Log.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {

/**
 * Internal cache entry that tracks RasterizedContent with LRU information
 */
struct CacheEntry {
  std::shared_ptr<RasterizedContent> content;
  float contentScale = 0.0f;
  size_t estimatedSize = 0;
  std::list<const Layer*>::iterator accessIterator;
};

/**
 * Implementation class for LayerCache
 */
class LayerCache::Impl {
 public:
  explicit Impl(size_t maxCacheSize, std::shared_ptr<ColorSpace> colorSpace)
      : _maxCacheSize(maxCacheSize), _colorSpace(std::move(colorSpace)) {
  }

  void setMaxCacheSize(size_t maxSize) {
    if (_maxCacheSize == maxSize) {
      return;
    }
    _maxCacheSize = maxSize;
    evictLRU();
  }

  size_t maxCacheSize() const {
    return _maxCacheSize;
  }

  size_t currentCacheSize() const {
    return _currentCacheSize;
  }

  RasterizedContent* getCachedImageAndRect(const Layer* layer, float contentScale) {
    if (layer == nullptr) {
      return nullptr;
    }

    auto it = _cacheMap.find(layer);
    if (it == _cacheMap.end()) {
      return nullptr;
    }

    auto& entry = it->second;
    // Check if content scale matches exactly (within floating point tolerance)
    if (std::abs(entry.contentScale - contentScale) > 1e-6f) {
      return nullptr;
    }

    if (!entry.content) {
      return nullptr;
    }

    // Mark as recently used by moving to end of access list
    _accessList.erase(entry.accessIterator);
    _accessList.push_back(layer);
    entry.accessIterator = std::prev(_accessList.end());

    return entry.content.get();
  }

  void cacheImage(const Layer* layer, float contentScale, std::shared_ptr<Image> image,
                  const Matrix& imageMatrix) {
    if (layer == nullptr || !image) {
      return;
    }

    // Create RasterizedContent wrapper
    // contextID is 0 because the context is managed by DisplayList and may change
    auto rasterizedContent =
        std::make_shared<RasterizedContent>(0, contentScale, std::move(image), imageMatrix);

    // Estimate size: assume 4 bytes per pixel (RGBA)
    size_t estimatedSize = estimateImageSize(rasterizedContent->getImage().get());

    // Check if there's an existing entry for this layer
    auto existingIt = _cacheMap.find(layer);
    if (existingIt != _cacheMap.end()) {
      // Remove the old entry's size from current total
      _currentCacheSize -= existingIt->second.estimatedSize;
      // Remove from access list
      _accessList.erase(existingIt->second.accessIterator);
    }

    // Create new cache entry
    CacheEntry entry;
    entry.content = rasterizedContent;
    entry.contentScale = contentScale;
    entry.estimatedSize = estimatedSize;

    // Add to cache map
    _cacheMap[layer] = entry;

    // Add to access list (most recently used goes at end)
    _accessList.push_back(layer);
    _cacheMap[layer].accessIterator = std::prev(_accessList.end());

    // Update total cache size
    _currentCacheSize += estimatedSize;

    // Evict LRU entries if necessary
    evictLRU();
  }

  void invalidateLayer(const Layer* layer) {
    auto it = _cacheMap.find(layer);
    if (it == _cacheMap.end()) {
      return;
    }

    auto& entry = it->second;
    _currentCacheSize -= entry.estimatedSize;
    _accessList.erase(entry.accessIterator);
    _cacheMap.erase(it);
  }

  void clear() {
    _cacheMap.clear();
    _accessList.clear();
    _currentCacheSize = 0;
  }

 private:
  size_t _maxCacheSize = 0;
  size_t _currentCacheSize = 0;
  std::shared_ptr<ColorSpace> _colorSpace;

  // Map from Layer pointer to CacheEntry
  std::map<const Layer*, CacheEntry> _cacheMap;

  // Access list for LRU tracking (front is least recently used, back is most recently used)
  std::list<const Layer*> _accessList;

  /**
   * Estimates the size of an image in bytes
   * Assumes 4 bytes per pixel (RGBA8888 format)
   */
  static size_t estimateImageSize(const Image* image) {
    if (!image) {
      return 0;
    }
    // Add 10% overhead for metadata and internal structures
    return static_cast<size_t>(image->width() * image->height() * 4 * 1.1f);
  }

  /**
   * Evicts least recently used entries until cache size is within limit
   */
  void evictLRU() {
    while (_currentCacheSize > _maxCacheSize && !_accessList.empty()) {
      // Get the least recently used layer (front of the list)
      const Layer* lruLayer = _accessList.front();
      invalidateLayer(lruLayer);
    }
  }
};

// ==================== Public LayerCache Implementation ====================

LayerCache::LayerCache(size_t maxCacheSize, std::shared_ptr<ColorSpace> colorSpace)
    : _impl(std::make_unique<Impl>(maxCacheSize, std::move(colorSpace))) {
}

LayerCache::~LayerCache() = default;

void LayerCache::setMaxCacheSize(size_t maxSize) {
  if (_impl) {
    _impl->setMaxCacheSize(maxSize);
  }
}

size_t LayerCache::maxCacheSize() const {
  return _impl ? _impl->maxCacheSize() : 0;
}

size_t LayerCache::currentCacheSize() const {
  return _impl ? _impl->currentCacheSize() : 0;
}

RasterizedContent* LayerCache::getCachedImageAndRect(const Layer* layer, float contentScale) {
  if (_impl) {
    return _impl->getCachedImageAndRect(layer, contentScale);
  }
  return nullptr;
}

void LayerCache::cacheImage(const Layer* layer, float contentScale, std::shared_ptr<Image> image,
                            const Matrix& imageMatrix) {
  if (_impl) {
    _impl->cacheImage(layer, contentScale, std::move(image), imageMatrix);
  }
}

void LayerCache::invalidateLayer(const Layer* layer) {
  if (_impl) {
    _impl->invalidateLayer(layer);
  }
}

void LayerCache::clear() {
  if (_impl) {
    _impl->clear();
  }
}

}  // namespace tgfx

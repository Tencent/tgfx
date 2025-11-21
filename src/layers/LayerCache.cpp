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

#include "layers/LayerCache.h"
#include <algorithm>
#include <cmath>
#include "LayerCache.h"
#include "contents/RasterizedContent.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {

struct CacheEntry {
  std::shared_ptr<RasterizedContent> content;
  float contentScale = 0.0f;
  size_t estimatedSize = 0;
  std::list<const Layer*>::iterator accessIterator;
};

static size_t estimateImageSize(const Image* image) {
  if (!image) {
    return 0;
  }
  return static_cast<size_t>(image->width() * image->height() * 4);
}

LayerCache::LayerCache(size_t maxCacheSize, std::shared_ptr<ColorSpace> colorSpace)
    : _maxCacheSize(maxCacheSize), _colorSpace(std::move(colorSpace)) {
}

LayerCache::~LayerCache() = default;

void LayerCache::setMaxCacheSize(size_t maxSize) {
  if (_maxCacheSize == maxSize) {
    return;
  }
  _maxCacheSize = maxSize;
  evictLRU();
}

size_t LayerCache::maxCacheSize() const {
  return _maxCacheSize;
}

size_t LayerCache::currentCacheSize() const {
  return _currentCacheSize;
}

RasterizedContent* LayerCache::getCachedImage(const Layer* layer, float contentScale) {
  if (layer == nullptr) {
    return nullptr;
  }

  auto it = _cacheMap.find(layer);
  if (it == _cacheMap.end()) {
    return nullptr;
  }

  auto& entry = it->second;
  if (entry.contentScale < contentScale) {
    return nullptr;
  }

  if (!entry.content) {
    return nullptr;
  }

  _accessList.erase(entry.accessIterator);
  _accessList.push_back(layer);
  entry.accessIterator = std::prev(_accessList.end());

  return entry.content.get();
}

void LayerCache::cacheImage(const Layer* layer, float contentScale, std::shared_ptr<Image> image,
                            const Matrix& imageMatrix) {
  if (layer == nullptr || !image) {
    return;
  }

  auto rasterizedContent =
      std::make_shared<RasterizedContent>(0, contentScale, std::move(image), imageMatrix);
  size_t estimatedSize = estimateImageSize(rasterizedContent->getImage().get());

  auto existingIt = _cacheMap.find(layer);
  if (existingIt != _cacheMap.end()) {
    _currentCacheSize -= existingIt->second.estimatedSize;
    _accessList.erase(existingIt->second.accessIterator);
  }

  CacheEntry entry;
  entry.content = rasterizedContent;
  entry.contentScale = contentScale;
  entry.estimatedSize = estimatedSize;

  _cacheMap[layer] = entry;
  _accessList.push_back(layer);
  _cacheMap[layer].accessIterator = std::prev(_accessList.end());
  _currentCacheSize += estimatedSize;

  evictLRU();
}

void LayerCache::invalidateLayer(const Layer* layer) {
  if (layer == nullptr) {
    return;
  }

  auto it = _cacheMap.find(layer);
  if (it == _cacheMap.end()) {
    return;
  }

  auto& entry = it->second;
  _currentCacheSize -= entry.estimatedSize;
  _accessList.erase(entry.accessIterator);
  _cacheMap.erase(it);
}

void LayerCache::clear() {
  _cacheMap.clear();
  _accessList.clear();
  _currentCacheSize = 0;
}

void LayerCache::evictLRU() {
  while (_currentCacheSize > _maxCacheSize && !_accessList.empty()) {
    const Layer* lruLayer = _accessList.front();
    auto it = _cacheMap.find(lruLayer);
    if (it != _cacheMap.end()) {
      _currentCacheSize -= it->second.estimatedSize;
      _accessList.erase(it->second.accessIterator);
      _cacheMap.erase(it);
    }
  }
}

void LayerCache::setContext(Context* context) {
  if (_context == context) {
    return;
  }
  _context = context;
  clear();
}

}  // namespace tgfx

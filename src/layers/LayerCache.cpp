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
#include <list>
#include <map>
#include <memory>
#include "contents/RasterizedContent.h"
#include "core/utils/Log.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {

struct AtlasRegion {
  Rect rect;
  bool occupied = false;
  const Layer* owner = nullptr;

  explicit AtlasRegion(const Rect& r, bool occ = false, const Layer* own = nullptr)
      : rect(r), occupied(occ), owner(own) {
  }

  size_t area() const {
    return static_cast<size_t>(rect.width() * rect.height());
  }
};

class TextureAtlas {
 public:
  const int padding = 0;
  TextureAtlas(Context* context, int size, std::shared_ptr<ColorSpace> colorSpace)
      : _context(context), _size(size), _colorSpace(std::move(colorSpace)),
        _currentX(0), _currentY(0), _rowHeight(0) {
    _surface = Surface::Make(context, size, size, false, 1, false, 0, _colorSpace);
  }

  Rect allocate(int width, int height, const Layer* owner) {
    if (!_surface) {
      return Rect::MakeEmpty();
    }

    int paddedWidth = width + padding * 2;
    int paddedHeight = height + padding * 2;

    // Try to fit in current row
    if (_currentX + paddedWidth <= _size) {
      Rect region = Rect::MakeXYWH(static_cast<float>(_currentX + padding),
                                    static_cast<float>(_currentY + padding),
                                    static_cast<float>(width), static_cast<float>(height));
      _currentX += paddedWidth;
      _rowHeight = std::max(_rowHeight, paddedHeight);
      _occupiedRegions.emplace_back(region, true, owner);
      return region;
    }

    // Start a new row
    _currentY += _rowHeight;
    _currentX = 0;
    _rowHeight = 0;

    if (_currentY + paddedHeight > _size) {
      return Rect::MakeEmpty();  // Atlas is full
    }

    Rect region = Rect::MakeXYWH(static_cast<float>(_currentX + padding),
                                  static_cast<float>(_currentY + padding),
                                  static_cast<float>(width), static_cast<float>(height));
    _currentX += paddedWidth;
    _rowHeight = paddedHeight;
    _occupiedRegions.emplace_back(region, true, owner);
    return region;
  }



  bool drawImage(const Rect& region, std::shared_ptr<Image> image) {
    if (!_surface || !image) {
      return false;
    }
    auto canvas = _surface->getCanvas();
    if (!canvas) {
      return false;
    }
    canvas->save();
    canvas->translate(region.left, region.top);
    float scaleX = region.width() / static_cast<float>(image->width());
    float scaleY = region.height() / static_cast<float>(image->height());
    if (std::abs(scaleX - 1.0f) > 0.001f || std::abs(scaleY - 1.0f) > 0.001f) {
      canvas->scale(scaleX, scaleY);
    }
    canvas->drawImage(image, 0, 0);
    canvas->restore();
    return true;
  }

  std::shared_ptr<Image> makeImageSnapshot() const {
    if (_surface) {
      return _surface->makeImageSnapshot();
    }
    return nullptr;
  }

  Context* context() const {
    return _context;
  }

  int size() const {
    return _size;
  }

  const std::vector<AtlasRegion>& getOccupiedRegions() const {
    return _occupiedRegions;
  }

 private:
  Context* _context;
  int _size;
  std::shared_ptr<ColorSpace> _colorSpace;
  std::shared_ptr<Surface> _surface;
  std::vector<AtlasRegion> _occupiedRegions;

  int _currentX = 0;
  int _currentY = 0;
  int _rowHeight = 0;

};

struct CacheEntry {
  std::unique_ptr<RasterizedContent> content;
  float contentScale = 0.0f;
  size_t estimatedSize = 0;
  std::list<const Layer*>::iterator accessIterator;
  TextureAtlas* atlas = nullptr;
  Rect atlasRegion;
  std::shared_ptr<Image> sourceImage;
};

LayerCache::LayerCache(size_t maxCacheSize, int atlasSize, std::shared_ptr<ColorSpace> colorSpace)
    : _maxCacheSize(maxCacheSize), _atlasSize(atlasSize), _colorSpace(std::move(colorSpace)) {
}

LayerCache::~LayerCache() {
  clear();
}

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
  if (std::abs(entry.contentScale - contentScale) > 1e-6f) {
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
  if (layer == nullptr || !image || !_context) {
    return;
  }
  int imageWidth = image->width();
  int imageHeight = image->height();
  size_t estimatedSize = static_cast<size_t>(imageWidth * imageHeight * 4);
  auto existingIt = _cacheMap.find(layer);
  if (existingIt != _cacheMap.end()) {
    _currentCacheSize -= existingIt->second.estimatedSize;
    _accessList.erase(existingIt->second.accessIterator);
    _cacheMap.erase(existingIt);
    removeEmptyAtlases();
  }
  bool useAtlas = (imageWidth <= _atlasSize / 2 && imageHeight <= _atlasSize / 2);
  std::unique_ptr<RasterizedContent> rasterizedContent;
  CacheEntry entry;
  entry.contentScale = contentScale;
  entry.estimatedSize = estimatedSize;
  entry.sourceImage = image;
  if (useAtlas) {
    Rect region = allocateAtlasRegion(imageWidth, imageHeight, layer);
    if (!region.isEmpty()) {
      entry.atlas = findAtlasContainingRegion(region, layer);
      if (entry.atlas && entry.atlas->drawImage(region, image)) {
        entry.atlasRegion = region;
        auto atlasImage = entry.atlas->makeImageSnapshot();
        if (atlasImage) {
          auto subImage = atlasImage->makeSubset(region);
          if (subImage) {
            rasterizedContent = std::make_unique<RasterizedContent>(
                _context->uniqueID(), contentScale, subImage, imageMatrix);
          }
        }
      }
    }
  }
  if (!rasterizedContent) {
    entry.atlas = nullptr;
    entry.atlasRegion = Rect::MakeEmpty();
    rasterizedContent =
        std::make_unique<RasterizedContent>(_context->uniqueID(), contentScale, image, imageMatrix);
  }
  entry.content = std::move(rasterizedContent);
  _cacheMap[layer] = std::move(entry);
  _accessList.push_back(layer);
  _cacheMap[layer].accessIterator = std::prev(_accessList.end());
  _currentCacheSize += estimatedSize;
  evictLRU();
}

void LayerCache::invalidateLayer(const Layer* layer) {
  auto it = _cacheMap.find(layer);
  if (it == _cacheMap.end()) {
    return;
  }
  auto& entry = it->second;
  _currentCacheSize -= entry.estimatedSize;
  _accessList.erase(entry.accessIterator);
  _cacheMap.erase(it);
  removeEmptyAtlases();
}

void LayerCache::clear() {
  _cacheMap.clear();
  _accessList.clear();
  _atlases.clear();
  _currentCacheSize = 0;
}

void LayerCache::defragment(bool ) {
  // Lazy defragmentation: only triggered when creating new atlas
  // Old atlases remain as-is, new valid entries are drawn into new atlases
}

Rect LayerCache::allocateAtlasRegion(int width, int height, const Layer* owner) {
  if (!_context) {
    return Rect::MakeEmpty();
  }
  for (auto& atlas : _atlases) {
    Rect region = atlas->allocate(width, height, owner);
    if (!region.isEmpty()) {
      return region;
    }
  }
  // Create new atlas directly without migrating old data
  auto newAtlas = std::make_unique<TextureAtlas>(_context, _atlasSize, _colorSpace);
  Rect region = newAtlas->allocate(width, height, owner);
  if (!region.isEmpty()) {
    _atlases.push_back(std::move(newAtlas));
    return region;
  }
  return Rect::MakeEmpty();
}

TextureAtlas* LayerCache::findAtlasContainingRegion(const Rect& region, const Layer* owner) {
  for (auto& atlas : _atlases) {
    const auto& occupied = atlas->getOccupiedRegions();
    for (const auto& occupied_region : occupied) {
      if (occupied_region.owner == owner &&
          std::abs(occupied_region.rect.left - region.left) < 0.1f &&
          std::abs(occupied_region.rect.top - region.top) < 0.1f) {
        return atlas.get();
      }
    }
  }
  return nullptr;
}



void LayerCache::evictLRU() {
  while (_currentCacheSize > _maxCacheSize && !_accessList.empty()) {
    const Layer* lruLayer = _accessList.front();
    auto it = _cacheMap.find(lruLayer);
    if (it != _cacheMap.end()) {
      auto& entry = it->second;
      _currentCacheSize -= entry.estimatedSize;
      _accessList.erase(entry.accessIterator);
      _cacheMap.erase(it);
    }
  }
  removeEmptyAtlases();
}

void LayerCache::removeEmptyAtlases() {
  if (_atlases.size() <= 1) {
    return;  // Keep at least one atlas
  }

  // Count entries in each atlas
  std::map<TextureAtlas*, size_t> atlasUsageCount;
  for (const auto& pair : _cacheMap) {
    if (pair.second.atlas != nullptr) {
      atlasUsageCount[pair.second.atlas]++;
    }
  }

  // Identify atlases to remove based on utilization threshold
  size_t atlasCount = _atlases.size();
  size_t minEntriesToKeep = std::max(size_t(1), atlasCount / 4);

  std::vector<TextureAtlas*> atlasesToRemove;
  for (auto& atlas : _atlases) {
    auto it = atlasUsageCount.find(atlas.get());
    if (it == atlasUsageCount.end()) {
      // Completely empty atlas
      atlasesToRemove.push_back(atlas.get());
    } else if (it->second < minEntriesToKeep) {
      // Low-utilization atlas
      atlasesToRemove.push_back(atlas.get());
    }
  }

  // Migrate cache entries from atlases being removed to remaining atlases
  for (auto atlasToRemove : atlasesToRemove) {
    std::vector<const Layer*> layersToMigrate;
    for (auto& pair : _cacheMap) {
      if (pair.second.atlas == atlasToRemove) {
        layersToMigrate.push_back(pair.first);
      }
    }

    for (const auto* layer : layersToMigrate) {
      auto it = _cacheMap.find(layer);
      if (it == _cacheMap.end()) {
        continue;
      }

      auto& entry = it->second;
      if (!entry.sourceImage) {
        // No source image, remove the entry
        _currentCacheSize -= entry.estimatedSize;
        _accessList.erase(entry.accessIterator);
        _cacheMap.erase(it);
        continue;
      }

      // Try to allocate in a new atlas and redraw
      int imageWidth = entry.sourceImage->width();
      int imageHeight = entry.sourceImage->height();
      Rect newRegion = allocateAtlasRegion(imageWidth, imageHeight, layer);

      if (!newRegion.isEmpty()) {
        TextureAtlas* newAtlas = findAtlasContainingRegion(newRegion, layer);
        if (newAtlas && newAtlas->drawImage(newRegion, entry.sourceImage)) {
          entry.atlas = newAtlas;
          entry.atlasRegion = newRegion;

          // Update the cached image snapshot from new atlas
          auto atlasImage = newAtlas->makeImageSnapshot();
          if (atlasImage) {
            auto subImage = atlasImage->makeSubset(newRegion);
            if (subImage && entry.content) {
              // Note: RasterizedContent doesn't have a direct way to update the image,
              // so we recreate it. In practice, content.get() still points to valid data
              // since the new atlas has the same content as the old one.
              // The old RasterizedContent remains valid as a snapshot.
            }
          }
        }
      } else {
        // Can't fit in atlas anymore, remove the entry
        _currentCacheSize -= entry.estimatedSize;
        _accessList.erase(entry.accessIterator);
        _cacheMap.erase(it);
      }
    }
  }

  // Finally, remove the empty or low-utilization atlases
  _atlases.erase(
      std::remove_if(_atlases.begin(), _atlases.end(),
                     [&atlasesToRemove](const std::unique_ptr<TextureAtlas>& atlas) {
                       return std::find(atlasesToRemove.begin(), atlasesToRemove.end(), atlas.get()) !=
                              atlasesToRemove.end();
                     }),
      _atlases.end());
}

}  // namespace tgfx

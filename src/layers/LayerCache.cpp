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
#include <set>
#include "contents/RasterizedContent.h"
#include "core/utils/Log.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {

struct CacheEntry {
  std::shared_ptr<RasterizedContent> content;
  float contentScale = 0.0f;
  size_t estimatedSize = 0;
  std::list<const Layer*>::iterator accessIterator;
  uint64_t lastUsedFrame = 0;
  size_t atlasIndex = static_cast<size_t>(-1);  // -1 means not allocated in atlas
  int atlasTileX = -1;
  int atlasTileY = -1;
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
  entry.lastUsedFrame = _frameCounter;

  return entry.content.get();
}

void LayerCache::cacheImage(const Layer* layer, float contentScale, std::shared_ptr<Image> image,
                            const Matrix& imageMatrix) {
  if (layer == nullptr || !image) {
    return;
  }

  size_t atlasIndex = static_cast<size_t>(-1);
  int tileX = -1, tileY = -1;
  std::shared_ptr<Image> cachedImage = image;

  // Try to use atlas if the image size fits within a tile
  if (image->width() <= _tileSize && image->height() <= _tileSize &&
      allocateAtlasTile(atlasIndex, tileX, tileY)) {
    if (atlasIndex < _atlases.size() && _atlases[atlasIndex].surface) {
      auto canvas = _atlases[atlasIndex].surface->getCanvas();
      float drawX = tileX * _tileSize;
      float drawY = tileY * _tileSize;
      canvas->save();
      canvas->translate(drawX, drawY);
      canvas->drawImage(image);
      canvas->restore();
      cachedImage = getAtlasRegionImage(atlasIndex, tileX, tileY);
      if (!cachedImage) {
        freeAtlasTile(atlasIndex, tileX, tileY);
        atlasIndex = static_cast<size_t>(-1);
        tileX = -1;
        tileY = -1;
        cachedImage = image;
      }
    }
  }

  auto rasterizedContent =
      std::make_shared<RasterizedContent>(0, contentScale, std::move(cachedImage), imageMatrix);
  size_t estimatedSize = estimateImageSize(rasterizedContent->getImage().get());

  auto existingIt = _cacheMap.find(layer);
  if (existingIt != _cacheMap.end()) {
    _currentCacheSize -= existingIt->second.estimatedSize;
    _accessList.erase(existingIt->second.accessIterator);
    if (existingIt->second.atlasIndex != static_cast<size_t>(-1) &&
        existingIt->second.atlasTileX >= 0 && existingIt->second.atlasTileY >= 0) {
      freeAtlasTile(existingIt->second.atlasIndex, existingIt->second.atlasTileX,
                    existingIt->second.atlasTileY);
    }
  }

  CacheEntry entry;
  entry.content = rasterizedContent;
  entry.contentScale = contentScale;
  entry.estimatedSize = estimatedSize;
  entry.lastUsedFrame = _frameCounter;
  entry.atlasIndex = atlasIndex;
  entry.atlasTileX = tileX;
  entry.atlasTileY = tileY;

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
  if (entry.atlasIndex != static_cast<size_t>(-1) && entry.atlasTileX >= 0 &&
      entry.atlasTileY >= 0) {
    freeAtlasTile(entry.atlasIndex, entry.atlasTileX, entry.atlasTileY);
  }
  _cacheMap.erase(it);
}

void LayerCache::clear() {
  _cacheMap.clear();
  _accessList.clear();
  _currentCacheSize = 0;
  clearAtlases();
}

void LayerCache::evictLRU() {
  while (_currentCacheSize > _maxCacheSize && !_accessList.empty()) {
    const Layer* lruLayer = _accessList.front();
    auto it = _cacheMap.find(lruLayer);
    if (it != _cacheMap.end()) {
      _currentCacheSize -= it->second.estimatedSize;
      _accessList.erase(it->second.accessIterator);
      if (it->second.atlasIndex != static_cast<size_t>(-1) && it->second.atlasTileX >= 0 &&
          it->second.atlasTileY >= 0) {
        freeAtlasTile(it->second.atlasIndex, it->second.atlasTileX, it->second.atlasTileY);
      }
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
  if (context) {
    initAtlases();
  }
}

void LayerCache::setExpirationFrames(size_t frames) {
  if (_expirationFrames == frames) {
    return;
  }
  _expirationFrames = frames;
}

int LayerCache::atlasTileSize() const {
  return _tileSize;
}

void LayerCache::setAtlasTileSize(int tileSize) {
  if (tileSize <= 0 || tileSize > 2048) {
    return;
  }
  if (_tileSize == tileSize) {
    return;
  }
  _tileSize = tileSize;
  clear();
  initAtlases();
}

void LayerCache::advanceFrameAndPurge() {
  ++_frameCounter;
  purgeExpiredEntries();
  evictLRU();
  compactAtlases();
}

void LayerCache::purgeExpiredEntries() {
  auto it = _cacheMap.begin();
  while (it != _cacheMap.end()) {
    auto& entry = it->second;
    if (_frameCounter - entry.lastUsedFrame > _expirationFrames) {
      _currentCacheSize -= entry.estimatedSize;
      _accessList.erase(entry.accessIterator);
      if (entry.atlasIndex != static_cast<size_t>(-1) && entry.atlasTileX >= 0 &&
          entry.atlasTileY >= 0) {
        freeAtlasTile(entry.atlasIndex, entry.atlasTileX, entry.atlasTileY);
      }
      it = _cacheMap.erase(it);
    } else {
      ++it;
    }
  }
}

void LayerCache::calculateAtlasGridSize(int& width, int& height) {
  // Calculate nearest rectangle dimensions with at least 2 atlases
  // such that width * height >= 2 and both dimensions are as close as possible
  width = 1;
  height = 2;
  int targetAtlasCount = 2;
  int maxDim = MAX_ATLAS_SIZE / _tileSize;

  // Find optimal grid dimensions
  for (int w = 1; w * w <= targetAtlasCount * 4; ++w) {
    if (w > maxDim) break;
    for (int h = 1; h * w <= targetAtlasCount * 4; ++h) {
      if (h > maxDim) break;
      if (w * h >= targetAtlasCount && std::abs(w - h) < std::abs(width - height)) {
        width = w;
        height = h;
      }
    }
  }
}

void LayerCache::initAtlases() {
  if (!_context || _tileSize <= 0) {
    return;
  }

  int gridWidth = 1, gridHeight = 1;
  calculateAtlasGridSize(gridWidth, gridHeight);

  int atlasSize = std::min(MAX_ATLAS_SIZE, gridWidth * _tileSize);
  if (atlasSize < _tileSize) {
    // Atlas size is smaller than tile size, disable atlas caching
    return;
  }

  auto atlasCount = static_cast<size_t>(gridWidth * gridHeight);
  _atlases.resize(atlasCount);

  for (size_t i = 0; i < atlasCount; ++i) {
    auto& atlasInfo = _atlases[i];
    atlasInfo.width = atlasSize;
    atlasInfo.height = atlasSize;
    atlasInfo.surface =
        Surface::Make(_context, atlasSize, atlasSize, true, 1, false, 0, _colorSpace);
    if (atlasInfo.surface) {
      atlasInfo.image = atlasInfo.surface->makeImageSnapshot();
      int tileCountPerDim = atlasSize / _tileSize;
      atlasInfo.tileMap.resize(static_cast<size_t>(tileCountPerDim * tileCountPerDim), false);
    }
  }
}

void LayerCache::clearAtlases() {
  _atlases.clear();
}

std::shared_ptr<Image> LayerCache::getAtlasRegionImage(size_t atlasIndex, int tileX, int tileY) {
  if (atlasIndex >= _atlases.size()) {
    return nullptr;
  }
  auto& atlasInfo = _atlases[atlasIndex];
  if (!atlasInfo.image || tileX < 0 || tileY < 0) {
    return nullptr;
  }
  int tileCountPerDim = atlasInfo.width / _tileSize;
  if (tileX >= tileCountPerDim || tileY >= tileCountPerDim) {
    return nullptr;
  }
  Rect regionRect = Rect::MakeXYWH(tileX * _tileSize, tileY * _tileSize, _tileSize, _tileSize);
  return atlasInfo.image->makeSubset(regionRect);
}

bool LayerCache::allocateAtlasTile(size_t& outAtlasIndex, int& outTileX, int& outTileY) {
  for (size_t atlasIdx = 0; atlasIdx < _atlases.size(); ++atlasIdx) {
    auto& atlasInfo = _atlases[atlasIdx];
    int tileCountPerDim = atlasInfo.width / _tileSize;
    for (size_t i = 0; i < atlasInfo.tileMap.size(); ++i) {
      if (!atlasInfo.tileMap[i]) {
        atlasInfo.tileMap[i] = true;
        outAtlasIndex = atlasIdx;
        outTileY = static_cast<int>(i) / tileCountPerDim;
        outTileX = static_cast<int>(i) % tileCountPerDim;
        return true;
      }
    }
  }
  return false;
}

void LayerCache::freeAtlasTile(size_t atlasIndex, int tileX, int tileY) {
  if (atlasIndex >= _atlases.size()) {
    return;
  }
  auto& atlasInfo = _atlases[atlasIndex];
  int tileCountPerDim = atlasInfo.width / _tileSize;
  if (tileX >= 0 && tileY >= 0 && tileX < tileCountPerDim && tileY < tileCountPerDim) {
    size_t index = static_cast<size_t>(tileY * tileCountPerDim + tileX);
    DEBUG_ASSERT(index < atlasInfo.tileMap.size());
    atlasInfo.tileMap[index] = false;
  }
}

bool LayerCache::canContinueCaching() const {
  if (_atlases.empty()) {
    // Atlas caching is disabled
    return false;
  }

  for (const auto& atlasInfo : _atlases) {
    for (bool tileUsed : atlasInfo.tileMap) {
      if (!tileUsed) {
        return true;
      }
    }
  }
  return false;
}

void LayerCache::compactAtlases() {
  if (_atlases.empty() || !_context) {
    return;
  }

  // Calculate usage statistics for each atlas
  static constexpr float COMPACT_THRESHOLD = 0.25f;  // Recycle atlas if usage < 25%

  std::vector<size_t> atlasesToKeep;
  std::vector<size_t> atlasesToRecycle;

  for (size_t i = 0; i < _atlases.size(); ++i) {
    const auto& atlasInfo = _atlases[i];
    int usedTiles = 0;
    for (bool tileUsed : atlasInfo.tileMap) {
      if (tileUsed) {
        ++usedTiles;
      }
    }

    float usageRatio =
        atlasInfo.tileMap.empty() ? 0.0f : static_cast<float>(usedTiles) / atlasInfo.tileMap.size();

    // Keep atlas if it has reasonable usage or is the only one left
    if (usageRatio >= COMPACT_THRESHOLD || (atlasesToKeep.empty() && i == _atlases.size() - 1)) {
      atlasesToKeep.push_back(i);
    } else {
      atlasesToRecycle.push_back(i);
    }
  }

  // If all atlases can be recycled (none meet threshold), keep the first one
  if (atlasesToKeep.empty() && !_atlases.empty()) {
    atlasesToKeep.push_back(0);
    if (atlasesToRecycle.size() > 0) {
      atlasesToRecycle.erase(std::find(atlasesToRecycle.begin(), atlasesToRecycle.end(), 0));
    }
  }

  // If there are atlases to recycle, collect their tiles and reorganize
  if (!atlasesToRecycle.empty()) {
    // Collect all tiles from recycled atlases
    std::vector<std::pair<size_t, std::pair<int, int>>> tilesToRelocate;
    for (auto& cacheEntry : _cacheMap) {
      if (cacheEntry.second.atlasIndex != static_cast<size_t>(-1)) {
        size_t atlasIdx = cacheEntry.second.atlasIndex;
        if (std::find(atlasesToRecycle.begin(), atlasesToRecycle.end(), atlasIdx) !=
            atlasesToRecycle.end()) {
          tilesToRelocate.push_back(
              {atlasIdx, {cacheEntry.second.atlasTileX, cacheEntry.second.atlasTileY}});
        }
      }
    }

    // Check if compacting is worthwhile
    static constexpr int MIN_TILES_TO_RELOCATE = 4;  // At least 4 tiles to make it worth relocating
    bool shouldRelocate = false;

    if (tilesToRelocate.size() >= MIN_TILES_TO_RELOCATE) {
      // Check if tiles come from multiple atlases
      std::set<size_t> sourceAtlases;
      for (const auto& tile : tilesToRelocate) {
        sourceAtlases.insert(tile.first);
      }
      // Only relocate if tiles come from multiple atlases
      shouldRelocate = sourceAtlases.size() > 1;
    }

    // Try to fit tiles into kept atlases or create a new one if needed
    AtlasInfo newAtlas;
    if (shouldRelocate && !tilesToRelocate.empty()) {
      // Create a new atlas for relocated tiles
      int atlasSize = _atlases[atlasesToKeep[0]].width;
      int tileCountPerDim = atlasSize / _tileSize;
      int maxTilesPerAtlas = tileCountPerDim * tileCountPerDim;

      newAtlas.width = atlasSize;
      newAtlas.height = atlasSize;
      newAtlas.surface =
          Surface::Make(_context, atlasSize, atlasSize, true, 1, false, 0, _colorSpace);
      if (newAtlas.surface) {
        newAtlas.image = newAtlas.surface->makeImageSnapshot();
        newAtlas.tileMap.resize(static_cast<size_t>(maxTilesPerAtlas), false);

        // Redraw tiles into the new atlas
        auto newCanvas = newAtlas.surface->getCanvas();
        int newTileIdx = 0;

        for (const auto& oldTile : tilesToRelocate) {
          size_t oldAtlasIdx = oldTile.first;
          int oldTileX = oldTile.second.first;
          int oldTileY = oldTile.second.second;
          DEBUG_ASSERT(newTileIdx >= maxTilesPerAtlas);

          // Get the old tile image
          auto oldTileImage = getAtlasRegionImage(oldAtlasIdx, oldTileX, oldTileY);
          if (oldTileImage) {
            int newTileX = newTileIdx % tileCountPerDim;
            int newTileY = newTileIdx / tileCountPerDim;
            float drawX = newTileX * _tileSize;
            float drawY = newTileY * _tileSize;

            newCanvas->save();
            newCanvas->translate(drawX, drawY);
            newCanvas->drawImage(oldTileImage);
            newCanvas->restore();

            newAtlas.tileMap[static_cast<size_t>(newTileIdx)] = true;

            // Update cache entries that referenced the old tile
            for (auto& cacheEntry : _cacheMap) {
              if (cacheEntry.second.atlasIndex == oldAtlasIdx &&
                  cacheEntry.second.atlasTileX == oldTileX &&
                  cacheEntry.second.atlasTileY == oldTileY) {
                cacheEntry.second.atlasIndex = atlasesToKeep.size();  // Index of the new atlas
                cacheEntry.second.atlasTileX = newTileX;
                cacheEntry.second.atlasTileY = newTileY;
              }
            }

            newTileIdx++;
          }
        }
      }
    }

    // Rebuild atlases list with kept atlases + new compacted atlas
    std::vector<AtlasInfo> compactedAtlases;

    // Add kept atlases with remapped indices
    std::map<size_t, size_t> oldToNewIndex;
    for (size_t newIdx = 0; newIdx < atlasesToKeep.size(); ++newIdx) {
      oldToNewIndex[atlasesToKeep[newIdx]] = newIdx;
      compactedAtlases.push_back(std::move(_atlases[atlasesToKeep[newIdx]]));
    }

    // Build a set of recently-relocated tiles for lookup
    std::set<std::pair<size_t, std::pair<int, int>>> relocatedTiles;
    for (const auto& tile : tilesToRelocate) {
      relocatedTiles.insert(tile);
    }

    // Remove cache entries for tiles in recycled atlases
    auto cacheIt = _cacheMap.begin();
    while (cacheIt != _cacheMap.end()) {
      if (cacheIt->second.atlasIndex != static_cast<size_t>(-1)) {
        size_t atlasIdx = cacheIt->second.atlasIndex;
        if (std::find(atlasesToRecycle.begin(), atlasesToRecycle.end(), atlasIdx) !=
            atlasesToRecycle.end()) {
          // This cache entry points to a recycled atlas
          if (shouldRelocate) {
            // In relocation mode, only remove tiles that were not relocated
            auto tileKey = std::make_pair(
                atlasIdx, std::make_pair(cacheIt->second.atlasTileX, cacheIt->second.atlasTileY));
            if (relocatedTiles.find(tileKey) == relocatedTiles.end()) {
              // This tile was not relocated, remove it
              _currentCacheSize -= cacheIt->second.estimatedSize;
              _accessList.erase(cacheIt->second.accessIterator);
              cacheIt = _cacheMap.erase(cacheIt);
              continue;
            }
          } else {
            // In non-relocation mode, remove all tiles from recycled atlases
            _currentCacheSize -= cacheIt->second.estimatedSize;
            _accessList.erase(cacheIt->second.accessIterator);
            cacheIt = _cacheMap.erase(cacheIt);
            continue;
          }
        }
      }
      ++cacheIt;
    }

    // Remap cache entries pointing to kept atlases
    for (auto& cacheEntry : _cacheMap) {
      if (cacheEntry.second.atlasIndex != static_cast<size_t>(-1)) {
        auto it = oldToNewIndex.find(cacheEntry.second.atlasIndex);
        if (it != oldToNewIndex.end() && it->first != it->second) {
          cacheEntry.second.atlasIndex = it->second;
        }
      }
    }

    // Add the new compacted atlas if it has tiles and relocation was performed
    if (shouldRelocate && !newAtlas.tileMap.empty() && newAtlas.surface) {
      compactedAtlases.push_back(std::move(newAtlas));
    }

    _atlases = std::move(compactedAtlases);
  }
}

}  // namespace tgfx

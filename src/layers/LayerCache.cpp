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
  std::list<const Layer*>::iterator accessIterator;
  uint64_t lastUsedFrame = 0;
  size_t atlasIndex = static_cast<size_t>(-1);  // -1 means not allocated in atlas
  int atlasTileX = -1;
  int atlasTileY = -1;
};

LayerCache::LayerCache(size_t maxCacheSize, std::shared_ptr<ColorSpace> colorSpace)
    : _maxCacheSize(maxCacheSize), _colorSpace(std::move(colorSpace)) {
  calculateAtlasConfiguration();
}

// Calculate cache entry size for atlas tile
inline size_t getCacheEntrySize(int tileSize) {
  return static_cast<size_t>(tileSize * tileSize * 4);
}

LayerCache::~LayerCache() = default;

void LayerCache::setMaxCacheSize(size_t maxSize) {
  if (_maxCacheSize == maxSize) {
    return;
  }
  _maxCacheSize = maxSize;
  clear();
  calculateAtlasConfiguration();
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
  if (!entry.content || entry.content->contentScale() < contentScale) {
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

  // Try to use atlas if the image size fits within a tile
  if (image->width() > _tileSize || image->height() > _tileSize ||
      !allocateAtlasTile(&atlasIndex, &tileX, &tileY)) {
    return;
  }
  DEBUG_ASSERT(atlasIndex < _atlases.size() && _atlases[atlasIndex].surface);
  auto canvas = _atlases[atlasIndex].surface->getCanvas();
  float drawX = static_cast<float>(tileX * _tileSize);
  float drawY = static_cast<float>(tileY * _tileSize);
  canvas->save();
  canvas->translate(drawX, drawY);
  canvas->clipRect(Rect::MakeWH(_tileSize, _tileSize));
  canvas->clear();
  canvas->drawImage(image);
  canvas->restore();
  auto cachedImage = getAtlasRegionImage(atlasIndex, tileX, tileY);
  if (!cachedImage) {
    freeAtlasTile(atlasIndex, tileX, tileY);
    atlasIndex = static_cast<size_t>(-1);
    tileX = -1;
    tileY = -1;
    return;
  }

  auto rasterizedContent = std::make_shared<RasterizedContent>(_context->uniqueID(), contentScale,
                                                               std::move(cachedImage), imageMatrix);

  auto existingIt = _cacheMap.find(layer);
  if (existingIt != _cacheMap.end()) {
    _currentCacheSize -= getCacheEntrySize(_tileSize);
    _accessList.erase(existingIt->second.accessIterator);
    if (existingIt->second.atlasIndex != static_cast<size_t>(-1) &&
        existingIt->second.atlasTileX >= 0 && existingIt->second.atlasTileY >= 0) {
      freeAtlasTile(existingIt->second.atlasIndex, existingIt->second.atlasTileX,
                    existingIt->second.atlasTileY);
      --_usedTileCount;
    }
  }

  CacheEntry entry;
  entry.content = rasterizedContent;
  entry.lastUsedFrame = _frameCounter;
  entry.atlasIndex = atlasIndex;
  entry.atlasTileX = tileX;
  entry.atlasTileY = tileY;

  _cacheMap[layer] = entry;
  _accessList.push_back(layer);
  _cacheMap[layer].accessIterator = std::prev(_accessList.end());
  _currentCacheSize += getCacheEntrySize(_tileSize);
  if (atlasIndex != static_cast<size_t>(-1) && tileX >= 0 && tileY >= 0) {
    ++_usedTileCount;
  }

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
  _currentCacheSize -= getCacheEntrySize(_tileSize);
  _accessList.erase(entry.accessIterator);
  if (entry.atlasIndex != static_cast<size_t>(-1) && entry.atlasTileX >= 0 &&
      entry.atlasTileY >= 0) {
    freeAtlasTile(entry.atlasIndex, entry.atlasTileX, entry.atlasTileY);
    --_usedTileCount;
  }
  _cacheMap.erase(it);
}

void LayerCache::clear() {
  _cacheMap.clear();
  _accessList.clear();
  _currentCacheSize = 0;
  _usedTileCount = 0;
  clearAtlases();
}

void LayerCache::evictLRU() {
  while (_currentCacheSize > _maxCacheSize && !_accessList.empty()) {
    const Layer* lruLayer = _accessList.front();
    auto it = _cacheMap.find(lruLayer);
    if (it != _cacheMap.end()) {
      _currentCacheSize -= getCacheEntrySize(_tileSize);
      _accessList.erase(it->second.accessIterator);
      if (it->second.atlasIndex != static_cast<size_t>(-1) && it->second.atlasTileX >= 0 &&
          it->second.atlasTileY >= 0) {
        freeAtlasTile(it->second.atlasIndex, it->second.atlasTileX, it->second.atlasTileY);
        --_usedTileCount;
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
  if (tileSize <= 0 || tileSize > MAX_ATLAS_SIZE) {
    return;
  }
  if (_tileSize == tileSize) {
    return;
  }
  _tileSize = tileSize;
  clear();
  calculateAtlasConfiguration();
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
      _currentCacheSize -= getCacheEntrySize(_tileSize);
      _accessList.erase(entry.accessIterator);
      if (entry.atlasIndex != static_cast<size_t>(-1) && entry.atlasTileX >= 0 &&
          entry.atlasTileY >= 0) {
        freeAtlasTile(entry.atlasIndex, entry.atlasTileX, entry.atlasTileY);
        --_usedTileCount;
      }
      it = _cacheMap.erase(it);
    } else {
      ++it;
    }
  }
}

void LayerCache::calculateAtlasGridSize(int* width, int* height) {
  // Calculate atlas grid dimensions based on max cache size
  // maxCacheSize determines how many tiles we want to support
  // Each tile takes tileSize*tileSize*4 bytes
  size_t maxTileCount = _maxCacheSize / getCacheEntrySize(_tileSize);

  int maxDim = MAX_ATLAS_SIZE / _tileSize;
  maxTileCount = std::min(static_cast<size_t>(maxDim * maxDim), maxTileCount);

  // Find optimal grid dimensions (width x height) such that width * height >= maxTileCount
  // Prefer nearly-square dimensions, try to have at least 2 atlases
  *width = 1;
  *height = static_cast<int>(maxTileCount);  // Initial: 1 x maxTileCount

  int bestDiff = std::abs(*width - *height);
  int bestAtlasCount = 1;  // This grid fits in 1 atlas

  // Search for dimensions closer to square
  for (int w = 1; w <= maxDim && static_cast<size_t>(w * w) <= maxTileCount * 2; ++w) {
    int h = (static_cast<int>(maxTileCount) + w - 1) / w;  // Ceiling division
    if (h <= maxDim && static_cast<size_t>(w * h) >= maxTileCount) {
      int diff = std::abs(w - h);
      // Count atlases needed: each atlas can hold (w*h) tiles
      // We want at least 2 atlases if possible
      int atlasesNeeded = (w * h + w * h - 1) / (w * h);

      // Prefer more atlases (for parallelism), then prefer square-ish shape
      if (atlasesNeeded > bestAtlasCount || (atlasesNeeded == bestAtlasCount && diff < bestDiff)) {
        *width = w;
        *height = h;
        bestDiff = diff;
        bestAtlasCount = atlasesNeeded;
      }
    }
  }
}

void LayerCache::calculateAtlasConfiguration() {
  if (_tileSize <= 0) {
    return;
  }

  int gridWidth = 1, gridHeight = 1;
  calculateAtlasGridSize(&gridWidth, &gridHeight);

  // Calculate atlas dimensions (can be rectangular)
  int atlasWidth = std::min(MAX_ATLAS_SIZE, gridWidth * _tileSize);
  int atlasHeight = std::min(MAX_ATLAS_SIZE, gridHeight * _tileSize);

  // Check if both dimensions are at least tile size
  if (atlasWidth < _tileSize || atlasHeight < _tileSize) {
    return;
  }

  // Store atlas configuration for lazy initialization
  _atlasWidth = atlasWidth;
  _atlasHeight = atlasHeight;
}

void LayerCache::clearAtlases() {
  _atlases.clear();
}

std::shared_ptr<Image> LayerCache::getAtlasRegionImage(size_t atlasIndex, int tileX, int tileY) {
  if (atlasIndex >= _atlases.size()) {
    return nullptr;
  }
  auto& atlasInfo = _atlases[atlasIndex];
  if (!atlasInfo.surface || tileX < 0 || tileY < 0) {
    return nullptr;
  }
  int tileCountX = atlasInfo.surface->width() / _tileSize;
  int tileCountY = atlasInfo.surface->height() / _tileSize;
  if (tileX >= tileCountX || tileY >= tileCountY) {
    return nullptr;
  }
  // Create a fresh snapshot to get the latest content
  auto image = atlasInfo.surface->makeImageSnapshot();
  if (!image) {
    return nullptr;
  }
  Rect regionRect =
      Rect::MakeXYWH(static_cast<float>(tileX * _tileSize), static_cast<float>(tileY * _tileSize),
                     static_cast<float>(_tileSize), static_cast<float>(_tileSize));
  return image->makeSubset(regionRect);
}

bool LayerCache::allocateAtlasTile(size_t* outAtlasIndex, int* outTileX, int* outTileY) {
  if (_atlasWidth <= 0 || _atlasHeight <= 0) {
    return false;
  }

  // First, try to find an empty slot in existing atlases
  for (size_t atlasIdx = 0; atlasIdx < _atlases.size(); ++atlasIdx) {
    auto& atlasInfo = _atlases[atlasIdx];
    int tileCountX = atlasInfo.surface->width() / _tileSize;
    for (size_t i = 0; i < atlasInfo.tileMap.size(); ++i) {
      if (!atlasInfo.tileMap[i]) {
        atlasInfo.tileMap[i] = true;
        *outAtlasIndex = atlasIdx;
        *outTileY = static_cast<int>(i) / tileCountX;
        *outTileX = static_cast<int>(i) % tileCountX;
        return true;
      }
    }
  }

  // If all existing atlases are full, create a new one
  if (!_context) {
    return false;
  }

  auto newSurface =
      Surface::Make(_context, _atlasWidth, _atlasHeight, false, 1, false, 0, _colorSpace);
  if (!newSurface) {
    return false;
  }

  int tileCountX = _atlasWidth / _tileSize;
  int tileCountY = _atlasHeight / _tileSize;
  int maxTilesPerAtlas = tileCountX * tileCountY;

  AtlasInfo newAtlas;
  newAtlas.surface = std::move(newSurface);
  newAtlas.tileMap.resize(static_cast<size_t>(maxTilesPerAtlas), false);
  newAtlas.tileMap[0] = true;  // Allocate first tile

  *outAtlasIndex = _atlases.size();
  _atlases.push_back(std::move(newAtlas));
  *outTileY = 0;
  *outTileX = 0;
  return true;
}

void LayerCache::freeAtlasTile(size_t atlasIndex, int tileX, int tileY) {
  if (atlasIndex >= _atlases.size()) {
    return;
  }
  auto& atlasInfo = _atlases[atlasIndex];
  int tileCountX = atlasInfo.surface->width() / _tileSize;
  int tileCountY = atlasInfo.surface->height() / _tileSize;
  if (tileX >= 0 && tileY >= 0 && tileX < tileCountX && tileY < tileCountY) {
    size_t index = static_cast<size_t>(tileY * tileCountX + tileX);
    DEBUG_ASSERT(index < atlasInfo.tileMap.size());
    atlasInfo.tileMap[index] = false;
  }
}

bool LayerCache::canContinueCaching() const {
  // Calculate max tile count based on max cache size
  size_t maxTileCount = _maxCacheSize / getCacheEntrySize(_tileSize);
  return _usedTileCount < maxTileCount;
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

    // Check if atlas surface is still in use (use_count > 1 means external references exist)
    // use_count == 1 means only _atlases holds the reference
    if (atlasInfo.surface && atlasInfo.surface.use_count() == 1) {
      // Atlas is not used elsewhere, can be recycled
      atlasesToRecycle.push_back(i);
      continue;
    }

    int usedTiles = 0;
    for (bool tileUsed : atlasInfo.tileMap) {
      if (tileUsed) {
        ++usedTiles;
      }
    }

    float usageRatio = atlasInfo.tileMap.empty() ? 0.0f
                                                 : static_cast<float>(usedTiles) /
                                                       static_cast<float>(atlasInfo.tileMap.size());

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
      int atlasWidth = _atlases[atlasesToKeep[0]].surface->width();
      int atlasHeight = _atlases[atlasesToKeep[0]].surface->height();
      int tileCountX = atlasWidth / _tileSize;
      int tileCountY = atlasHeight / _tileSize;
      int maxTilesPerAtlas = tileCountX * tileCountY;

      newAtlas.surface =
          Surface::Make(_context, atlasWidth, atlasHeight, false, 1, false, 0, _colorSpace);
      if (newAtlas.surface) {
        newAtlas.tileMap.resize(static_cast<size_t>(maxTilesPerAtlas), false);

        // Redraw tiles into the new atlas
        auto newCanvas = newAtlas.surface->getCanvas();
        int newTileIdx = 0;

        for (const auto& oldTile : tilesToRelocate) {
          size_t oldAtlasIdx = oldTile.first;
          int oldTileX = oldTile.second.first;
          int oldTileY = oldTile.second.second;
          DEBUG_ASSERT(newTileIdx < maxTilesPerAtlas);

          // Get the old tile image
          auto oldTileImage = getAtlasRegionImage(oldAtlasIdx, oldTileX, oldTileY);
          if (oldTileImage) {
            int newTileX = newTileIdx % tileCountX;
            int newTileY = newTileIdx / tileCountX;
            float drawX = static_cast<float>(newTileX * _tileSize);
            float drawY = static_cast<float>(newTileY * _tileSize);

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
              _currentCacheSize -= getCacheEntrySize(_tileSize);
              _accessList.erase(cacheIt->second.accessIterator);
              --_usedTileCount;
              cacheIt = _cacheMap.erase(cacheIt);
              continue;
            }
          } else {
            // In non-relocation mode, remove all tiles from recycled atlases
            _currentCacheSize -= getCacheEntrySize(_tileSize);
            _accessList.erase(cacheIt->second.accessIterator);
            --_usedTileCount;
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

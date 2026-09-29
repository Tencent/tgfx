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

#include "TileCache.h"
#include <cmath>
#include <limits>
#include "core/utils/Log.h"
#include "core/utils/TileSortCompareFunc.h"

namespace tgfx {
constexpr int64_t TileKey(int tileX, int tileY) {
  return static_cast<int64_t>(tileX) * (int64_t{1} << 32) + static_cast<uint32_t>(tileY);
}

std::shared_ptr<Tile> TileCache::getTile(int tileX, int tileY) const {
  auto key = TileKey(tileX, tileY);
  auto result = tileMap.find(key);
  return result != tileMap.end() ? result->second : nullptr;
}

std::vector<std::shared_ptr<Tile>> TileCache::getTilesUnderRect(const Rect& rect,
                                                                bool requireFullCoverage,
                                                                bool* continuous) const {
  if (rect.isEmpty()) {
    if (continuous) {
      *continuous = false;
    }
    return {};
  }
  // Keep unclamped bounds so oversized dirty regions still invalidate every intersecting tile.
  auto startTileX = std::floor(static_cast<double>(rect.left) / tileSize);
  auto startTileY = std::floor(static_cast<double>(rect.top) / tileSize);
  auto endTileX = std::ceil(static_cast<double>(rect.right) / tileSize);
  auto endTileY = std::ceil(static_cast<double>(rect.bottom) / tileSize);
  auto requestTileCount = (endTileX - startTileX) * (endTileY - startTileY);
  auto canEnumerate = startTileX >= std::numeric_limits<int>::min() &&
                      startTileY >= std::numeric_limits<int>::min() &&
                      endTileX <= std::numeric_limits<int>::max() &&
                      endTileY <= std::numeric_limits<int>::max();
  std::vector<std::shared_ptr<Tile>> tiles = {};
  if (canEnumerate && requestTileCount < static_cast<double>(tileMap.size())) {
    tiles.reserve(static_cast<size_t>(requestTileCount));
    auto endX = static_cast<int>(endTileX);
    auto endY = static_cast<int>(endTileY);
    for (int tileY = static_cast<int>(startTileY); tileY < endY; ++tileY) {
      for (int tileX = static_cast<int>(startTileX); tileX < endX; ++tileX) {
        auto key = TileKey(tileX, tileY);
        auto result = tileMap.find(key);
        if (result != tileMap.end()) {
          tiles.push_back(result->second);
        }
      }
    }
  } else {
    tiles.reserve(tileMap.size());
    for (auto& item : tileMap) {
      auto& tile = item.second;
      if (tile->tileX >= startTileX && tile->tileX < endTileX && tile->tileY >= startTileY &&
          tile->tileY < endTileY) {
        tiles.push_back(tile);
      }
    }
  }

  auto allFound = static_cast<double>(tiles.size()) == requestTileCount;
  if (requireFullCoverage && !allFound) {
    tiles.clear();
  }
  if (continuous != nullptr) {
    if (allFound && !tiles.empty()) {
      auto firstTile = tiles.front();
      *continuous = true;
      for (auto& tile : tiles) {
        if (static_cast<int64_t>(tile->tileX) - firstTile->tileX !=
                static_cast<int64_t>(tile->sourceX) - firstTile->sourceX ||
            static_cast<int64_t>(tile->tileY) - firstTile->tileY !=
                static_cast<int64_t>(tile->sourceY) - firstTile->sourceY) {
          *continuous = false;
          break;
        }
      }
    } else {
      *continuous = false;
    }
  }
  return tiles;
}

void TileCache::addTile(std::shared_ptr<Tile> tile) {
  auto key = TileKey(tile->tileX, tile->tileY);
  DEBUG_ASSERT(tileMap.find(key) == tileMap.end());
  tileMap[key] = std::move(tile);
}

bool TileCache::removeTile(int tileX, int tileY) {
  auto key = TileKey(tileX, tileY);
  return tileMap.erase(key) > 0;
}

std::vector<std::shared_ptr<Tile>> TileCache::getReusableTiles(float centerX, float centerY) {
  std::vector<std::shared_ptr<Tile>> tiles = {};
  for (auto& item : tileMap) {
    if (item.second.use_count() == 1) {
      tiles.push_back(item.second);
    }
  }
  std::sort(tiles.begin(), tiles.end(),
            [centerX, centerY, tileSize = static_cast<float>(tileSize)](
                const std::shared_ptr<Tile>& a, const std::shared_ptr<Tile>& b) {
              return TileSortCompareFunc({centerX, centerY}, tileSize, {a->tileX, a->tileY},
                                         {b->tileX, b->tileY}, SortOrder::Descending);
            });
  return tiles;
}
}  // namespace tgfx

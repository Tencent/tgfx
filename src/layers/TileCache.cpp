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
#include "../../include/tgfx/core/Log.h"
#include "core/utils/TileSortCompareFunc.h"

namespace tgfx {
constexpr int64_t TileKey(int tileX, int tileY) {
  return (static_cast<int64_t>(tileX) << 32) | static_cast<uint32_t>(tileY);
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
  int startTileX = static_cast<int>(std::floor(rect.left / static_cast<float>(tileSize)));
  int startTileY = static_cast<int>(std::floor(rect.top / static_cast<float>(tileSize)));
  int endTileX = static_cast<int>(std::ceil(rect.right / static_cast<float>(tileSize)));
  int endTileY = static_cast<int>(std::ceil(rect.bottom / static_cast<float>(tileSize)));
  std::vector<std::shared_ptr<Tile>> tiles = {};
  auto requestTileCount =
      static_cast<size_t>(endTileX - startTileX) * static_cast<size_t>(endTileY - startTileY);
  if (requestTileCount < tileMap.size()) {
    tiles.reserve(requestTileCount);
    for (int tileY = startTileY; tileY < endTileY; ++tileY) {
      for (int tileX = startTileX; tileX < endTileX; ++tileX) {
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

  auto allFound = tiles.size() == requestTileCount;
  if (requireFullCoverage && !allFound) {
    tiles.clear();
  }
  if (continuous != nullptr) {
    if (allFound && !tiles.empty()) {
      auto firstTile = tiles.front();
      *continuous = true;
      for (auto& tile : tiles) {
        if (tile->tileX - firstTile->tileX != tile->sourceX - firstTile->sourceX ||
            tile->tileY - firstTile->tileY != tile->sourceY - firstTile->sourceY) {
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

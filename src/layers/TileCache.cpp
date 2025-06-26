/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "core/utils/Log.h"

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
  DEBUG_ASSERT(!rect.isEmpty());
  int startX = static_cast<int>(std::floor(rect.left / static_cast<float>(tileSize)));
  int startY = static_cast<int>(std::floor(rect.top / static_cast<float>(tileSize)));
  int endX = static_cast<int>(std::ceil(rect.right / static_cast<float>(tileSize)));
  int endY = static_cast<int>(std::ceil(rect.bottom / static_cast<float>(tileSize)));
  std::vector<std::shared_ptr<Tile>> tiles = {};
  // Do not use reserve() here, as the input rect may be very large, and we don't want to
  // allocate a large vector that may not be fully filled.
  bool allFound = true;
  for (int tileY = startY; tileY < endY; ++tileY) {
    for (int tileX = startX; tileX < endX; ++tileX) {
      auto key = TileKey(tileX, tileY);
      auto result = tileMap.find(key);
      if (result != tileMap.end()) {
        tiles.push_back(result->second);
      } else {
        allFound = false;
      }
    }
  }
  if (requireFullCoverage && !allFound) {
    tiles.clear();
  }
  if (continuous != nullptr) {
    if (allFound && !tiles.empty()) {
      auto firstTile = tiles.front();
      auto sourceStartX = firstTile->sourceX;
      auto sourceStartY = firstTile->sourceY;
      *continuous = true;
      for (auto& tile : tiles) {
        if (tile->tileX - startX != tile->sourceX - sourceStartX ||
            tile->tileY - startY != tile->sourceY - sourceStartY) {
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
              float dxA = (static_cast<float>(a->tileX) + 0.5f) * tileSize - centerX;
              float dyA = (static_cast<float>(a->tileY) + 0.5f) * tileSize - centerY;
              float dxB = (static_cast<float>(b->tileX) + 0.5f) * tileSize - centerX;
              float dyB = (static_cast<float>(b->tileY) + 0.5f) * tileSize - centerY;
              return dxA * dxA + dyA * dyA < dxB * dxB + dyB * dyB;
            });
  return tiles;
}
}  // namespace tgfx

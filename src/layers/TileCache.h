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

#include <unordered_map>
#include "tgfx/core/Rect.h"

namespace tgfx {
/**
 * Tile represents a single tile in the tile cache.
 */
class Tile {
 public:
  /**
   * Index of the atlas in the cache.
   */
  size_t sourceIndex = 0;
  /**
   * The tile's x-coordinate in the atlas grid.
   */
  int sourceX = 0;
  /**
   * The tile's y-coordinate in the atlas grid.
   */
  int sourceY = 0;
  /**
   * The tile's x-coordinate in the zoomed display list grid.
   */
  int tileX = 0;
  /**
   * The tile's y-coordinate in the zoomed display list grid.
   */
  int tileY = 0;

  /**
   * Returns the source rectangle of the tile in the atlas.
   */
  Rect getSourceRect(int tileSize) const {
    return Rect::MakeXYWH(sourceX * tileSize, sourceY * tileSize, tileSize, tileSize);
  }

  /**
   * Returns the rectangle of the tile in the zoomed display list grid.
   */
  Rect getTileRect(int tileSize, const Rect* clipRect = nullptr) const {
    auto result = Rect::MakeXYWH(tileX * tileSize, tileY * tileSize, tileSize, tileSize);
    if (clipRect != nullptr && !result.intersect(*clipRect)) {
      return Rect::MakeEmpty();
    }
    return result;
  }
};

/**
 * TileCache manages a grid of tiles for rendering.
 */
class TileCache {
 public:
  /**
   * Constructs a TileCache with the specified tile size.
   */
  explicit TileCache(int tileSize) : tileSize(tileSize) {
  }

  bool empty() const {
    return tileMap.empty();
  }

  /**
   * Returns the tile at the specified grid coordinates, or nullptr if it does not exist.
   */
  std::shared_ptr<Tile> getTile(int tileX, int tileY) const;

  /**
   * Returns a vector of tiles that intersect the specified rectangle and removes them from the
   * cache. The rectangle is in the tile cache's coordinate space, without any content offset.
   * @param rect The rectangle to check for tiles.
   * @param requireFullCoverage If true, only returns tiles when the rectangle is fully covered.
   * @param continuous This output parameter is set to true if all tiles in the region exist and
   * their source coordinates (sourceX/sourceY) form a contiguous, aligned block on the surface.
   * @return
   */
  std::vector<std::shared_ptr<Tile>> getTilesUnderRect(const Rect& rect,
                                                       bool requireFullCoverage = false,
                                                       bool* continuous = nullptr) const;

  /**
   * Adds a tile to the grid cache. Asserts that the tile does not already exist in the cache.
   */
  void addTile(std::shared_ptr<Tile> tile);

  /**
   * Removes the tile at the specified grid coordinates from the cache. Returns false if the tile
   * does not exist.
   */
  bool removeTile(int tileX, int tileY);

  /**
   * Returns a list of reusable tiles. These tiles have no external references and are sorted by
   * their distance to the viewport center, with the farthest ones first.
   */
  std::vector<std::shared_ptr<Tile>> getReusableTiles(float centerX, float centerY);

 private:
  int tileSize = 256;
  std::unordered_map<int64_t, std::shared_ptr<Tile>> tileMap = {};
};
}  // namespace tgfx

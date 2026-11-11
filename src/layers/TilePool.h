/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include <cstdint>
#include <limits>
#include <list>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "tgfx/core/Rect.h"

namespace tgfx {
/**
 * Tile represents a single slot of an atlas together with the grid cell it currently renders.
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
   * The tile's x-coordinate in its rasterization grid.
   */
  int tileX = 0;
  /**
   * The tile's y-coordinate in its rasterization grid.
   */
  int tileY = 0;

  /**
   * Returns the source rectangle of the tile in the atlas.
   */
  Rect getSourceRect(int tileSize) const {
    return Rect::MakeXYWH(sourceX * tileSize, sourceY * tileSize, tileSize, tileSize);
  }

  /**
   * Returns the rectangle of the tile in its rasterization grid.
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
 * Grid tiles are rasterized 1:1 at the current zoom scale and become invalid once it changes. Tree
 * tiles are rasterized at a power-of-two scale and stay valid across zoom scales.
 */
enum class TileType { Grid, Tree };

/**
 * The level stored in the keys of grid tiles. Real levels can be negative, so the sentinel has to
 * be a value that no zoom scale can produce.
 */
constexpr int32_t kExactLevel = std::numeric_limits<int32_t>::min();

/**
 * Returns the tree level whose tiles cover (tileSize / 2, tileSize] screen pixels at the given
 * zoom scale. The zoom scale must be greater than zero.
 */
int GetTileLevel(float zoomScale);

/**
 * Returns the scale from layer space to the rasterization grid of the given tree level.
 */
float GetTileRasterScale(int level);

/**
 * TileKey identifies a tile in the pool. Grid tiles use scaleInt, which is never zero for a
 * rendered zoom scale, while tree tiles use level with scaleInt fixed at zero.
 */
struct TileKey {
  int32_t tileX = 0;
  int32_t tileY = 0;
  int32_t level = 0;
  int64_t scaleInt = 0;

  static TileKey MakeGrid(int tileX, int tileY, int64_t scaleInt) {
    return {tileX, tileY, kExactLevel, scaleInt};
  }

  static TileKey MakeTree(int tileX, int tileY, int level) {
    return {tileX, tileY, level, 0};
  }

  TileType type() const {
    return scaleInt != 0 ? TileType::Grid : TileType::Tree;
  }

  bool operator==(const TileKey& other) const {
    return tileX == other.tileX && tileY == other.tileY && level == other.level &&
           scaleInt == other.scaleInt;
  }
};

struct TileKeyHash {
  size_t operator()(const TileKey& key) const;
};

/**
 * TilePool indexes every cached tile in one flat table and keeps a separate LRU list per tile type,
 * so that the two types never evict each other implicitly.
 */
class TilePool {
 public:
  /**
   * Returns the tile for the key without changing its LRU position, or nullptr if there is none.
   */
  std::shared_ptr<Tile> find(const TileKey& key) const;

  /**
   * Adds a tile as the most recently used one of its type.
   */
  void insert(const TileKey& key, std::shared_ptr<Tile> tile);

  /**
   * Marks the tile as the most recently used one of its type. Does nothing if the key is missing.
   */
  void touch(const TileKey& key);

  /**
   * Removes the tile from the pool and returns it, or returns nullptr if the key is missing.
   */
  std::shared_ptr<Tile> erase(const TileKey& key);

  /**
   * Removes up to count least recently used tiles of the given type and returns them. Tiles that
   * are referenced outside the pool are in use by the current frame and stay in the pool.
   */
  std::vector<std::shared_ptr<Tile>> evict(TileType type, size_t count);

  /**
   * Removes every tile of the given type and returns them.
   */
  std::vector<std::shared_ptr<Tile>> clear(TileType type);

  size_t size(TileType type) const {
    return lruOf(type).size();
  }

  /**
   * Returns the keys of the given type, ordered from the most to the least recently used.
   */
  const std::list<TileKey>& keysOf(TileType type) const {
    return lruOf(type);
  }

  size_t quota(TileType type) const {
    return type == TileType::Grid ? gridQuota : treeQuota;
  }

  void setQuota(TileType type, size_t quota) {
    (type == TileType::Grid ? gridQuota : treeQuota) = quota;
  }

 private:
  struct Entry {
    std::shared_ptr<Tile> tile = nullptr;
    std::list<TileKey>::iterator lruIterator = {};
  };

  std::unordered_map<TileKey, Entry, TileKeyHash> entries = {};
  std::list<TileKey> gridLru = {};
  std::list<TileKey> treeLru = {};
  size_t gridQuota = 0;
  size_t treeQuota = 0;

  const std::list<TileKey>& lruOf(TileType type) const {
    return type == TileType::Grid ? gridLru : treeLru;
  }

  std::list<TileKey>& lruOf(TileType type) {
    return type == TileType::Grid ? gridLru : treeLru;
  }
};

/**
 * BlankTileSet remembers the cells whose rasterization produced no draw call, so they can be
 * filled with the background color instead of taking an atlas slot. Grid marks only exist for one
 * zoom scale at a time, while tree marks are grouped by level and stay valid across zoom scales.
 * Marks must be erased whenever the content under them may have changed.
 */
class BlankTileSet {
 public:
  bool contains(const TileKey& key) const;

  /**
   * Adds a mark. A grid key with a different zoom scale drops all existing grid marks first. Once
   * the set is full, the grid marks are dropped first, then the largest tree level.
   */
  void insert(const TileKey& key);

  /**
   * Erases the marks of one grid scale or tree level whose cells intersect the rectangle, given
   * in the rasterization grid of that scale or level.
   */
  void eraseUnderRect(const TileKey& group, const Rect& rect, int tileSize);

  /**
   * Returns the tree levels that hold marks.
   */
  std::vector<int> treeLevels() const;

  int64_t gridScale() const {
    return gridCells.empty() ? 0 : gridScaleInt;
  }

  void clearGrid() {
    count -= gridCells.size();
    gridCells.clear();
  }

  void clear() {
    gridCells.clear();
    treeCells.clear();
    count = 0;
  }

  size_t size() const {
    return count;
  }

  void setMaxCount(size_t maxCount) {
    _maxCount = maxCount;
  }

 private:
  using CellSet = std::unordered_set<int64_t>;

  int64_t gridScaleInt = 0;
  CellSet gridCells = {};
  std::unordered_map<int32_t, CellSet> treeCells = {};
  size_t count = 0;
  size_t _maxCount = std::numeric_limits<size_t>::max();

  void dropGroupForSpace();
};
}  // namespace tgfx

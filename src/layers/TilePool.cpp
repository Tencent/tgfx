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

#include "TilePool.h"
#include <cmath>
#include <functional>
#include "core/utils/Log.h"

namespace tgfx {
int GetTileLevel(float zoomScale) {
  DEBUG_ASSERT(zoomScale > 0.0f);
  return std::ilogb(1.0f / zoomScale);
}

float GetTileRasterScale(int level) {
  // The level can be negative, which rules out a bit shift.
  return std::ldexp(1.0f, -level);
}

size_t TileKeyHash::operator()(const TileKey& key) const {
  auto combine = [](size_t seed, size_t value) -> size_t {
    return seed ^ (value + static_cast<size_t>(0x9e3779b9u) + (seed << 6) + (seed >> 2));
  };
  size_t hash = std::hash<int32_t>()(key.tileX);
  hash = combine(hash, std::hash<int32_t>()(key.tileY));
  hash = combine(hash, std::hash<int32_t>()(key.level));
  return combine(hash, std::hash<int64_t>()(key.scaleInt));
}

std::shared_ptr<Tile> TilePool::find(const TileKey& key) const {
  auto result = entries.find(key);
  return result != entries.end() ? result->second.tile : nullptr;
}

void TilePool::insert(const TileKey& key, std::shared_ptr<Tile> tile) {
  DEBUG_ASSERT(entries.find(key) == entries.end());
  auto& lru = lruOf(key.type());
  auto result = entries.find(key);
  if (result != entries.end()) {
    result->second.tile = std::move(tile);
    lru.splice(lru.begin(), lru, result->second.lruIterator);
    return;
  }
  lru.push_front(key);
  entries[key] = {std::move(tile), lru.begin()};
}

void TilePool::touch(const TileKey& key) {
  auto result = entries.find(key);
  if (result == entries.end()) {
    return;
  }
  auto& lru = lruOf(key.type());
  lru.splice(lru.begin(), lru, result->second.lruIterator);
}

std::shared_ptr<Tile> TilePool::erase(const TileKey& key) {
  auto result = entries.find(key);
  if (result == entries.end()) {
    return nullptr;
  }
  lruOf(key.type()).erase(result->second.lruIterator);
  auto tile = std::move(result->second.tile);
  entries.erase(result);
  return tile;
}

std::vector<std::shared_ptr<Tile>> TilePool::evict(TileType type, size_t count) {
  std::vector<std::shared_ptr<Tile>> tiles = {};
  auto& lru = lruOf(type);
  auto iterator = lru.end();
  while (iterator != lru.begin() && tiles.size() < count) {
    --iterator;
    auto result = entries.find(*iterator);
    DEBUG_ASSERT(result != entries.end());
    if (result->second.tile.use_count() > 1) {
      continue;
    }
    tiles.push_back(std::move(result->second.tile));
    entries.erase(result);
    iterator = lru.erase(iterator);
  }
  return tiles;
}

std::vector<std::shared_ptr<Tile>> TilePool::clear(TileType type) {
  std::vector<std::shared_ptr<Tile>> tiles = {};
  auto& lru = lruOf(type);
  tiles.reserve(lru.size());
  for (auto& key : lru) {
    auto result = entries.find(key);
    DEBUG_ASSERT(result != entries.end());
    tiles.push_back(std::move(result->second.tile));
    entries.erase(result);
  }
  lru.clear();
  return tiles;
}

static int64_t PackCell(int32_t tileX, int32_t tileY) {
  return static_cast<int64_t>((static_cast<uint64_t>(static_cast<uint32_t>(tileX)) << 32) |
                              static_cast<uint32_t>(tileY));
}

bool BlankTileSet::contains(const TileKey& key) const {
  auto cell = PackCell(key.tileX, key.tileY);
  if (key.type() == TileType::Grid) {
    return key.scaleInt == gridScaleInt && gridCells.count(cell) > 0;
  }
  auto result = treeCells.find(key.level);
  return result != treeCells.end() && result->second.count(cell) > 0;
}

void BlankTileSet::insert(const TileKey& key) {
  if (key.type() == TileType::Grid && key.scaleInt != gridScaleInt) {
    clearGrid();
    gridScaleInt = key.scaleInt;
  }
  if (count >= _maxCount) {
    dropGroupForSpace();
  }
  auto& cells = key.type() == TileType::Grid ? gridCells : treeCells[key.level];
  if (cells.insert(PackCell(key.tileX, key.tileY)).second) {
    count++;
  }
}

void BlankTileSet::dropGroupForSpace() {
  if (!gridCells.empty()) {
    clearGrid();
    return;
  }
  auto largest = treeCells.end();
  for (auto iter = treeCells.begin(); iter != treeCells.end(); ++iter) {
    if (largest == treeCells.end() || iter->second.size() > largest->second.size()) {
      largest = iter;
    }
  }
  if (largest != treeCells.end()) {
    count -= largest->second.size();
    treeCells.erase(largest);
  }
}

void BlankTileSet::eraseUnderRect(const TileKey& group, const Rect& rect, int tileSize) {
  CellSet* cells = nullptr;
  auto treeGroup = treeCells.end();
  if (group.type() == TileType::Grid) {
    if (group.scaleInt != gridScaleInt) {
      return;
    }
    cells = &gridCells;
  } else {
    treeGroup = treeCells.find(group.level);
    if (treeGroup == treeCells.end()) {
      return;
    }
    cells = &treeGroup->second;
  }
  if (cells->empty() || rect.isEmpty()) {
    return;
  }
  auto tileSizeFloat = static_cast<float>(tileSize);
  auto startX = static_cast<int64_t>(std::floor(rect.left / tileSizeFloat));
  auto startY = static_cast<int64_t>(std::floor(rect.top / tileSizeFloat));
  auto endX = static_cast<int64_t>(std::ceil(rect.right / tileSizeFloat));
  auto endY = static_cast<int64_t>(std::ceil(rect.bottom / tileSizeFloat));
  auto before = cells->size();
  if (static_cast<uint64_t>((endX - startX) * (endY - startY)) > before) {
    for (auto iter = cells->begin(); iter != cells->end();) {
      auto tileX = static_cast<int32_t>(static_cast<uint64_t>(*iter) >> 32);
      auto tileY = static_cast<int32_t>(static_cast<uint32_t>(*iter));
      if (tileX >= startX && tileX < endX && tileY >= startY && tileY < endY) {
        iter = cells->erase(iter);
      } else {
        ++iter;
      }
    }
  } else {
    for (auto tileY = startY; tileY < endY; ++tileY) {
      for (auto tileX = startX; tileX < endX; ++tileX) {
        cells->erase(PackCell(static_cast<int32_t>(tileX), static_cast<int32_t>(tileY)));
      }
    }
  }
  count -= before - cells->size();
  if (treeGroup != treeCells.end() && cells->empty()) {
    treeCells.erase(treeGroup);
  }
}

std::vector<int> BlankTileSet::treeLevels() const {
  std::vector<int> levels = {};
  levels.reserve(treeCells.size());
  for (auto& item : treeCells) {
    levels.push_back(item.first);
  }
  return levels;
}
}  // namespace tgfx

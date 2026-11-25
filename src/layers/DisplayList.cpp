/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "tgfx/layers/DisplayList.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "core/utils/DecomposeRects.h"
#include "core/utils/GpuProbe.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/TileSortCompareFunc.h"
#include "gpu/DrawingManager.h"
#include "layers/BackgroundHandler.h"
#include "layers/BackgroundSnapshotMap.h"
#include "layers/BackgroundSource.h"
#include "layers/CaptureCulling.h"
#include "layers/DrawArgs.h"
#include "layers/RootLayer.h"
#include "layers/StyleOutputCache.h"
#include "layers/TilePool.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {
static constexpr size_t MAX_DIRTY_REGION_FRAMES = 5;
static constexpr float DIRTY_REGION_ANTIALIAS_MARGIN = 0.5f;
static constexpr int MIN_TILE_SIZE = 16;
static constexpr int MAX_TILE_SIZE = 2048;
static constexpr int MAX_ATLAS_SIZE = 4096;
// Bounds the tile memory at roughly 2 GB (32 atlases of 64 MB). Every display resolution fits,
// including 8K, so the bound only stops a surface far larger than any display from exhausting it.
static constexpr int MAX_ATLAS_COUNT = 32;
// Covering a viewport with the tree levels L to L+3 takes 1 + 1/4 + 1/16 + 1/64 times the tiles of
// level L.
static constexpr float TREE_LEVELS_COVERAGE_FACTOR = 1.33f;
static constexpr int TREE_LEVEL_COUNT = 4;
static constexpr size_t BLANK_MARKS_PER_TILE = 4;
// Number of rects the per-layer cull loop can afford.
static constexpr size_t MAX_CULL_RECTS = 32;

static bool captureCullingEnabledForTests = true;

bool CaptureCullingEnabledForTests() {
  return captureCullingEnabledForTests;
}

void SetCaptureCullingEnabledForTests(bool enabled) {
  captureCullingEnabledForTests = enabled;
}

class DrawTask {
 public:
  DrawTask(std::shared_ptr<Tile> tile, int tileSize, const Rect& drawRect = {}, float scale = 1.0f)
      : tiles({std::move(tile)}), _identityScale(scale == 1.0f) {
    calculateRects(tileSize, drawRect, scale);
  }

  DrawTask(std::vector<std::shared_ptr<Tile>> tiles, int tileSize, const Rect& drawRect)
      : tiles(std::move(tiles)) {
    DEBUG_ASSERT(!drawRect.isEmpty());
    calculateRects(tileSize, drawRect, 1.0f);
  }

  /**
   * Returns the index of the atlas in the surface caches.
   */
  size_t sourceIndex() const {
    return tiles.front()->sourceIndex;
  }

  /**
   * Returns the source rectangle of the tile in the atlas.
   */
  const Rect& sourceRect() const {
    return _sourceRect;
  }

  /**
   * Returns the rectangle of the tile in the zoomed display list grid.
   */
  const Rect& tileRect() const {
    return _tileRect;
  }

  const Rect& strictRect() const {
    return _strictRect;
  }

  const std::vector<std::shared_ptr<Tile>>& getTiles() const {
    return tiles;
  }

  bool identityScale() const {
    return _identityScale;
  }

  /**
   * Returns the level of the tree tiles this task rasterizes, or EXACT_LEVEL if it rasterizes grid
   * tiles at the current zoom scale.
   */
  int treeLevel() const {
    return _treeLevel;
  }

  void setTreeLevel(int level) {
    _treeLevel = level;
  }

  /**
   * Returns true if the task rasterizes the whole area of its tiles, which is required to judge
   * them blank.
   */
  bool coversTiles() const {
    return _coversTiles;
  }

  void setCoversTiles(bool coversTiles) {
    _coversTiles = coversTiles;
  }

  /**
   * Returns true if the task redraws tiles whose layers drew subtree caches of other sizes, so it
   * must build the caches it needs whatever the time budget.
   */
  bool requiresExactCaches() const {
    return _requiresExactCaches;
  }

  void setRequiresExactCaches(bool requiresExactCaches) {
    _requiresExactCaches = requiresExactCaches;
  }

 private:
  // Hold strong references to tiles to ensure the pool doesn't evict them.
  std::vector<std::shared_ptr<Tile>> tiles = {};
  Rect _sourceRect = {};
  Rect _tileRect = {};
  Rect _strictRect = {};
  bool _identityScale = true;
  bool _coversTiles = false;
  bool _requiresExactCaches = false;
  int _treeLevel = EXACT_LEVEL;

  void calculateRects(int tileSize, const Rect& drawRect, float scale) {
    DEBUG_ASSERT(!tiles.empty());
    DEBUG_ASSERT(std::all_of(tiles.begin(), tiles.end(), [&](const std::shared_ptr<Tile>& t) {
      return t->sourceIndex == tiles.front()->sourceIndex;
    }));
    auto& tile = tiles.front();
    _tileRect = drawRect.isEmpty() ? tile->getTileRect(tileSize) : drawRect;
    _sourceRect = _tileRect;
    auto offsetX = (tile->sourceX - tile->tileX) * tileSize;
    auto offsetY = (tile->sourceY - tile->tileY) * tileSize;
    _sourceRect.offset(static_cast<float>(offsetX), static_cast<float>(offsetY));
    _tileRect.scale(scale, scale);
    _strictRect = tile->getSourceRect(tileSize);
  }
};

static std::vector<Rect> GetNonIntersectingRects(const Rect& a, const Rect& b) {
  std::vector<Rect> rects = {};
  auto leftRect = Rect::MakeLTRB(a.left, a.top, b.left, b.bottom);
  if (!leftRect.isEmpty()) {
    rects.emplace_back(leftRect);
  }
  auto topRect = Rect::MakeLTRB(b.left, a.top, a.right, b.top);
  if (!topRect.isEmpty()) {
    rects.emplace_back(topRect);
  }
  auto rightRect = Rect::MakeLTRB(b.right, b.top, a.right, a.bottom);
  if (!rightRect.isEmpty()) {
    rects.emplace_back(rightRect);
  }
  auto bottomRect = Rect::MakeLTRB(a.left, b.bottom, b.right, a.bottom);
  if (!bottomRect.isEmpty()) {
    rects.emplace_back(bottomRect);
  }
  return rects;
}

static float ToZoomScaleFloat(int64_t zoomScaleInt, int64_t precision) {
  if (zoomScaleInt < 0) {
    return static_cast<float>(precision) / static_cast<float>(-zoomScaleInt);
  }
  return static_cast<float>(zoomScaleInt) / static_cast<float>(precision);
}

static int64_t ToZoomScaleInt(float zoomScaleFloat, int64_t precision) {
  if (zoomScaleFloat < 0.0f || FloatNearlyZero(zoomScaleFloat)) {
    return 0;
  }
  if (zoomScaleFloat < 1.0f) {
    return -static_cast<int64_t>(std::roundf(static_cast<float>(precision) / zoomScaleFloat));
  }
  return static_cast<int64_t>(std::roundf(static_cast<float>(precision) * zoomScaleFloat));
}

static int64_t ChangeZoomScalePrecision(int64_t zoomScaleInt, int oldPrecision, int newPrecision) {
  return static_cast<int64_t>(
      std::roundf(static_cast<float>(zoomScaleInt) * static_cast<float>(newPrecision) /
                  static_cast<float>(oldPrecision)));
}

static std::vector<std::pair<int, int>> GenerateGridTiles(int startX, int endX, int startY,
                                                          int endY) {
  std::vector<std::pair<int, int>> tiles;
  for (auto tileY = startY; tileY < endY; ++tileY) {
    for (auto tileX = startX; tileX < endX; ++tileX) {
      tiles.emplace_back(tileX, tileY);
    }
  }
  return tiles;
}

static std::vector<std::pair<int, int>> GetSortedTiles(int startX, int endX, int startY, int endY,
                                                       int tileSize, const Point& mousePosition) {

  auto tiles = GenerateGridTiles(startX, endX, startY, endY);
  std::sort(tiles.begin(), tiles.end(),
            [&mousePosition, tileSize = static_cast<float>(tileSize)](
                const std::pair<int, int>& a, const std::pair<int, int>& b) {
              return TileSortCompareFunc(mousePosition, tileSize, a, b);
            });
  return tiles;
}

/**
 * The tree tiles of one level inside the viewport. The references also keep the pool from evicting
 * these tiles while the current frame is being assembled.
 */
struct TreeLevelCoverage {
  int level = 0;
  float zoomToRaster = 1.0f;
  int startX = 0;
  int startY = 0;
  int countX = 0;
  int countY = 0;
  std::vector<std::shared_ptr<Tile>> tiles = {};
  std::vector<bool> blanks = {};

  size_t indexOf(int tileX, int tileY) const {
    return static_cast<size_t>((tileY - startY) * countX + tileX - startX);
  }

  std::shared_ptr<Tile>& tileAt(int tileX, int tileY) {
    return tiles[indexOf(tileX, tileY)];
  }

  bool isBlank(int tileX, int tileY) const {
    return blanks[indexOf(tileX, tileY)];
  }

  bool isCovered(int tileX, int tileY) const {
    auto index = indexOf(tileX, tileY);
    return tiles[index] != nullptr || blanks[index];
  }

  // Returns the rectangle in this level's rasterization grid.
  Rect toRaster(const Rect& zoomRect) const {
    auto rect = zoomRect;
    rect.scale(zoomToRaster, zoomToRaster);
    return rect;
  }
};

static void ForEachTileUnderRect(const Rect& rect, int tileSize,
                                 const std::function<bool(int, int)>& visitor) {
  auto tileSizeFloat = static_cast<float>(tileSize);
  auto startX = FloatFloorToInt(rect.left / tileSizeFloat);
  auto startY = FloatFloorToInt(rect.top / tileSizeFloat);
  auto endX = FloatCeilToInt(rect.right / tileSizeFloat);
  auto endY = FloatCeilToInt(rect.bottom / tileSizeFloat);
  for (auto tileY = startY; tileY < endY; ++tileY) {
    for (auto tileX = startX; tileX < endX; ++tileX) {
      if (!visitor(tileX, tileY)) {
        return;
      }
    }
  }
}

// Sizes the coverage to the tiles of its raster grid under the zoomed rect.
static void InitCoverage(TreeLevelCoverage* coverage, const Rect& renderRect, int tileSize) {
  auto tileSizeFloat = static_cast<float>(tileSize);
  auto rasterRect = coverage->toRaster(renderRect);
  coverage->startX = FloatFloorToInt(rasterRect.left / tileSizeFloat);
  coverage->startY = FloatFloorToInt(rasterRect.top / tileSizeFloat);
  coverage->countX = FloatCeilToInt(rasterRect.right / tileSizeFloat) - coverage->startX;
  coverage->countY = FloatCeilToInt(rasterRect.bottom / tileSizeFloat) - coverage->startY;
  auto cellCount = static_cast<size_t>(coverage->countX * coverage->countY);
  coverage->tiles.resize(cellCount);
  coverage->blanks.resize(cellCount, false);
}

// Looks up the grid tiles of the stale zoom scale inside the viewport, as a coverage the display
// falls back to when no tree level covers a cell. Returns an empty list when there are none.
static std::vector<TreeLevelCoverage> CollectStaleGridCoverage(
    const TilePool* tilePool, int64_t staleScaleInt, float staleZoomScale, float zoomScale,
    const Rect& renderRect, int tileSize) {
  if (staleScaleInt == 0 || tilePool->size(TileType::Grid) == 0) {
    return {};
  }
  TreeLevelCoverage coverage = {};
  coverage.zoomToRaster = staleZoomScale / zoomScale;
  InitCoverage(&coverage, renderRect, tileSize);
  bool found = false;
  for (auto tileY = coverage.startY; tileY < coverage.startY + coverage.countY; ++tileY) {
    for (auto tileX = coverage.startX; tileX < coverage.startX + coverage.countX; ++tileX) {
      if (auto tile = tilePool->find(TileKey::MakeGrid(tileX, tileY, staleScaleInt))) {
        coverage.tileAt(tileX, tileY) = std::move(tile);
        found = true;
      }
    }
  }
  if (!found) {
    return {};
  }
  std::vector<TreeLevelCoverage> coverages = {};
  coverages.push_back(std::move(coverage));
  return coverages;
}

// Looks up every tile of the levels L to L+levelCount-1, capped at maxLevel, inside the viewport.
// Hits are touched regardless of whether the frame displays them, which is what keeps the coarse
// levels from being evicted.
static std::vector<TreeLevelCoverage> CollectTreeCoverages(TilePool* tilePool,
                                                           const BlankTileSet* blankTiles,
                                                           float zoomScale, const Rect& renderRect,
                                                           int tileSize, int maxLevel,
                                                           int levelCount = TREE_LEVEL_COUNT) {
  std::vector<TreeLevelCoverage> coverages = {};
  coverages.reserve(static_cast<size_t>(std::max(levelCount, 0)));
  auto baseLevel = GetTileLevel(zoomScale);
  for (int i = 0; i < levelCount && baseLevel + i <= maxLevel; ++i) {
    TreeLevelCoverage coverage = {};
    coverage.level = baseLevel + i;
    coverage.zoomToRaster = GetTileRasterScale(coverage.level) / zoomScale;
    InitCoverage(&coverage, renderRect, tileSize);
    for (auto tileY = coverage.startY; tileY < coverage.startY + coverage.countY; ++tileY) {
      for (auto tileX = coverage.startX; tileX < coverage.startX + coverage.countX; ++tileX) {
        auto key = TileKey::MakeTree(tileX, tileY, coverage.level);
        auto tile = tilePool->find(key);
        if (tile != nullptr) {
          tilePool->touch(key);
          coverage.tileAt(tileX, tileY) = std::move(tile);
        } else if (blankTiles->contains(key)) {
          coverage.blanks[coverage.indexOf(tileX, tileY)] = true;
        }
      }
    }
    coverages.push_back(std::move(coverage));
  }
  return coverages;
}

static TreeLevelCoverage* FindFinestCoverage(std::vector<TreeLevelCoverage>* coverages,
                                             const Rect& zoomRect, int tileSize) {
  for (auto& coverage : *coverages) {
    bool covered = true;
    ForEachTileUnderRect(coverage.toRaster(zoomRect), tileSize, [&](int tileX, int tileY) {
      covered = coverage.isCovered(tileX, tileY);
      return covered;
    });
    if (covered) {
      return &coverage;
    }
  }
  return nullptr;
}

// Returns the tiles of a level that are neither cached nor blank under any of the given zoomed
// rects, each listed once, in the raster grid of that level.
static std::vector<std::pair<int, int>> CollectMissingTreeTiles(const TreeLevelCoverage& coverage,
                                                                const std::vector<Rect>& zoomRects,
                                                                int tileSize) {
  std::vector<std::pair<int, int>> cells = {};
  std::unordered_set<size_t> requested = {};
  for (auto& rect : zoomRects) {
    ForEachTileUnderRect(coverage.toRaster(rect), tileSize, [&](int tileX, int tileY) {
      if (!coverage.isCovered(tileX, tileY) &&
          requested.insert(coverage.indexOf(tileX, tileY)).second) {
        cells.emplace_back(tileX, tileY);
      }
      return true;
    });
  }
  return cells;
}

// Blank cells go to backgroundRects, in the zoomed display list grid.
static void AppendFallbackScreenTasks(TreeLevelCoverage* coverage, const Rect& zoomRect,
                                      int tileSize, std::vector<DrawTask>* tasks,
                                      std::vector<Rect>* backgroundRects) {
  auto rasterRect = coverage->toRaster(zoomRect);
  auto displayScale = 1.0f / coverage->zoomToRaster;
  ForEachTileUnderRect(rasterRect, tileSize, [&](int tileX, int tileY) {
    auto drawRect = Rect::MakeXYWH(tileX * tileSize, tileY * tileSize, tileSize, tileSize);
    if (!drawRect.intersect(rasterRect)) {
      return true;
    }
    if (coverage->isBlank(tileX, tileY)) {
      drawRect.scale(displayScale, displayScale);
      backgroundRects->push_back(drawRect);
      ++gCaptureProbe.blankTreeCells;
    } else {
      tasks->emplace_back(coverage->tileAt(tileX, tileY), tileSize, drawRect, displayScale);
    }
    return true;
  });
}

static std::vector<std::shared_ptr<Tile>> GetGridTilesUnderRect(const TilePool* tilePool,
                                                                int64_t scaleInt, int tileSize,
                                                                const Rect& rect,
                                                                bool* continuous) {
  *continuous = false;
  if (rect.isEmpty()) {
    return {};
  }
  auto tileSizeFloat = static_cast<float>(tileSize);
  auto startX = FloatFloorToInt(rect.left / tileSizeFloat);
  auto startY = FloatFloorToInt(rect.top / tileSizeFloat);
  auto endX = FloatCeilToInt(rect.right / tileSizeFloat);
  auto endY = FloatCeilToInt(rect.bottom / tileSizeFloat);
  auto requestCount = static_cast<size_t>(endX - startX) * static_cast<size_t>(endY - startY);
  std::vector<std::shared_ptr<Tile>> tiles = {};
  if (requestCount < tilePool->size(TileType::Grid)) {
    tiles.reserve(requestCount);
    for (auto tileY = startY; tileY < endY; ++tileY) {
      for (auto tileX = startX; tileX < endX; ++tileX) {
        auto tile = tilePool->find(TileKey::MakeGrid(tileX, tileY, scaleInt));
        if (tile != nullptr) {
          tiles.push_back(std::move(tile));
        }
      }
    }
  } else {
    for (auto& key : tilePool->keysOf(TileType::Grid)) {
      if (key.scaleInt == scaleInt && key.tileX >= startX && key.tileX < endX &&
          key.tileY >= startY && key.tileY < endY) {
        tiles.push_back(tilePool->find(key));
      }
    }
  }
  if (tiles.size() == requestCount && !tiles.empty()) {
    auto& first = tiles.front();
    *continuous = std::all_of(tiles.begin(), tiles.end(), [&](const std::shared_ptr<Tile>& tile) {
      return tile->sourceIndex == first->sourceIndex &&
             tile->tileX - first->tileX == tile->sourceX - first->sourceX &&
             tile->tileY - first->tileY == tile->sourceY - first->sourceY;
    });
  }
  return tiles;
}

static void GetAtlasTileGrid(int tileCount, int* countX, int* countY) {
  *countX = FloatCeilToInt(sqrtf(static_cast<float>(tileCount)));
  *countY = FloatCeilToInt(static_cast<float>(tileCount) / static_cast<float>(*countX));
}

DisplayList::DisplayList()
    : _root(RootLayer::Make()), tilePool(std::make_unique<TilePool>()),
      blankTiles(std::make_unique<BlankTileSet>()) {
  _root->_root = _root.get();
}

DisplayList::~DisplayList() {
  resetCaches();
}

Layer* DisplayList::root() const {
  return _root.get();
}

float DisplayList::zoomScale() const {
  return ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision);
}

void DisplayList::setZoomScale(float zoomScale) {
  auto zoomScaleInt = ToZoomScaleInt(zoomScale, _zoomScalePrecision);
  if (_zoomScaleInt == zoomScaleInt) {
    return;
  }
  _hasContentChanged = true;
  _zoomScaleInt = zoomScaleInt;
}

void DisplayList::setZoomScalePrecision(int precision) {
  if (precision < 1) {
    precision = 1;
  }
  if (_zoomScalePrecision == precision) {
    return;
  }
  auto oldPrecision = _zoomScalePrecision;
  _zoomScalePrecision = precision;
  _zoomScaleInt = ChangeZoomScalePrecision(_zoomScaleInt, oldPrecision, precision);
  lastZoomScaleInt = ChangeZoomScalePrecision(lastZoomScaleInt, oldPrecision, precision);
  staleGridScaleInt = 0;
  recycleGridTiles();
}

void DisplayList::setContentOffset(float offsetX, float offsetY) {
  // Round to integer pixels to avoid subpixel positioning artifacts.
  offsetX = roundf(offsetX);
  offsetY = roundf(offsetY);
  if (offsetX == _contentOffset.x && offsetY == _contentOffset.y) {
    return;
  }
  _hasContentChanged = true;
  _contentOffset.x = offsetX;
  _contentOffset.y = offsetY;
}

void DisplayList::setRenderMode(RenderMode renderMode) {
  if (_renderMode == renderMode) {
    return;
  }
  if (_renderMode != RenderMode::Direct) {
    resetCaches();
  }
  hasZoomBlurTiles = false;
  _renderMode = renderMode;
}

void DisplayList::setTileSize(int tileSize) {
  if (tileSize < MIN_TILE_SIZE) {
    tileSize = MIN_TILE_SIZE;
  }
  if (tileSize > MAX_TILE_SIZE) {
    tileSize = MAX_TILE_SIZE;
  }
  if (tileSize & (tileSize - 1)) {
    // Ensure tileSize is a power of two.
    tileSize = 1 << static_cast<int>(std::log2(tileSize));
  }
  if (_tileSize == tileSize) {
    return;
  }
  _tileSize = tileSize;
  if (_renderMode == RenderMode::Tiled) {
    resetCaches();
  }
}

void DisplayList::setBackgroundColor(const Color& color) {
  if (_backgroundColor == color) {
    return;
  }
  _backgroundColor = color;
  _root->invalidateContent();
  resetCaches();
}

void DisplayList::setSubtreeCacheMaxSize(int maxSize) {
  if (maxSize < 0) {
    maxSize = 0;
  }
  _subtreeCacheMaxSize = maxSize;
}

void DisplayList::showDirtyRegions(bool show) {
  if (_showDirtyRegions == show) {
    return;
  }
  _showDirtyRegions = show;
  if (!_showDirtyRegions) {
    lastDirtyRegions = {};
  }
}

bool DisplayList::hasContentChanged() const {
  if (_hasContentChanged || hasZoomBlurTiles || _root->bitFields.dirtyDescendents) {
    return true;
  }
  if (!_showDirtyRegions) {
    return false;
  }
  for (auto& regions : lastDirtyRegions) {
    if (!regions.empty()) {
      return true;
    }
  }
  return false;
}

void DisplayList::render(Surface* surface, bool autoClear) {
  if (!surface) {
    return;
  }
  _hasContentChanged = false;
  auto dirtyRegions = _root->updateDirtyRegions();
  if (_zoomScaleInt == 0) {
    if (autoClear) {
      auto canvas = surface->getCanvas();
      canvas->clear();
    }
    return;
  }
  switch (_renderMode) {
    case RenderMode::Direct:
      dirtyRegions = renderDirect(surface, autoClear);
      break;
    case RenderMode::Partial:
      dirtyRegions = renderPartial(surface, autoClear, dirtyRegions);
      break;
    case RenderMode::Tiled:
      dirtyRegions = renderTiled(surface, autoClear, dirtyRegions);
      break;
  }
  if (_showDirtyRegions) {
    renderDirtyRegions(surface->getCanvas(), std::move(dirtyRegions));
  }
  _root->updateStaticSubtreeFlags();
}

std::vector<Rect> DisplayList::renderDirect(Surface* surface, bool autoClear) const {
  auto surfaceRect = Rect::MakeWH(surface->width(), surface->height());
  std::unique_ptr<BackgroundSnapshotMap> snapshotMap = nullptr;
  if (_root->hasBackgroundStyle(1.0f)) {
    snapshotMap = captureBackgrounds(surface, {surfaceRect}, getViewMatrix());
  }
  drawRootLayer(surface, surfaceRect, getViewMatrix(), autoClear, snapshotMap.get());
  return {Rect::MakeEmpty()};
}

static std::vector<Rect> MapDirtyRegions(const std::vector<Rect>& dirtyRegions,
                                         const Matrix& viewMatrix, bool decompose = false,
                                         const Rect* clipRect = nullptr) {
  std::vector<Rect> dirtyRects = {};
  dirtyRects.reserve(dirtyRegions.size());
  for (auto& region : dirtyRegions) {
    auto dirtyRect = viewMatrix.mapRect(region);
    if (dirtyRect.isEmpty()) {
      continue;
    }
    // Expand by 0.5 pixels to preserve antialiasing results.
    dirtyRect.outset(DIRTY_REGION_ANTIALIAS_MARGIN, DIRTY_REGION_ANTIALIAS_MARGIN);
    // Snap to pixel boundaries to avoid subpixel clipping artifacts.
    dirtyRect.roundOut();
    if (!clipRect || dirtyRect.intersect(*clipRect)) {
      dirtyRects.push_back(dirtyRect);
    }
  }
  if (decompose) {
    DecomposeRects(dirtyRects.data(), dirtyRects.size());
  }
  return dirtyRects;
}

// Returns the viewport rectangle in zoom space, i.e. the surface rect before the content offset
// is applied.
static Rect GetViewportRect(const Surface* surface, const Point& contentOffset) {
  auto rect = Rect::MakeWH(surface->width(), surface->height());
  rect.offset(-contentOffset.x, -contentOffset.y);
  return rect;
}
std::vector<Rect> DisplayList::renderPartial(Surface* surface, bool autoClear,
                                             const std::vector<Rect>& dirtyRegions) {
  auto context = surface->getContext();
  bool cacheChanged = false;
  auto partialCache = surfaceCaches.empty() ? nullptr : surfaceCaches.front();
  if (partialCache == nullptr || partialCache->getContext() != context ||
      partialCache->width() != surface->width() || partialCache->height() != surface->height() ||
      !ColorSpace::Equals(partialCache->colorSpace().get(), surface->colorSpace().get())) {
    surfaceCaches.clear();
    partialCache = Surface::Make(context, surface->width(), surface->height(), ColorType::RGBA_8888,
                                 1, false, surface->renderFlags(), surface->colorSpace());
    if (partialCache == nullptr) {
      LOGE("DisplayList::renderPartial: Failed to create partial cache surface.");
      return renderDirect(surface, autoClear);
    }
    surfaceCaches.push_back(partialCache);
    cacheChanged = true;
  }
  auto viewMatrix = getViewMatrix();
  auto surfaceRect = Rect::MakeWH(surface->width(), surface->height());
  std::vector<Rect> renderRects = {};
  if (cacheChanged || lastZoomScaleInt != _zoomScaleInt || lastContentOffset != _contentOffset) {
    renderRects = {surfaceRect};
    lastZoomScaleInt = _zoomScaleInt;
    lastContentOffset = _contentOffset;
  } else {
    renderRects = MapDirtyRegions(dirtyRegions, viewMatrix, true, &surfaceRect);
  }
  // Pass the full rect list so each replay is scoped to a single rect; otherwise Layers in the
  // gaps between scattered dirty regions get skipped.
  std::unique_ptr<BackgroundSnapshotMap> snapshotMap = nullptr;
  if (_root->hasBackgroundStyle(1.0f)) {
    snapshotMap = captureBackgrounds(surface, renderRects, viewMatrix);
  }
  auto canvas = surface->getCanvas();
  for (auto& drawRect : renderRects) {
    drawRootLayer(partialCache.get(), drawRect, viewMatrix, true, snapshotMap.get());
  }
  AutoCanvasRestore restore(canvas);
  canvas->resetMatrix();
  Paint paint = {};
  paint.setAntiAlias(false);
  if (autoClear) {
    paint.setBlendMode(BlendMode::Src);
  }
  static SamplingOptions sampling(FilterMode::Nearest);
  canvas->drawImage(partialCache->makeImageSnapshot(), sampling, &paint);
  return renderRects;
}

// Walks the subtree a tile draws and reports whether any background style cannot read its backdrop
// from the tile: offscreen composites (blend modes, group alpha, filters, masks, 3D) do not write
// to the tile, and a style whose sample area leaves the tile or meets an earlier style output has
// no valid backdrop there either.
std::vector<Rect> DisplayList::renderTiled(Surface* surface, bool autoClear,
                                           const std::vector<Rect>& dirtyRegions) {
  const auto frameStart = std::chrono::steady_clock::now();
  gCaptureProbe.reset();
  if (!surfaceCaches.empty() && (surfaceCaches.front()->getContext() != surface->getContext() ||
                                 !ColorSpace::Equals(surfaceCaches.front()->colorSpace().get(),
                                                     surface->colorSpace().get()))) {
    resetCaches();
  }
  checkTileCount(surface);
  const auto zoomChanged = lastZoomScaleInt != _zoomScaleInt;
  // No draw task outlives a frame, so nothing references the grid tiles of the previous zoom scale.
  // The last scale that drew any is kept as a fallback until its tiles are evicted, since a moving
  // viewport draws none at the scales it passes through.
  if (zoomChanged) {
    auto hasLastGrid = std::any_of(
        tilePool->keysOf(TileType::Grid).begin(), tilePool->keysOf(TileType::Grid).end(),
        [&](const TileKey& key) { return key.scaleInt == lastZoomScaleInt; });
    if (hasLastGrid) {
      staleGridScaleInt = lastZoomScaleInt;
    }
    if (staleGridScaleInt == _zoomScaleInt) {
      staleGridScaleInt = 0;
    }
    recycleGridTiles(staleGridScaleInt != 0 ? staleGridScaleInt : _zoomScaleInt);
  }
  staleGridShown = false;
  auto tileTasks = invalidateTileCaches(dirtyRegions, GetViewportRect(surface, _contentOffset));
  takeDeferredPatches(&tileTasks);
  std::vector<Rect> skippedRects = {};
  auto screenTasks = collectScreenTasks(surface, &tileTasks, &skippedRects);
  // Skipped rects must keep the tiled path; only fall back when both are empty.
  if (screenTasks.empty() && skippedRects.empty()) {
    recycleCurrentTileTasks(tileTasks);
    return renderDirect(surface, autoClear);
  }
  const bool hasBackgroundStyle = _root->hasBackgroundStyle(1.0f);
  if (hasBackgroundStyle && _backgroundColor.isOpaque()) {
    if (styleOutputCache == nullptr) {
      constexpr size_t MaxStyleOutputBytes = 64 * 1024 * 1024;
      styleOutputCache = std::make_unique<StyleOutputCache>(MaxStyleOutputBytes);
    }
    styleOutputCache->beginFrame(_zoomScaleInt, dirtyRegions);
  } else {
    styleOutputCache = nullptr;
  }
  if (forceCaptureZoomScaleInt != _zoomScaleInt) {
    forceCaptureTiles.clear();
    forceCaptureZoomScaleInt = _zoomScaleInt;
  }
  auto zoomScale = ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision);
  // TEMP probe: how much of a tiled frame the per-level background captures take.
  const auto drawStart = std::chrono::steady_clock::now();
  const auto captureProbe = gCaptureProbe;
  double budgetSeconds = 0.0;
  if (_tileTimeBudget > 0.0f) {
    const auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - frameStart).count();
    // A non-positive budget means no limit, so an overrun keeps the smallest positive one instead.
    budgetSeconds = std::max(static_cast<double>(_tileTimeBudget) - elapsed, 1e-9);
  }
  std::vector<bool> drawn = {};
  auto dirtyRects = drawTileTasks(tileTasks, surface, zoomScale, budgetSeconds, &drawn);
  const auto drawnCount = static_cast<size_t>(std::count(drawn.begin(), drawn.end(), true));
  if (drawnCount < tileTasks.size()) {
    deferTileTasks(tileTasks, drawn);
    skippedRects.clear();
    staleGridShown = false;
    screenTasks = collectDisplayTasks(surface, &skippedRects);
  }
  if (drawnCount < tileTasks.size() || !deferredPatches.empty()) {
    hasZoomBlurTiles = true;
  }
  {
    const auto& p = gCaptureProbe;
    int skippedCount = 0;
    for (auto value : p.skipped) {
      skippedCount += value;
    }
    if (skippedCount > 0 || p.blankTreeCells > 0) {
      std::map<int, int> treeLevels = {};
      for (const auto& key : tilePool->keysOf(TileType::Tree)) {
        ++treeLevels[key.level];
      }
      std::string levels = {};
      for (const auto& [level, count] : treeLevels) {
        levels += " L" + std::to_string(level) + "=" + std::to_string(count);
      }
      std::printf(
          "[TILE-SKIP] zoom=%.4f base=%d maxLevel=%d skipped=%d/%d/%d/%d/%d blankTree=%d "
          "evicted=%d/%d grid=%zu/%zu tree=%zu/%zu empty=%zu tasks=%zu drawn=%zu pool:%s\n",
          static_cast<double>(zoomScale), GetTileLevel(zoomScale), maxTreeLevel(), p.skipped[0],
          p.skipped[1], p.skipped[2], p.skipped[3], p.skipped[4], p.blankTreeCells, p.evicted[0],
          p.evicted[1], tilePool->size(TileType::Grid), tilePool->quota(TileType::Grid),
          tilePool->size(TileType::Tree), tilePool->quota(TileType::Tree), emptyTiles.size(),
          tileTasks.size(), drawnCount, levels.c_str());
    }
  }
  if (!tileTasks.empty()) {
    std::printf(
        "[TILE-BUDGET] tasks=%zu drawn=%zu deferredPatches=%zu borrows=%d ms=%.1f\n",
        tileTasks.size(), drawnCount, deferredPatches.size(), gCaptureProbe.subtreeBorrows,
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart)
            .count());
  }
  if (gCaptureProbe.levelCaptures > 0 || gCaptureProbe.captureMs > 2.0) {
    std::printf("[CAP-FRAME] tasks=%zu levels=%d ms=%.1f\n", drawnCount,
                gCaptureProbe.levelCaptures, gCaptureProbe.captureMs);
  }
  if (!tileTasks.empty()) {
    const auto& p = gCaptureProbe;
    if (p.subtreeHits + p.subtreeNewBuilds + p.subtreeRebuilds + p.subtreeRefused > 0) {
      std::printf(
          "[SUB-CACHE] tasks=%zu zoom=%lld hits=%d new=%d rebuilt=%d refused=%d buildMs=%.1f "
          "edges=%d/%d/%d/%d/%d/%d/%d ok=%d fail=%d/%d/%d/%d failEmpty=%d failLeaf=%d "
          "failMaxEdge=%.1f\n",
          drawnCount, static_cast<long long>(_zoomScaleInt), p.subtreeHits, p.subtreeNewBuilds,
          p.subtreeRebuilds, p.subtreeRefused, p.subtreeBuildMs, p.subtreeBuildsByEdge[5],
          p.subtreeBuildsByEdge[6], p.subtreeBuildsByEdge[7], p.subtreeBuildsByEdge[8],
          p.subtreeBuildsByEdge[9], p.subtreeBuildsByEdge[10], p.subtreeBuildsByEdge[11],
          p.subtreeOk, p.subtreeFailPicture, p.subtreeFailImage, p.subtreeFailFilter,
          p.subtreeFailTexture, p.subtreeFailEmptyBounds, p.subtreeFailLeaf,
          static_cast<double>(p.subtreeFailMaxEdge));
    }
  }
  if (!tileTasks.empty()) {
    auto& sp = gStyleProbe;
    static const char* const PlaceNames[] = {"live", "capture", "subtree"};
    std::string line = {};
    char buffer[200];
    for (int place = 0; place < StyleProbe::Places; ++place) {
      for (int type = 0; type < StyleProbe::Types; ++type) {
        if (sp.calls[place][type] == 0 || type == 1) {
          continue;
        }
        std::snprintf(buffer, sizeof(buffer), " %s.t%d=%d/%d/%d/%.3f", PlaceNames[place], type,
                      sp.calls[place][type], sp.analytic[place][type], sp.lazyContent[place][type],
                      sp.contentMpx[place][type]);
        line += buffer;
      }
    }
    if (!line.empty()) {
      std::printf("[STYLE] tasks=%zu uniqueLive=%zu repeatLive=%d%s\n", drawnCount,
                  sp.liveKeys.size(), sp.liveRepeatsFromPrevious, line.c_str());
    }
    sp.beginFrame();
  }
  if (hasBackgroundStyle && !tileTasks.empty()) {
    const double drawMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - drawStart)
            .count();
    std::printf(
        "[CAP-DRAW] tasks=%zu ms=%.1f tileReads=%d tileReadMs=%.1f snapReads=%d missing=%d "
        "fallbacks=%d fallbackMs=%.1f drawSrcN=%d drawSrcMs=%.1f bgStyleN=%d bgStyleMs=%.1f\n",
        drawnCount, drawMs - gCaptureProbe.captureMs,
        gCaptureProbe.tileReads - captureProbe.tileReads,
        gCaptureProbe.tileReadMs - captureProbe.tileReadMs,
        gCaptureProbe.snapshotReads - captureProbe.snapshotReads,
        gCaptureProbe.missing - captureProbe.missing, gCaptureProbe.fallbacks,
        gCaptureProbe.fallbackMs, gCaptureProbe.drawStyleSources - captureProbe.drawStyleSources,
        gCaptureProbe.drawStyleSourceMs - captureProbe.drawStyleSourceMs,
        gCaptureProbe.bgStyles - captureProbe.bgStyles,
        gCaptureProbe.bgStyleMs - captureProbe.bgStyleMs);
    for (int pass = 0; pass < 2; ++pass) {
      const auto& p = gCaptureProbe;
      if (p.sourceCalls[pass][0] == 0 && p.sourceCacheHits[pass][0] == 0) {
        continue;
      }
      std::printf(
          "[CAP-SRC] pass=%s maxDepth=%d d0=%d/%d/%d/%.1f d1=%d/%d/%d/%.1f d2=%d/%d/%d/%.1f "
          "d3=%d/%d/%d/%.1f\n",
          pass == 0 ? "draw" : "capture", p.maxDepth, p.sourceCalls[pass][0],
          p.sourceNonNull[pass][0], p.sourceCacheHits[pass][0], p.sourceMs[pass][0],
          p.sourceCalls[pass][1], p.sourceNonNull[pass][1], p.sourceCacheHits[pass][1],
          p.sourceMs[pass][1], p.sourceCalls[pass][2], p.sourceNonNull[pass][2],
          p.sourceCacheHits[pass][2], p.sourceMs[pass][2], p.sourceCalls[pass][3],
          p.sourceNonNull[pass][3], p.sourceCacheHits[pass][3], p.sourceMs[pass][3]);
    }
  }
  drawScreenTasks(std::move(screenTasks), std::move(skippedRects), surface, autoClear);
  // Once every cell shows a tile of the current scale or a tree level, the stale grid tiles are no
  // longer needed.
  if (!staleGridShown) {
    dropStaleGridTiles();
  }
  return dirtyRects;
}

std::unique_ptr<BackgroundSnapshotMap> DisplayList::captureTaskBackgrounds(
    Surface* surface, const std::vector<DrawTask>& tileTasks,
    const std::vector<size_t>& taskIndices, int level, float zoomScale,
    std::vector<bool>* taskSnapshotCover) const {
  // A capture serves tiles of one raster scale, so a tile is captured with the density it is drawn
  // with. That is what lets tree tiles take part: their scale is 2^-level, not the current zoom.
  auto rasterScale = level == EXACT_LEVEL ? zoomScale : GetTileRasterScale(level);
  if (FloatNearlyZero(rasterScale)) {
    return nullptr;
  }
  // Only styles with neither a cached output nor a tile backdrop are captured, over their own
  // area within each task rather than the whole tile.
  auto viewMatrix = Matrix::MakeScale(rasterScale);
  std::vector<Rect> captureRects = {};
  std::vector<Rect> taskRects = {};
  std::vector<size_t> capturedTasks = {};
  taskRects.reserve(taskIndices.size());
  const auto scanStart = std::chrono::steady_clock::now();
  int forcedCount = 0;
  int missingRectCount = 0;
  for (auto index : taskIndices) {
    const auto& task = tileTasks[index];
    taskRects.push_back(task.tileRect());
    if (isForceCaptured(task)) {
      captureRects.push_back(task.tileRect());
      capturedTasks.push_back(index);
      ++forcedCount;
      continue;
    }
    std::vector<Rect> missingRects = {};
    if (!collectMissingBackdrops(task, &missingRects)) {
      continue;
    }
    missingRectCount += static_cast<int>(missingRects.size());
    // One rect per task: capture culls layers per rect, so a rect per style would multiply the
    // cost of walking the tree by the number of styles.
    auto missingUnion = Rect::MakeEmpty();
    for (const auto& rect : missingRects) {
      missingUnion.join(rect);
    }
    auto captureRect = viewMatrix.mapRect(missingUnion);
    captureRect.roundOut();
    captureRects.push_back(captureRect);
    capturedTasks.push_back(index);
  }
  const double scanMs =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - scanStart)
          .count();
  std::unique_ptr<BackgroundSnapshotMap> map = nullptr;
  if (!captureRects.empty()) {
    // Every task of this level draws with the map, so the layer style sources it caches are
    // recorded against all task rects rather than the narrowed capture rects.
    double captureArea = 0.0;
    for (const auto& rect : captureRects) {
      captureArea += static_cast<double>(rect.width()) * static_cast<double>(rect.height());
    }
    double tileArea = 0.0;
    for (auto index : capturedTasks) {
      const auto& rect = tileTasks[index].tileRect();
      tileArea += static_cast<double>(rect.width()) * static_cast<double>(rect.height());
    }
    const auto before = gCaptureProbe;
    const auto levelStart = std::chrono::steady_clock::now();
    map = captureBackgrounds(surface, captureRects, viewMatrix, taskRects);
    const double levelMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - levelStart)
            .count();
    const double sourceMs = gCaptureProbe.styleSourceMs - before.styleSourceMs;
    const double snapMs = gCaptureProbe.snapshotMs - before.snapshotMs;
    std::printf(
        "[CAP-LEVEL] level=%d tiles=%zu ms=%.1f tasks=%zu forced=%d missRects=%d scanMs=%.2f "
        "areaRatio=%.2f srcN=%d srcMs=%.1f snapN=%d snapMs=%.1f restMs=%.1f\n",
        level, capturedTasks.size(), levelMs, taskIndices.size(), forcedCount, missingRectCount,
        scanMs, tileArea > 0.0 ? captureArea / tileArea : 0.0,
        gCaptureProbe.styleSources - before.styleSources, sourceMs,
        gCaptureProbe.snapshots - before.snapshots, snapMs, levelMs - sourceMs - snapMs);
  } else {
    std::printf("[CAP-SKIP] level=%d tasks=%zu scanMs=%.2f\n", level, taskIndices.size(), scanMs);
    // Every task reads its backdrops from the tile, but the layer style sources are still
    // shared: the first task records each one and the others reuse it.
    map = std::make_unique<BackgroundSnapshotMap>();
    map->styleSourceRects = makeStyleSourceRects(taskRects, viewMatrix);
  }
  for (auto index : capturedTasks) {
    (*taskSnapshotCover)[index] = true;
  }
  return map;
}

std::vector<Rect> DisplayList::drawTileTasks(const std::vector<DrawTask>& tileTasks,
                                             Surface* surface, float zoomScale,
                                             double budgetSeconds, std::vector<bool>* drawn) {
  drawn->assign(tileTasks.size(), false);
  size_t drawnCount = 0;
  TileBudget budget = {};
  budget.deadline = std::chrono::steady_clock::now() +
                    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        std::chrono::duration<double>(budgetSeconds));
  auto timeExpired = [&]() { return budgetSeconds > 0.0 && drawnCount > 0 && budget.expired(); };
  // One capture transform cannot serve two raster scales, so a group holds the tasks of one level.
  // Within a level, a group of neighboring tiles shares a capture while the budget can still stop
  // between groups. Groups keep the order in which their first task appears.
  struct TaskGroup {
    int level = 0;
    int blockX = 0;
    int blockY = 0;
    std::vector<size_t> taskIndices = {};
  };
  std::vector<TaskGroup> groups = {};
  for (size_t i = 0; i < tileTasks.size(); ++i) {
    const auto& tile = *tileTasks[i].getTiles().front();
    auto level = tileTasks[i].treeLevel();
    auto blockX = FloatFloorToInt(static_cast<float>(tile.tileX) / 2.0f);
    auto blockY = FloatFloorToInt(static_cast<float>(tile.tileY) / 2.0f);
    auto it = std::find_if(groups.begin(), groups.end(), [&](const TaskGroup& group) {
      return group.level == level && group.blockX == blockX && group.blockY == blockY;
    });
    if (it == groups.end()) {
      groups.push_back({level, blockX, blockY, {i}});
    } else {
      it->taskIndices.push_back(i);
    }
  }
  const bool hasBackgroundStyle = _root->hasBackgroundStyle(1.0f);
  std::vector<bool> taskSnapshotCover(tileTasks.size(), false);
  std::vector<Rect> dirtyRects = {};
  auto surfaceRect = Rect::MakeWH(surface->width(), surface->height());
  for (const auto& group : groups) {
    if (timeExpired()) {
      break;
    }
    const bool mayBorrow =
        budgetSeconds > 0.0 &&
        std::none_of(group.taskIndices.begin(), group.taskIndices.end(),
                     [&](size_t index) { return tileTasks[index].requiresExactCaches(); });
    tileBudget = mayBorrow ? &budget : nullptr;
    std::unique_ptr<BackgroundSnapshotMap> snapshots = nullptr;
    if (hasBackgroundStyle) {
      const auto captureStart = std::chrono::steady_clock::now();
      snapshots = captureTaskBackgrounds(surface, tileTasks, group.taskIndices, group.level,
                                         zoomScale, &taskSnapshotCover);
      ++gCaptureProbe.levelCaptures;
      gCaptureProbe.captureMs +=
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - captureStart)
              .count();
    }
    for (auto taskIndex : group.taskIndices) {
      const auto& task = tileTasks[taskIndex];
      auto isTreeTask = task.treeLevel() != EXACT_LEVEL;
      int missingBackdrops = 0;
      auto hasContent =
          drawTileTask(task, snapshots.get(), taskSnapshotCover[taskIndex], &missingBackdrops);
      updateForceCapture(task, missingBackdrops > 0);
      if (missingBackdrops > 0) {
        // Some styles predicted to read the tile could not, so redraw the task with a capture of its
        // own rather than leave those styles out of the tile.
        Matrix viewMatrix = Matrix::I();
        Rect clipRect = {};
        auto rasterScale = getTileDrawGeometry(task, &viewMatrix, &clipRect);
        const auto fallbackStart = std::chrono::steady_clock::now();
        auto taskSnapshots =
            captureBackgrounds(surface, {task.tileRect()}, Matrix::MakeScale(rasterScale));
        hasContent = drawTileTask(task, taskSnapshots.get(), true);
        const double fallbackMs = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - fallbackStart)
                                      .count();
        ++gCaptureProbe.fallbacks;
        gCaptureProbe.fallbackMs += fallbackMs;
        std::printf("[CAP-FALLBACK] level=%d missing=%d ms=%.1f\n", task.treeLevel(),
                    missingBackdrops, fallbackMs);
      }
      if (!hasContent && task.coversTiles()) {
        recycleBlankTiles(task);
      }
      // A grid task reports its tileRect in the zoom grid, while a tree task reports it in the raster
      // grid of its own level. Scale the tree rect to the zoom grid first, then shift both by the
      // content offset to reach surface pixels, which is where this frame reports its dirty rects.
      auto dirtyRect = task.tileRect();
      if (isTreeTask) {
        auto displayScale = zoomScale / GetTileRasterScale(task.treeLevel());
        dirtyRect.scale(displayScale, displayScale);
      }
      dirtyRect.offset(_contentOffset.x, _contentOffset.y);
      if (dirtyRect.intersect(surfaceRect)) {
        dirtyRects.emplace_back(dirtyRect);
      }
      (*drawn)[taskIndex] = true;
      ++drawnCount;
    }
    if (budget.borrowedCache) {
      // Layers of this group drew subtree caches of other sizes, so its tiles are shown as they are
      // and rasterized again in a later frame.
      budget.borrowedCache = false;
      for (auto taskIndex : group.taskIndices) {
        const auto& task = tileTasks[taskIndex];
        for (const auto& tile : task.getTiles()) {
          DrawTask redraw(tile, _tileSize);
          redraw.setTreeLevel(task.treeLevel());
          redraw.setRequiresExactCaches(true);
          deferredPatches.push_back(std::move(redraw));
        }
      }
    }
  }
  tileBudget = nullptr;
  return dirtyRects;
}

void DisplayList::checkTileCount(Surface* renderSurface) {
  DEBUG_ASSERT(renderSurface != nullptr);
  const auto surfaceWidth = renderSurface->width();
  const auto surfaceHeight = renderSurface->height();
  if (totalTileCount > 0 && surfaceWidth == lastSurfaceWidth &&
      surfaceHeight == lastSurfaceHeight) {
    return;
  }
  lastSurfaceWidth = surfaceWidth;
  lastSurfaceHeight = surfaceHeight;
  auto countTiles = [surfaceWidth, surfaceHeight](float tileSize) {
    return (FloatCeilToInt(static_cast<float>(surfaceWidth) / tileSize) + 2) *
           (FloatCeilToInt(static_cast<float>(surfaceHeight) / tileSize) + 2);
  };
  const auto tileSizeFloat = static_cast<float>(_tileSize);
  // A tree tile of the viewport's level covers more than half a tile size on screen.
  const auto treeTileCount = countTiles(tileSizeFloat * 0.5f);
  const auto gridTileCount = countTiles(tileSizeFloat);
  totalTileCount = 2 * treeTileCount + gridTileCount;
  // Callers built against the scale-keyed cache ask for at least maxTileCount() tiles; keep that
  // floor, since the layered pool otherwise sizes itself from the viewport alone.
  if (_maxTileCount > totalTileCount) {
    totalTileCount = _maxTileCount;
  }
  auto maxTileCountPerAtlas = getMaxTileCountPerAtlas(renderSurface->getContext());
  if (maxTileCountPerAtlas > 0) {
    auto atlasCount = (totalTileCount + maxTileCountPerAtlas - 1) / maxTileCountPerAtlas;
    totalTileCount = std::min(atlasCount, MAX_ATLAS_COUNT) * maxTileCountPerAtlas;
  }
  minTreeTileCount = static_cast<size_t>(
      FloatCeilToInt(TREE_LEVELS_COVERAGE_FACTOR * static_cast<float>(treeTileCount)));
  // Splitting by demand gives both types the same safety factor at any aspect ratio.
  auto treeRatio =
      static_cast<double>(treeTileCount) / static_cast<double>(treeTileCount + gridTileCount);
  auto total = static_cast<size_t>(totalTileCount);
  auto treeQuota = static_cast<size_t>(std::lround(static_cast<double>(total) * treeRatio));
  // MAX_ATLAS_COUNT can leave less than the viewport and the fallback levels need together. Keep the
  // viewport sharp first and the fallback levels second, so a capped pool degrades instead of
  // starving one side; the proportional split only decides how the slack is spent.
  auto gridDemand = std::min(static_cast<size_t>(gridTileCount), total);
  auto treeDemand = std::min(minTreeTileCount, total);
  treeQuota = std::min(std::max(treeQuota, treeDemand), total - gridDemand);
  tilePool->setQuota(TileType::Tree, treeQuota);
  tilePool->setQuota(TileType::Grid, total - treeQuota);
  blankTiles->setMaxCount(BLANK_MARKS_PER_TILE * static_cast<size_t>(totalTileCount));
}

std::vector<DrawTask> DisplayList::invalidateTileCaches(const std::vector<Rect>& dirtyRegions,
                                                        const Rect& viewportRect) {
  if (dirtyRegions.empty()) {
    return {};
  }
  std::vector<DrawTask> tileTasks = {};
  invalidateStaleGridTiles(dirtyRegions);
  invalidateCurrentGridTiles(dirtyRegions, viewportRect, &tileTasks);
  invalidateTreeTiles(dirtyRegions, viewportRect, &tileTasks);
  invalidateBlankTiles(dirtyRegions);
  return tileTasks;
}

void DisplayList::invalidateBlankTiles(const std::vector<Rect>& dirtyRegions) {
  if (blankTiles->size() == 0) {
    return;
  }
  auto gridScale = blankTiles->gridScale();
  if (gridScale != 0) {
    auto group = TileKey::MakeGrid(0, 0, gridScale);
    auto zoomMatrix = Matrix::MakeScale(ToZoomScaleFloat(gridScale, _zoomScalePrecision));
    for (auto& dirtyRect : MapDirtyRegions(dirtyRegions, zoomMatrix)) {
      blankTiles->eraseUnderRect(group, dirtyRect, _tileSize);
    }
  }
  for (auto level : blankTiles->treeLevels()) {
    auto group = TileKey::MakeTree(0, 0, level);
    auto rasterMatrix = Matrix::MakeScale(GetTileRasterScale(level));
    for (auto& dirtyRect : MapDirtyRegions(dirtyRegions, rasterMatrix)) {
      blankTiles->eraseUnderRect(group, dirtyRect, _tileSize);
    }
  }
}

void DisplayList::invalidateTreeTiles(const std::vector<Rect>& dirtyRegions,
                                      const Rect& viewportRect, std::vector<DrawTask>* tileTasks) {
  if (tilePool->size(TileType::Tree) == 0) {
    return;
  }
  auto zoomScale = ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision);
  auto baseLevel = GetTileLevel(zoomScale);
  struct LevelDirtyRects {
    std::vector<Rect> dirtyRects = {};
    Rect viewportRect = {};
  };
  std::unordered_map<int, LevelDirtyRects> rectsByLevel = {};
  std::vector<TileKey> dirtyKeys = {};
  for (auto& key : tilePool->keysOf(TileType::Tree)) {
    auto result = rectsByLevel.find(key.level);
    if (result == rectsByLevel.end()) {
      auto rasterScale = GetTileRasterScale(key.level);
      LevelDirtyRects rects = {};
      rects.dirtyRects = MapDirtyRegions(dirtyRegions, Matrix::MakeScale(rasterScale));
      rects.viewportRect = viewportRect;
      rects.viewportRect.scale(rasterScale / zoomScale, rasterScale / zoomScale);
      result = rectsByLevel.emplace(key.level, std::move(rects)).first;
    }
    auto tileRect =
        Rect::MakeXYWH(key.tileX * _tileSize, key.tileY * _tileSize, _tileSize, _tileSize);
    auto& rects = result->second;
    if (std::none_of(
            rects.dirtyRects.begin(), rects.dirtyRects.end(),
            [&tileRect](const Rect& dirtyRect) { return Rect::Intersects(tileRect, dirtyRect); })) {
      continue;
    }
    // Only the coarsest level is patched, because the finer fallbacks are not drawn while the
    // grid covers every cell: patching them would replay the whole scene for pixels nobody sees.
    // They are dropped here instead and rebuilt by the chain top-up once the grid is settled.
    const auto coarsestLevel = std::min(baseLevel + TREE_LEVEL_COUNT - 1, maxTreeLevel());
    if (key.level != coarsestLevel || !Rect::Intersects(tileRect, rects.viewportRect)) {
      dirtyKeys.push_back(key);
      continue;
    }
    auto tile = tilePool->find(key);
    for (auto& dirtyRect : rects.dirtyRects) {
      auto drawRect = tileRect;
      if (drawRect.intersect(dirtyRect)) {
        DrawTask task(tile, _tileSize, drawRect);
        task.setTreeLevel(key.level);
        tileTasks->push_back(std::move(task));
      }
    }
  }
  for (auto& key : dirtyKeys) {
    if (auto recycled = tilePool->erase(key)) {
      emptyTiles.push_back(std::move(recycled));
    }
  }
}

static TileKey GetTileKey(const DrawTask& task, const Tile& tile, int64_t zoomScaleInt) {
  return task.treeLevel() == EXACT_LEVEL
             ? TileKey::MakeGrid(tile.tileX, tile.tileY, zoomScaleInt)
             : TileKey::MakeTree(tile.tileX, tile.tileY, task.treeLevel());
}

void DisplayList::recycleCurrentTileTasks(const std::vector<DrawTask>& tileTasks) {
  for (auto& task : tileTasks) {
    for (auto& tile : task.getTiles()) {
      if (auto recycled = tilePool->erase(GetTileKey(task, *tile, _zoomScaleInt))) {
        emptyTiles.push_back(std::move(recycled));
      }
    }
  }
}

void DisplayList::recycleGridTiles(int64_t keepScaleInt) {
  if (keepScaleInt == 0) {
    auto tiles = tilePool->clear(TileType::Grid);
    emptyTiles.insert(emptyTiles.end(), tiles.begin(), tiles.end());
  } else {
    std::vector<TileKey> keys = {};
    for (const auto& key : tilePool->keysOf(TileType::Grid)) {
      if (key.scaleInt != keepScaleInt) {
        keys.push_back(key);
      }
    }
    for (const auto& key : keys) {
      if (auto recycled = tilePool->erase(key)) {
        emptyTiles.push_back(std::move(recycled));
      }
    }
  }
  blankTiles->clearGrid();
}

void DisplayList::dropStaleGridTiles() {
  if (staleGridScaleInt == 0) {
    return;
  }
  std::vector<TileKey> keys = {};
  for (const auto& key : tilePool->keysOf(TileType::Grid)) {
    if (key.scaleInt == staleGridScaleInt) {
      keys.push_back(key);
    }
  }
  for (const auto& key : keys) {
    if (auto recycled = tilePool->erase(key)) {
      emptyTiles.push_back(std::move(recycled));
    }
  }
  staleGridScaleInt = 0;
}

void DisplayList::invalidateStaleGridTiles(const std::vector<Rect>& dirtyRegions) {
  if (staleGridScaleInt == 0 || dirtyRegions.empty()) {
    return;
  }
  auto staleMatrix = Matrix::MakeScale(ToZoomScaleFloat(staleGridScaleInt, _zoomScalePrecision));
  for (auto& dirtyRect : MapDirtyRegions(dirtyRegions, staleMatrix)) {
    bool continuous = false;
    auto tiles =
        GetGridTilesUnderRect(tilePool.get(), staleGridScaleInt, _tileSize, dirtyRect, &continuous);
    for (auto& tile : tiles) {
      if (auto recycled =
              tilePool->erase(TileKey::MakeGrid(tile->tileX, tile->tileY, staleGridScaleInt))) {
        emptyTiles.push_back(std::move(recycled));
      }
    }
  }
}

void DisplayList::recycleBlankTiles(const DrawTask& task) {
  for (auto& tile : task.getTiles()) {
    auto key = GetTileKey(task, *tile, _zoomScaleInt);
    if (tilePool->erase(key) == nullptr) {
      continue;
    }
    // The slot stays valid for the rest of this frame: it is only rewritten when a later frame
    // takes it from the free list.
    emptyTiles.push_back(tile);
    blankTiles->insert(key);
  }
}

void DisplayList::invalidateCurrentGridTiles(const std::vector<Rect>& dirtyRegions,
                                             const Rect& viewportRect,
                                             std::vector<DrawTask>* tileTasks) {
  auto zoomMatrix = Matrix::MakeScale(ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision));
  auto dirtyRects = MapDirtyRegions(dirtyRegions, zoomMatrix, true);
  for (auto& dirtyRect : dirtyRects) {
    bool continuous = false;
    auto tiles =
        GetGridTilesUnderRect(tilePool.get(), _zoomScaleInt, _tileSize, dirtyRect, &continuous);
    if (continuous && viewportRect.contains(dirtyRect)) {
      tileTasks->emplace_back(std::move(tiles), _tileSize, dirtyRect);
      continue;
    }
    for (auto& tile : tiles) {
      auto tileRect = tile->getTileRect(_tileSize);
      if (!Rect::Intersects(tileRect, viewportRect)) {
        // Dirty content outside the viewport only invalidates the tile: rasterizing it now would
        // touch pixels nobody sees, while keeping it would leave stale content behind for the next
        // time it scrolls back into view.
        auto key = TileKey::MakeGrid(tile->tileX, tile->tileY, _zoomScaleInt);
        if (auto recycled = tilePool->erase(key)) {
          emptyTiles.push_back(std::move(recycled));
        }
        continue;
      }
      auto drawRect = tileRect;
      // The unclipped dirty rect is used here so that a tile straddling the viewport edge does not
      // keep stale pixels in the part outside the viewport.
      if (drawRect.intersect(dirtyRect)) {
        tileTasks->emplace_back(tile, _tileSize, drawRect);
      }
    }
  }
}

std::vector<DrawTask> DisplayList::collectScreenTasks(const Surface* surface,
                                                      std::vector<DrawTask>* tileTasks,
                                                      std::vector<Rect>* skippedRects) {
  auto refineBudget = std::max(_maxTilesRefinedPerFrame, 0);
  // Below minFallbackScale() no fallback level is left, so missing tiles are rasterized at once.
  const auto useFallback =
      _tileUpdateMode != TileUpdateMode::Immediate &&
      GetTileLevel(ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision)) <= maxTreeLevel();
  // Grid tiles drawn while the viewport moves are often stale by the next frame, so a moving
  // viewport spends the budget on tree tiles, which stay valid across zoom scales.
  const auto viewportChanged =
      lastContentOffset != _contentOffset || lastZoomScaleInt != _zoomScaleInt;
  if (viewportChanged) {
    updateMousePosition();
    lastContentOffset = _contentOffset;
    lastZoomScaleInt = _zoomScaleInt;
  }
  hasZoomBlurTiles = false;
  auto renderRect = getVisibleRenderRect(surface);
  if (renderRect.isEmpty()) {
    return {};
  }
  auto zoomScale = ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision);
  const auto tileSizeFloat = static_cast<float>(_tileSize);
  const auto startX = FloatFloorToInt(renderRect.left / tileSizeFloat);
  const auto startY = FloatFloorToInt(renderRect.top / tileSizeFloat);
  const auto endX = FloatCeilToInt(renderRect.right / tileSizeFloat);
  const auto endY = FloatCeilToInt(renderRect.bottom / tileSizeFloat);

  if (startX >= endX || startY >= endY) {
    return {};
  }

  std::vector<DrawTask> screenTasks = {};
  auto tileCount = static_cast<size_t>(endX - startX) * static_cast<size_t>(endY - startY);
  screenTasks.reserve(tileCount);
  std::vector<TreeLevelCoverage> coverages = {};
  std::vector<TreeLevelCoverage> staleCoverages = {};
  if (useFallback) {
    coverages = CollectTreeCoverages(tilePool.get(), blankTiles.get(), zoomScale, renderRect,
                                     _tileSize, maxTreeLevel());
    staleCoverages = CollectStaleGridCoverage(
        tilePool.get(), staleGridScaleInt, ToZoomScaleFloat(staleGridScaleInt, _zoomScalePrecision),
        zoomScale, renderRect, _tileSize);
  }
  auto getGridRect = [&](const std::pair<int, int>& grid) {
    auto rect =
        Rect::MakeXYWH(grid.first * _tileSize, grid.second * _tileSize, _tileSize, _tileSize);
    rect.intersect(renderRect);
    return rect;
  };
  std::vector<std::pair<int, int>> missingGrids = {};
  missingGrids.reserve(tileCount);
  auto sortedTiles = GetSortedTiles(startX, endX, startY, endY, _tileSize, mousePosition);
  for (const auto& grid : sortedTiles) {
    auto key = TileKey::MakeGrid(grid.first, grid.second, _zoomScaleInt);
    auto tile = tilePool->find(key);
    if (tile != nullptr) {
      tilePool->touch(key);
      auto tileRect = tile->getTileRect(_tileSize, &renderRect);
      screenTasks.emplace_back(tile, _tileSize, tileRect);
    } else if (blankTiles->contains(key)) {
      skippedRects->push_back(getGridRect(grid));
      ++gCaptureProbe.skipped[0];
    } else {
      missingGrids.push_back(grid);
    }
  }

  std::vector<std::shared_ptr<Tile>> freeTiles = {};
  std::vector<std::pair<int, int>> dirtyGrids = {};
  bool continuous = false;
  // A frame without any atlas has nothing to fall back on, so it goes straight to full resolution.
  if (missingGrids.size() == tileCount && (!useFallback || surfaceCaches.empty())) {
    freeTiles = createContinuousTiles(surface, endX - startX, endY - startY);
    continuous = !freeTiles.empty();
  }
  if (continuous) {
    dirtyGrids = GenerateGridTiles(startX, endX, startY, endY);
    // This frame fills the whole grid at once and leaves the fallback levels empty, so keep the loop
    // alive for the frames that prepare them. Without a budget they can never be filled.
    if (useFallback && _maxTilesRefinedPerFrame > 0) {
      hasZoomBlurTiles = true;
    }
  } else {
    dirtyGrids.reserve(missingGrids.size());
    std::vector<std::pair<int, int>> fallbackGrids = {};
    for (auto& grid : missingGrids) {
      if (!useFallback || (!viewportChanged && refineBudget > 0)) {
        if (useFallback) {
          refineBudget--;
        }
        dirtyGrids.push_back(grid);
      } else {
        fallbackGrids.push_back(grid);
      }
    }
    // This frame draws the last missing grid tiles, so nothing else is left to drive the loop; keep
    // it alive for the frame that tops up the fallback levels.
    if (useFallback && !missingGrids.empty() && dirtyGrids.size() == missingGrids.size()) {
      hasZoomBlurTiles = true;
    }
    if (!fallbackGrids.empty()) {
      // A static frame only rasterizes while there is budget; without one these grids are selected
      // again on every frame and never rasterized, so they must not keep the loop alive.
      if (_maxTilesRefinedPerFrame > 0) {
        hasZoomBlurTiles = true;
      }
      std::vector<Rect> uncoveredRects = {};
      for (auto& grid : fallbackGrids) {
        auto gridRect = getGridRect(grid);
        if (FindFinestCoverage(&coverages, gridRect, _tileSize) == nullptr) {
          uncoveredRects.push_back(gridRect);
        }
      }
      // Pass 1: cells without any fallback are rasterized regardless of the budget, or they would
      // show the background. The coarsest level is used because it needs the fewest tiles: the
      // cost of a tile is dominated by the layers it intersects rather than by its area, and a
      // large layer is drawn again for every tile it reaches. Fast mode accepts the background
      // instead, so the frame that changes the zoom does not pay for tiles the budget skipped.
      if (!uncoveredRects.empty() && _tileUpdateMode != TileUpdateMode::Fast) {
        auto& coarsest = coverages.back();
        rasterizeTreeTiles(surface, CollectMissingTreeTiles(coarsest, uncoveredRects, _tileSize),
                           &coarsest, tileTasks);
      }
      // Pass 2: the budget refines the displayed fallbacks one level at a time, coarse levels
      // first, so the screen sharpens evenly instead of one cell at a time.
      if (viewportChanged && refineBudget > 0) {
        struct RefineCandidate {
          size_t coverageIndex = 0;
          int tileX = 0;
          int tileY = 0;
        };
        std::vector<RefineCandidate> candidates = {};
        std::unordered_set<int64_t> requested = {};
        for (auto& grid : fallbackGrids) {
          auto gridRect = getGridRect(grid);
          auto coverage = FindFinestCoverage(&coverages, gridRect, _tileSize);
          if (coverage == nullptr || coverage == &coverages.front()) {
            continue;
          }
          auto finerIndex = static_cast<size_t>(coverage - coverages.data()) - 1;
          auto& finer = coverages[finerIndex];
          ForEachTileUnderRect(finer.toRaster(gridRect), _tileSize, [&](int tileX, int tileY) {
            auto packed = static_cast<int64_t>(finerIndex) << 48 |
                          static_cast<int64_t>(finer.indexOf(tileX, tileY));
            if (!finer.isCovered(tileX, tileY) && requested.insert(packed).second) {
              candidates.push_back({finerIndex, tileX, tileY});
            }
            return true;
          });
        }
        // Candidates are already sorted by distance to the mouse within each level.
        std::stable_sort(candidates.begin(), candidates.end(),
                         [](const RefineCandidate& a, const RefineCandidate& b) {
                           return a.coverageIndex > b.coverageIndex;
                         });
        if (candidates.size() > static_cast<size_t>(refineBudget)) {
          candidates.resize(static_cast<size_t>(refineBudget));
        }
        std::vector<std::vector<std::pair<int, int>>> cellsByLevel(coverages.size());
        for (auto& candidate : candidates) {
          cellsByLevel[candidate.coverageIndex].emplace_back(candidate.tileX, candidate.tileY);
        }
        for (size_t i = 0; i < coverages.size(); ++i) {
          auto refined = rasterizeTreeTiles(surface, cellsByLevel[i], &coverages[i], tileTasks);
          refineBudget -= static_cast<int>(refined);
        }
      }
      // The passes above may have added tiles, so the coverage is queried again: the finest
      // complete level is displayed, and cells nothing covers fall back to the background.
      for (auto& grid : fallbackGrids) {
        auto gridRect = getGridRect(grid);
        auto coverage = FindFinestCoverage(&coverages, gridRect, _tileSize);
        if (coverage == nullptr) {
          coverage = FindFinestCoverage(&staleCoverages, gridRect, _tileSize);
          staleGridShown |= coverage != nullptr;
        }
        if (coverage != nullptr) {
          AppendFallbackScreenTasks(coverage, gridRect, _tileSize, &screenTasks, skippedRects);
        } else {
          skippedRects->push_back(gridRect);
          ++gCaptureProbe.skipped[1];
        }
      }
    }
    // Every viewport cell has its exact tile, so the screen is sharp and the budget can top up the
    // coarser fallback levels. A zoom-in remaps them onto the next frame's L+3..L+1; without them
    // that zoom has to rasterize a fallback level on the spot. A frame that already has other tile
    // work only reports what is left, so the loop keeps running until the levels are full.
    if (useFallback && !coverages.empty()) {
      const bool mayRefine = tileTasks->empty();
      const bool gridComplete = missingGrids.empty();
      // Once the grid is complete, the budget tops up the levels finer than the coarsest one, coarse
      // first, down to L itself. The coarsest level is left to pass 1.
      auto chainBudget = gridComplete ? std::max(_maxTilesRefinedPerFrame, 0) : 0;
      // coverages[0] is L and coverages.back() is the coarsest level.
      const auto finerLevelCount = static_cast<int>(coverages.size()) - 1;
      if (mayRefine) {
        for (int index = finerLevelCount - 1; index >= 0 && chainBudget > 0; --index) {
          auto& coverage = coverages[static_cast<size_t>(index)];
          auto cells = CollectMissingTreeTiles(coverage, {renderRect}, _tileSize);
          if (cells.empty()) {
            continue;
          }
          if (cells.size() > static_cast<size_t>(chainBudget)) {
            cells.resize(static_cast<size_t>(chainBudget));
          }
          auto refined = rasterizeTreeTiles(surface, cells, &coverage, tileTasks);
          chainBudget -= static_cast<int>(refined);
        }
      }
      // Report the state left after this frame: a skipped frame still has to keep the loop alive,
      // and a frame that filled everything has to stop it. An incomplete grid has already kept the
      // loop alive, so only a complete one needs the scan.
      bool chainPending = false;
      if (gridComplete) {
        for (int index = finerLevelCount - 1; index >= 0; --index) {
          auto& coverage = coverages[static_cast<size_t>(index)];
          if (!CollectMissingTreeTiles(coverage, {renderRect}, _tileSize).empty()) {
            chainPending = true;
            break;
          }
        }
      }
      // Without a budget the chain can never progress, and a capped pool that cannot hold the
      // levels would keep asking for frames that never fill them, so neither may keep the loop
      // running.
      auto treeCapacity = tilePool->quota(TileType::Tree) + tilePool->quota(TileType::Grid);
      if (chainPending && _maxTilesRefinedPerFrame > 0 &&
          tilePool->size(TileType::Tree) < treeCapacity) {
        hasZoomBlurTiles = true;
      }
    }
    // Stale grid tiles not on screen this frame are the first ones the new grid tiles may evict.
    staleCoverages.clear();
    freeTiles = getFreeTiles(surface, TileType::Grid, dirtyGrids.size());
    if (freeTiles.size() < dirtyGrids.size()) {
      if (!useFallback) {
        emptyTiles.insert(emptyTiles.end(), freeTiles.begin(), freeTiles.end());
        LOGE("DisplayList::collectScreenTasks() Not enough free tiles available.");
        return {};
      }
      // The next frame selects the remaining grids again, so they only need something to show now.
      // A frame that could not allocate any tile would repeat itself, so it must not ask for one.
      if (!freeTiles.empty()) {
        hasZoomBlurTiles = true;
      }
      for (auto i = freeTiles.size(); i < dirtyGrids.size(); ++i) {
        auto gridRect = getGridRect(dirtyGrids[i]);
        auto coverage = FindFinestCoverage(&coverages, gridRect, _tileSize);
        if (coverage != nullptr) {
          AppendFallbackScreenTasks(coverage, gridRect, _tileSize, &screenTasks, skippedRects);
        } else {
          skippedRects->push_back(gridRect);
          ++gCaptureProbe.skipped[2];
        }
      }
      dirtyGrids.resize(freeTiles.size());
    }
  }
  size_t tileIndex = 0;
  std::vector<std::shared_ptr<Tile>> taskTiles = {};
  taskTiles.reserve(dirtyGrids.size());
  for (auto& grid : dirtyGrids) {
    auto& tile = freeTiles[tileIndex++];
    tile->tileX = grid.first;
    tile->tileY = grid.second;
    taskTiles.push_back(tile);
    auto tileRect = tile->getTileRect(_tileSize, &renderRect);
    screenTasks.emplace_back(tile, _tileSize, tileRect);
    tilePool->insert(TileKey::MakeGrid(tile->tileX, tile->tileY, _zoomScaleInt), tile);
  }
  if (continuous) {
    // If we have continuous tiles, we can draw a single rectangle for the entire area.
    auto drawRect = Rect::MakeXYWH(startX * _tileSize, startY * _tileSize,
                                   (endX - startX) * _tileSize, (endY - startY) * _tileSize);
    DrawTask task(std::move(freeTiles), _tileSize, drawRect);
    task.setCoversTiles(true);
    tileTasks->push_back(std::move(task));
  } else {
    for (auto& tile : taskTiles) {
      DrawTask task(tile, _tileSize);
      task.setCoversTiles(true);
      tileTasks->push_back(std::move(task));
    }
  }
  return screenTasks;
}

int DisplayList::maxTreeLevel() const {
  if (_minFallbackScale <= 0.0f) {
    return std::numeric_limits<int>::max();
  }
  // The coarsest level whose raster scale 2^-level is not below minFallbackScale().
  return std::ilogb(1.0f / _minFallbackScale);
}

Rect DisplayList::getVisibleRenderRect(const Surface* surface) const {
  auto renderRect = GetViewportRect(surface, _contentOffset);
  auto renderBounds = _root->renderBounds;
  auto zoomScale = ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision);
  renderBounds.scale(zoomScale, zoomScale);
  renderBounds.roundOut();
  if (!renderRect.intersect(renderBounds)) {
    return Rect::MakeEmpty();
  }
  return renderRect;
}

std::vector<DrawTask> DisplayList::collectDisplayTasks(const Surface* surface,
                                                       std::vector<Rect>* skippedRects) {
  auto renderRect = getVisibleRenderRect(surface);
  if (renderRect.isEmpty()) {
    return {};
  }
  const auto tileSizeFloat = static_cast<float>(_tileSize);
  const auto startX = FloatFloorToInt(renderRect.left / tileSizeFloat);
  const auto startY = FloatFloorToInt(renderRect.top / tileSizeFloat);
  const auto endX = FloatCeilToInt(renderRect.right / tileSizeFloat);
  const auto endY = FloatCeilToInt(renderRect.bottom / tileSizeFloat);
  auto zoomScale = ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision);
  // Cells whose tiles were left to a later frame show any coarser level still cached, even past
  // the fallback levels a frame rasterizes, since they are only on screen until those tiles exist.
  int coarsestLevel = std::numeric_limits<int>::min();
  for (const auto& key : tilePool->keysOf(TileType::Tree)) {
    coarsestLevel = std::max(coarsestLevel, key.level);
  }
  auto baseLevel = GetTileLevel(zoomScale);
  auto levelCount = std::max(coarsestLevel - baseLevel + 1, TREE_LEVEL_COUNT);
  auto coverages = CollectTreeCoverages(tilePool.get(), blankTiles.get(), zoomScale, renderRect,
                                        _tileSize, maxTreeLevel(), levelCount);
  auto staleCoverages = CollectStaleGridCoverage(
      tilePool.get(), staleGridScaleInt, ToZoomScaleFloat(staleGridScaleInt, _zoomScalePrecision),
      zoomScale, renderRect, _tileSize);
  std::vector<DrawTask> screenTasks = {};
  for (auto tileY = startY; tileY < endY; ++tileY) {
    for (auto tileX = startX; tileX < endX; ++tileX) {
      auto key = TileKey::MakeGrid(tileX, tileY, _zoomScaleInt);
      auto gridRect = Rect::MakeXYWH(tileX * _tileSize, tileY * _tileSize, _tileSize, _tileSize);
      gridRect.intersect(renderRect);
      if (auto tile = tilePool->find(key)) {
        screenTasks.emplace_back(tile, _tileSize, tile->getTileRect(_tileSize, &renderRect));
      } else if (blankTiles->contains(key)) {
        skippedRects->push_back(gridRect);
        ++gCaptureProbe.skipped[3];
      } else if (auto coverage = FindFinestCoverage(&coverages, gridRect, _tileSize)) {
        AppendFallbackScreenTasks(coverage, gridRect, _tileSize, &screenTasks, skippedRects);
      } else if (auto stale = FindFinestCoverage(&staleCoverages, gridRect, _tileSize)) {
        staleGridShown = true;
        AppendFallbackScreenTasks(stale, gridRect, _tileSize, &screenTasks, skippedRects);
      } else {
        skippedRects->push_back(gridRect);
        ++gCaptureProbe.skipped[4];
      }
    }
  }
  return screenTasks;
}

void DisplayList::takeDeferredPatches(std::vector<DrawTask>* tileTasks) {
  auto patches = std::move(deferredPatches);
  deferredPatches.clear();
  for (auto& task : patches) {
    // A tile recycled since then, for a zoom change or an eviction, has no outdated content left.
    auto cached = std::all_of(
        task.getTiles().begin(), task.getTiles().end(), [&](const std::shared_ptr<Tile>& tile) {
          return tilePool->find(GetTileKey(task, *tile, _zoomScaleInt)) == tile;
        });
    if (cached) {
      tileTasks->push_back(std::move(task));
    }
  }
}

void DisplayList::deferTileTasks(const std::vector<DrawTask>& tileTasks,
                                 const std::vector<bool>& drawn) {
  for (size_t i = 0; i < tileTasks.size(); ++i) {
    if (drawn[i]) {
      continue;
    }
    const auto& task = tileTasks[i];
    if (!task.coversTiles()) {
      deferredPatches.push_back(task);
      continue;
    }
    // A new tile holds nothing to show yet, so it goes back to the free list and its cell is
    // requested again by a later frame.
    for (auto& tile : task.getTiles()) {
      if (auto recycled = tilePool->erase(GetTileKey(task, *tile, _zoomScaleInt))) {
        emptyTiles.push_back(std::move(recycled));
      }
    }
  }
}

size_t DisplayList::rasterizeTreeTiles(const Surface* surface,
                                       const std::vector<std::pair<int, int>>& cells,
                                       TreeLevelCoverage* coverage,
                                       std::vector<DrawTask>* tileTasks) {
  if (cells.empty()) {
    return 0;
  }
  auto treeTiles = getFreeTiles(surface, TileType::Tree, cells.size());
  for (size_t i = 0; i < treeTiles.size(); ++i) {
    auto& tile = treeTiles[i];
    tile->tileX = cells[i].first;
    tile->tileY = cells[i].second;
    tilePool->insert(TileKey::MakeTree(tile->tileX, tile->tileY, coverage->level), tile);
    coverage->tileAt(tile->tileX, tile->tileY) = tile;
    DrawTask task(tile, _tileSize);
    task.setTreeLevel(coverage->level);
    task.setCoversTiles(true);
    tileTasks->push_back(std::move(task));
  }
  return treeTiles.size();
}

std::vector<std::shared_ptr<Tile>> DisplayList::getFreeTiles(const Surface* renderSurface,
                                                             TileType type, size_t tileCount) {
  std::vector<std::shared_ptr<Tile>> tiles = {};
  tiles.reserve(tileCount);
  while (emptyTiles.size() < tileCount && createEmptyTiles(renderSurface)) {
  }
  auto emptyCount = std::min(tileCount, emptyTiles.size());
  tiles.insert(tiles.end(), emptyTiles.end() - static_cast<std::ptrdiff_t>(emptyCount),
               emptyTiles.end());
  emptyTiles.resize(emptyTiles.size() - emptyCount);
  auto evictInto = [&](TileType evictType, size_t count) {
    auto evicted = tilePool->evict(evictType, std::min(count, tileCount - tiles.size()));
    gCaptureProbe.evicted[evictType == TileType::Grid ? 0 : 1] += static_cast<int>(evicted.size());
    tiles.insert(tiles.end(), evicted.begin(), evicted.end());
  };
  const auto otherType = type == TileType::Grid ? TileType::Tree : TileType::Grid;
  if (tilePool->size(otherType) > tilePool->quota(otherType)) {
    evictInto(otherType, tilePool->size(otherType) - tilePool->quota(otherType));
  }
  evictInto(type, tileCount);
  if (tiles.size() == tileCount) {
    return tiles;
  }
  if (type == TileType::Tree) {
    // Tree tiles are only requested when nothing else can fill the screen, which outranks
    // sharpness.
    evictInto(TileType::Grid, tileCount);
    return tiles;
  }
  // Grid tiles may borrow from the tree tiles as long as the grid stays within its quota and the
  // tree levels can still cover the viewport.
  auto gridCount = tilePool->size(TileType::Grid) + tiles.size();
  auto gridQuota = tilePool->quota(TileType::Grid);
  auto treeCount = tilePool->size(TileType::Tree);
  if (gridCount < gridQuota && treeCount > minTreeTileCount) {
    evictInto(TileType::Tree, std::min(gridQuota - gridCount, treeCount - minTreeTileCount));
  }
  return tiles;
}

std::vector<std::shared_ptr<Tile>> DisplayList::createContinuousTiles(const Surface* renderSurface,
                                                                      int requestCountX,
                                                                      int requestCountY) {
  DEBUG_ASSERT(renderSurface != nullptr);
  auto context = renderSurface->getContext();
  auto tileCount = nextSurfaceTileCount(context);
  if (tileCount <= 0) {
    return {};
  }
  auto requestCount = requestCountX * requestCountY;
  if (tileCount < requestCount) {
    return {};
  }
  int countX = 0;
  int countY = 0;
  GetAtlasTileGrid(tileCount, &countX, &countY);
  if (countX < requestCountX) {
    countX = requestCountX;
    countY = FloatCeilToInt(static_cast<float>(tileCount) / static_cast<float>(countX));
  } else if (countY < requestCountY) {
    countY = requestCountY;
    countX = FloatCeilToInt(static_cast<float>(tileCount) / static_cast<float>(countY));
  }
  auto maxAtlasSize = std::min(context->gpu()->limits()->maxTextureDimension2D, MAX_ATLAS_SIZE);
  if (countX * _tileSize > maxAtlasSize || countY * _tileSize > maxAtlasSize) {
    return {};
  }
  auto surface =
      Surface::Make(context, countX * _tileSize, countY * _tileSize, ColorType::RGBA_8888, 1, false,
                    renderSurface->renderFlags(), renderSurface->colorSpace());
  if (surface == nullptr) {
    return {};
  }
  surfaceCaches.push_back(std::move(surface));
  auto surfaceIndex = surfaceCaches.size() - 1;
  auto emptyCount = countX * countY - requestCount;
  emptyTiles.reserve(emptyTiles.size() + static_cast<size_t>(emptyCount));
  std::vector<std::shared_ptr<Tile>> renderTiles = {};
  renderTiles.reserve(static_cast<size_t>(requestCount));
  for (int y = 0; y < countY; ++y) {
    for (int x = 0; x < countX; ++x) {
      auto tile = std::make_shared<Tile>();
      tile->sourceIndex = surfaceIndex;
      tile->sourceX = x;
      tile->sourceY = y;
      if (x < requestCountX && y < requestCountY) {
        renderTiles.push_back(std::move(tile));
      } else {
        emptyTiles.push_back(std::move(tile));
      }
    }
  }
  return renderTiles;
}

bool DisplayList::createEmptyTiles(const Surface* renderSurface) {
  DEBUG_ASSERT(renderSurface != nullptr);
  auto context = renderSurface->getContext();
  auto tileCount = nextSurfaceTileCount(context);
  if (tileCount <= 0) {
    return false;
  }
  int countX = 0;
  int countY = 0;
  GetAtlasTileGrid(tileCount, &countX, &countY);
  auto surface =
      Surface::Make(context, countX * _tileSize, countY * _tileSize, ColorType::RGBA_8888, 1, false,
                    renderSurface->renderFlags(), renderSurface->colorSpace());
  if (surface == nullptr) {
    return false;
  }
  surfaceCaches.push_back(std::move(surface));
  auto surfaceIndex = surfaceCaches.size() - 1;
  emptyTiles.reserve(emptyTiles.size() + static_cast<size_t>(countX * countY));
  for (int y = 0; y < countY; ++y) {
    for (int x = 0; x < countX; ++x) {
      auto tile = std::make_shared<Tile>();
      tile->sourceIndex = surfaceIndex;
      tile->sourceX = x;
      tile->sourceY = y;
      emptyTiles.push_back(std::move(tile));
    }
  }
  return !emptyTiles.empty();
}

int DisplayList::nextSurfaceTileCount(Context* context) const {
  DEBUG_ASSERT(context != nullptr);
  int surfaceTileCount = 0;
  for (auto& surface : surfaceCaches) {
    auto tileCountX = surface->width() / _tileSize;
    auto tileCountY = surface->height() / _tileSize;
    surfaceTileCount += tileCountX * tileCountY;
  }
  if (surfaceTileCount >= totalTileCount) {
    return 0;
  }
  auto maxTileCountPerAtlas = getMaxTileCountPerAtlas(context);
  return std::min(totalTileCount - surfaceTileCount, maxTileCountPerAtlas);
}

int DisplayList::getMaxTileCountPerAtlas(Context* context) const {
  auto maxTextureSize = std::min(context->gpu()->limits()->maxTextureDimension2D, MAX_ATLAS_SIZE);
  return (maxTextureSize / _tileSize) * (maxTextureSize / _tileSize);
}

float DisplayList::getTileDrawGeometry(const DrawTask& task, Matrix* viewMatrix,
                                       Rect* clipRect) const {
  auto rasterScale = task.treeLevel() == EXACT_LEVEL
                         ? ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision)
                         : GetTileRasterScale(task.treeLevel());
  DEBUG_ASSERT(rasterScale != 0.0f);
  *viewMatrix = Matrix::MakeScale(rasterScale);
  auto& tileRect = task.tileRect();
  auto& sourceRect = task.sourceRect();
  auto offsetX = sourceRect.left - tileRect.left;
  auto offsetY = sourceRect.top - tileRect.top;
  viewMatrix->postTranslate(offsetX, offsetY);
  *clipRect = tileRect;
  clipRect->offset(offsetX, offsetY);
  return rasterScale;
}

// The global rect a draw covers, matching the cull rect drawRootLayer hands to the layer tree.
static Rect GlobalRenderRect(const Rect& drawRect, const Matrix& viewMatrix) {
  Matrix inverse = Matrix::I();
  viewMatrix.invert(&inverse);
  auto renderRect = inverse.mapRect(drawRect);
  renderRect.roundOut();
  return renderRect;
}

bool DisplayList::drawTileTask(const DrawTask& task, BackgroundSnapshotMap* snapshots,
                               bool snapshotsCover, int* missingBackdrops) const {
  GpuProbeTag probeTag("tile");
  auto surface = surfaceCaches[task.sourceIndex()].get();
  DEBUG_ASSERT(surface != nullptr);
  auto canvas = surface->getCanvas();
  AutoCanvasRestore autoRestore(canvas);
  Matrix viewMatrix = Matrix::I();
  Rect clipRect = {};
  getTileDrawGeometry(task, &viewMatrix, &clipRect);
  return drawRootLayer(surface, clipRect, viewMatrix, true, snapshots, true, snapshotsCover,
                       missingBackdrops);
}

static std::tuple<int, int, int> ForceCaptureKey(const DrawTask& task, const Tile& tile) {
  return {task.treeLevel(), tile.tileX, tile.tileY};
}

bool DisplayList::isForceCaptured(const DrawTask& task) const {
  for (const auto& tile : task.getTiles()) {
    if (std::find(forceCaptureTiles.begin(), forceCaptureTiles.end(),
                  ForceCaptureKey(task, *tile)) != forceCaptureTiles.end()) {
      return true;
    }
  }
  return false;
}

void DisplayList::updateForceCapture(const DrawTask& task, bool skippedStyles) {
  for (const auto& tile : task.getTiles()) {
    auto key = ForceCaptureKey(task, *tile);
    auto found = std::find(forceCaptureTiles.begin(), forceCaptureTiles.end(), key);
    if (!skippedStyles) {
      if (found != forceCaptureTiles.end()) {
        forceCaptureTiles.erase(found);
      }
    } else if (found == forceCaptureTiles.end()) {
      forceCaptureTiles.push_back(key);
    }
  }
}

bool DisplayList::collectMissingBackdrops(const DrawTask& task,
                                          std::vector<Rect>* missingRects) const {
  Matrix viewMatrix = Matrix::I();
  Rect clipRect = {};
  auto rasterScale = getTileDrawGeometry(task, &viewMatrix, &clipRect);
  std::vector<Rect> drawnBounds = {};
  collectMissingBackdrops(_root.get(), GlobalRenderRect(clipRect, viewMatrix), 1.0f, rasterScale,
                          false, &drawnBounds, missingRects);
  return !missingRects->empty();
}

// Mirrors the order Layer::drawLayer paints a tile in: a layer's Below styles before its
// children, children in order. Any condition that sends drawing to an offscreen canvas makes the
// styles below it read something other than the tile, and keeps their outputs out of the cache.
void DisplayList::collectMissingBackdrops(Layer* layer, const Rect& globalRect, float parentAlpha,
                                          float scale, bool offscreen,
                                          std::vector<Rect>* drawnBounds,
                                          std::vector<Rect>* missingRects) const {
  // The root is drawn without its own visibility, alpha or matrix, so only its children apply them.
  const bool isRoot = layer == _root.get();
  if (!isRoot && (!layer->bitFields.visible || layer->_alpha <= 0 || layer->maskOwner != nullptr)) {
    return;
  }
  if (!Rect::Intersects(layer->renderBounds, globalRect) ||
      !Rect::Intersects(layer->backgroundStyleBounds, globalRect)) {
    return;
  }
  const float alpha = isRoot ? 1.0f : parentAlpha * layer->_alpha;
  const auto layerScale =
      isRoot ? scale : scale * layer->getMatrixWithScrollRect().asMatrix().getMaxScale();
  const auto blendMode =
      isRoot ? BlendMode::SrcOver : static_cast<BlendMode>(layer->bitFields.blendMode);
  // A subtree with blend modes may be drawn through a pass-through offscreen seeded with the tile,
  // and a perspective matrix projects an offscreen image of the subtree.
  offscreen = offscreen || (!isRoot && !layer->bitFields.matrix3DIsAffine) ||
              layer->canPreserve3D() || blendMode != BlendMode::SrcOver ||
              layer->bitFields.hasBlendMode || !layer->bitFields.passThroughBackground ||
              (alpha < 1.0f && layer->bitFields.allowsGroupOpacity) || !layer->_filters.empty() ||
              layer->hasValidMask();
  for (const auto& style : layer->_layerStyles) {
    // The same gates the draw path applies before dispatching the style to the consumer.
    if (!layer->needsBackgroundStyleOutput(style.get(), alpha) ||
        BackgroundHandler::IsNegligibleBackgroundStyle(style.get(), layerScale)) {
      continue;
    }
    const bool onTile = !offscreen && style->position() == LayerStylePosition::Below;
    const bool cached = onTile && styleOutputCache != nullptr &&
                        styleOutputCache->contains(BackgroundSnapshotKey{layer, style.get()});
    if (!cached &&
        !(onTile && BackgroundConsumer::CanReadTileBackdrop(layer, globalRect, *drawnBounds))) {
      // A style drawn into an offscreen composite (a 3D context, a pass-through group) samples the
      // backdrop the composite was seeded with, which spans more than the style's own bounds.
      auto missingRect = offscreen ? globalRect : layer->renderBounds;
      if (missingRect.intersect(globalRect)) {
        missingRects->push_back(missingRect);
      }
    }
    // Whatever its source, the style's output ends up in the tile, where later styles sample it,
    // unless the layer's own opaque content hides it.
    if (!BackgroundConsumer::CoversOwnStyleOutput(layer, alpha)) {
      drawnBounds->push_back(layer->renderBounds);
    }
  }
  for (const auto& child : layer->_children) {
    collectMissingBackdrops(child.get(), globalRect, alpha, layerScale, offscreen, drawnBounds,
                            missingRects);
  }
}

void DisplayList::drawScreenTasks(std::vector<DrawTask> screenTasks, std::vector<Rect> skippedRects,
                                  Surface* surface, bool autoClear) const {
  // Sort tasks by surface index to ensure they are drawn in batches.
  std::sort(screenTasks.begin(), screenTasks.end(),
            [](const DrawTask& a, const DrawTask& b) { return a.sourceIndex() < b.sourceIndex(); });
  auto canvas = surface->getCanvas();
  AutoCanvasRestore autoRestore(canvas);
  Paint paint = {};
  paint.setAntiAlias(false);
  if (autoClear) {
    paint.setBlendMode(BlendMode::Src);
  }
  static SamplingOptions nearestSampling(FilterMode::Nearest, MipmapMode::None);
  static SamplingOptions linearSampling(FilterMode::Linear, MipmapMode::None);
  canvas->setMatrix(Matrix::MakeTrans(_contentOffset.x, _contentOffset.y));
  Rect tileRect = {};
  for (auto& task : screenTasks) {
    auto surfaceCache = surfaceCaches[task.sourceIndex()];
    DEBUG_ASSERT(surfaceCache != nullptr);
    auto image = surfaceCache->makeImageSnapshot();
    auto& sampling = task.identityScale() ? nearestSampling : linearSampling;
    canvas->drawImageRect(image, task.sourceRect(), task.tileRect(), sampling, &paint,
                          SrcRectConstraint::Strict, &task.strictRect());
    tileRect.join(task.tileRect());
  }

  auto screenRect = Rect::MakeWH(surface->width(), surface->height());
  screenRect.offset(-_contentOffset.x, -_contentOffset.y);

  paint.setColor(_backgroundColor);
  for (auto& rect : skippedRects) {
    canvas->drawRect(rect, paint);
    tileRect.join(rect);
  }

  if (tileRect.contains(screenRect)) {
    return;
  }

  const auto currentZoomScale = ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision);
  auto renderBounds = _root->renderBounds;
  renderBounds.scale(currentZoomScale, currentZoomScale);
  renderBounds.roundOut();
  // Clip to render bounds because fallback tiles may extend beyond content due to roundOut.
  tileRect.intersect(renderBounds);

  paint.setColor(_backgroundColor);
  auto backgroundRect = GetNonIntersectingRects(screenRect, tileRect);
  for (auto& rect : backgroundRect) {
    canvas->drawRect(rect, paint);
  }
}

void DisplayList::renderDirtyRegions(Canvas* canvas, std::vector<Rect> dirtyRegions) {
  if (lastDirtyRegions.empty()) {
    for (size_t i = 0; i < MAX_DIRTY_REGION_FRAMES; ++i) {
      lastDirtyRegions.emplace_back();
    }
  }
  lastDirtyRegions.push_back(std::move(dirtyRegions));
  lastDirtyRegions.pop_front();
  Paint dirtyPaint = {};
  dirtyPaint.setAntiAlias(false);
  dirtyPaint.setColor(Color::Red());
  dirtyPaint.setAlpha(0.15f);
  AutoCanvasRestore autoRestore(canvas);
  canvas->resetMatrix();
  for (auto& regions : lastDirtyRegions) {
    for (auto& region : regions) {
      canvas->drawRect(region, dirtyPaint);
    }
  }
}

Matrix DisplayList::getViewMatrix() const {
  auto viewMatrix = Matrix::MakeScale(ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision));
  viewMatrix.postTranslate(_contentOffset.x, _contentOffset.y);
  return viewMatrix;
}

void DisplayList::resetCaches() {
  tilePool->clear(TileType::Grid);
  tilePool->clear(TileType::Tree);
  staleGridScaleInt = 0;
  blankTiles->clear();
  surfaceCaches = {};
  totalTileCount = 0;
  emptyTiles.clear();
  styleOutputCache = nullptr;
  forceCaptureTiles.clear();
}

bool DisplayList::drawRootLayer(Surface* surface, const Rect& drawRect, const Matrix& viewMatrix,
                                bool autoClear, BackgroundSnapshotMap* snapshots,
                                bool readTileBackdrop, bool snapshotsCover,
                                int* missingBackdrops) const {
  DEBUG_ASSERT(surface != nullptr);
  auto canvas = surface->getCanvas();
  auto context = surface->getContext();
  AutoCanvasRestore autoRestore(canvas);
  auto surfaceRect = Rect::MakeWH(surface->width(), surface->height());
  bool fullScreen = drawRect == surfaceRect;
  if (!fullScreen) {
    canvas->clipRect(drawRect, false);
  }
  if (autoClear) {
    canvas->clear();
  }
  canvas->setMatrix(viewMatrix);
  DrawArgs args(context);
  DEBUG_ASSERT(viewMatrix.invertible());
  auto renderRect = GlobalRenderRect(drawRect, viewMatrix);
  std::vector<Rect> renderRectsVec = {renderRect};
  args.renderRects = &renderRectsVec;
  args.dstColorSpace = surface->colorSpace();
  args.subtreeCacheMaxSize = _subtreeCacheMaxSize;
  args.tileBudget = tileBudget;
  // Draw background color before the layer tree. This ensures backgroundColor is only rendered
  // through the DisplayList::render() path, not when calling Layer::draw() directly.
  canvas->drawColor(_backgroundColor, BlendMode::SrcOver);
  // Consume pass: replay snapshots produced by the capture pass. When the tree has no
  // background-sourced styles, snapshots is null and we fall back to NoOp, which makes
  // background-sourced styles (if any show up unexpectedly) silently no-op — matching the
  // contour / 3D subtree semantics.
  // SrcOver compositing of the shared style output matches the style's own Src draw only over
  // an opaque backdrop, and caching only pays off when the frame renders through multiple
  // passes, so sharing is gated on both.
  BackgroundConsumer consumer(snapshots, snapshots != nullptr && _backgroundColor.isOpaque() &&
                                             snapshots->multiPass && snapshots->styleShareCompact);
  const bool tileBackdrop = readTileBackdrop && _root->hasBackgroundStyle(1.0f);
  if (tileBackdrop) {
    BackgroundConsumer::TileBackdrop backdrop = {};
    backdrop.surface = surface;
    backdrop.globalRect = renderRect;
    backdrop.snapshotsCover = snapshotsCover;
    backdrop.styleOutputs = styleOutputCache.get();
    // Every tile is rasterized in the frame it gets dirty, so an output is only reused when no
    // dirty region touched it, which a redrawn tile rarely allows; storing it would cost a
    // rasterization of the whole content for nothing.
    backdrop.storeStyleOutputs = false;
    consumer.setTileBackdrop(backdrop);
  }
  args.backgroundHandler = snapshots || tileBackdrop ? &consumer : BackgroundHandler::NoOp();
  // The clear and the background color above must not count as content.
  auto drawingManager = context->drawingManager();
  auto drawMark = drawingManager->drawMark();
  _root->drawLayer(args, surface->getCanvas(), 1.0f, BlendMode::SrcOver);
  if (missingBackdrops != nullptr) {
    *missingBackdrops = consumer.missingCount;
  }
  return drawingManager->hasDrawnSince(drawMark);
}

// Merges the rects that overlap or touch, so many styles sitting next to each other do not multiply
// the per-layer cull loop. Every join restarts the scan and a scan is O(n^2), so the loop is O(n^3)
// in the worst case: the callers size the list before calling.
void DisplayList::mergeCullRects(std::vector<Rect>* rects) {
  bool merged = true;
  while (merged) {
    merged = false;
    for (size_t i = 0; i < rects->size() && !merged; ++i) {
      for (size_t j = i + 1; j < rects->size(); ++j) {
        auto& current = (*rects)[i];
        const auto& other = (*rects)[j];
        auto joined = current;
        joined.join(other);
        // Merge when the bounding rect is no larger than the two areas combined: always true for a
        // shared edge or containment, and for overlaps that are nearly aligned, while a real gap is
        // left alone. The bounding rect may add a little area, which only draws a little more.
        auto joinedArea = joined.width() * joined.height();
        if (joinedArea <= current.width() * current.height() + other.width() * other.height()) {
          current = joined;
          rects->erase(rects->begin() + static_cast<long>(j));
          merged = true;
          break;
        }
      }
    }
  }
}

// Builds the list a capture culls its draw against. Merging only pays for a small list, and when
// too many groups stay apart, bounding them per captured rect keeps every rect inside its captured
// rect and still drops the rects no style reaches. Only when even that exceeds the budget, or
// covers more than the union does, the union is used, which may also span the gaps between captured
// rects. A union that spans those gaps wider than the captured rects themselves is not worth it:
// the capture then draws everything it would have drawn anyway, so the list is left empty and the
// caller keeps the captured rects.
std::vector<Rect> DisplayList::buildCullRects(const std::vector<Rect>& footprints,
                                              const std::vector<Rect>& captureRects) {
  auto narrowed = narrowCullRects(footprints, captureRects);
  if (narrowed.empty()) {
    return {};
  }
  if (narrowed.size() <= 4 * MAX_CULL_RECTS) {
    mergeCullRects(&narrowed);
    if (narrowed.size() <= MAX_CULL_RECTS) {
      return narrowed;
    }
  }
  auto unionRect = Rect::MakeEmpty();
  for (const auto& rect : narrowed) {
    unionRect.join(rect);
  }
  auto captureArea = 0.0f;
  for (const auto& rect : captureRects) {
    captureArea += rect.width() * rect.height();
  }
  auto unionArea = unionRect.width() * unionRect.height();
  auto perCapture = std::vector<Rect>{};
  perCapture.reserve(captureRects.size());
  for (const auto& captureRect : captureRects) {
    auto bounds = Rect::MakeEmpty();
    // Captured rects overlap once they are widened by the blur reach, so a piece narrowed to a
    // neighbour may reach into this one. Clipping it first keeps the bounds inside this rect, and
    // the neighbour still covers the piece in full.
    for (const auto& rect : narrowed) {
      auto clipped = rect;
      if (clipped.intersect(captureRect)) {
        bounds.join(clipped);
      }
    }
    if (!bounds.isEmpty()) {
      perCapture.push_back(bounds);
    }
  }
  // A frame that repaints more captured rects than the per-layer loop can afford has no list to
  // give: bounding them per rect would already exceed the budget, so it falls back to the union.
  if (perCapture.size() > MAX_CULL_RECTS) {
    return unionArea > captureArea ? std::vector<Rect>{} : std::vector<Rect>{unionRect};
  }
  auto perCaptureArea = 0.0f;
  for (const auto& rect : perCapture) {
    perCaptureArea += rect.width() * rect.height();
  }
  if (perCaptureArea <= unionArea) {
    return perCapture;
  }
  return unionArea > captureArea ? std::vector<Rect>{} : std::vector<Rect>{unionRect};
}

// A style reads the captured background under the recording it resolves, and only where it shows:
// within the blur reach of its visible region, which is what the footprint covers. The recording
// measures the same geometry the layer bounds do, but it is rounded out to the layer's own pixels,
// which are no larger than the captured ones since both follow the capture canvas scale, and the
// copy is then rounded out to the captured pixels. Widening a footprint by one captured pixel and
// rounding it out to that grid covers both roundings. A rotated layer reads no farther: the style
// resamples the copy on its own grid inside its content bounds.
static Rect RoundOutToCapturedPixels(const Rect& footprint, const Matrix& worldToPixel,
                                     const Matrix& pixelToWorld) {
  auto pixelRect = worldToPixel.mapRect(footprint);
  pixelRect.outset(1.0f, 1.0f);
  pixelRect.roundOut();
  return pixelToWorld.mapRect(pixelRect);
}

// Returns the world rects the capture may cull its draw to, or an empty list when the whole
// captured area has to be drawn. `worldRects` are the captured rects, already widened by the blur
// reach, and `pixelToWorld` maps a pixel of the captured background to the world.
std::vector<Rect> DisplayList::computeCaptureCullRects(const std::vector<Rect>& worldRects,
                                                       const Matrix& pixelToWorld) const {
  if (!captureCullingEnabledForTests) {
    return {};
  }
  Matrix worldToPixel = Matrix::I();
  if (!pixelToWorld.invert(&worldToPixel)) {
    return {};
  }
  auto captureBounds = Rect::MakeEmpty();
  for (const auto& rect : worldRects) {
    captureBounds.join(rect);
  }
  // An empty list means no style samples inside this capture, so there is nothing to narrow the
  // draw to, but the capture still runs: keeping the whole area only costs the culling for this
  // frame.
  auto footprints = collectBackgroundFootprints(captureBounds);
  if (footprints.empty()) {
    return {};
  }
  for (auto& footprint : footprints) {
    footprint = RoundOutToCapturedPixels(footprint, worldToPixel, pixelToWorld);
  }
  return buildCullRects(footprints, worldRects);
}

// Intersects each footprint with each captured rect instead of with their union, so the narrowed
// cull never covers more than the capture would have drawn without the footprints. Footprints that
// overlap no captured rect are dropped.
std::vector<Rect> DisplayList::narrowCullRects(const std::vector<Rect>& footprints,
                                               const std::vector<Rect>& captureRects) {
  std::vector<Rect> narrowed = {};
  narrowed.reserve(footprints.size());
  for (const auto& footprint : footprints) {
    for (const auto& captureRect : captureRects) {
      auto clipped = footprint;
      if (clipped.intersect(captureRect)) {
        narrowed.push_back(clipped);
      }
    }
  }
  return narrowed;
}

// Walks only the subtrees that hold a background style. A style blurs the backdrop of its whole
// content, which its render bounds cover, so the footprint of a layer nothing cuts also covers
// every style below it until a scrollRect or a mask cuts that subtree again. A cut layer still
// blurs the backdrop of its whole content, so a pixel next to the cut reads up to the blur reach
// beyond it, and widening the footprint by that reach covers every style below it for good.
// `clipped` tells whether this layer or an ancestor is cut, and `covered` whether a footprint above
// already covers this layer.
//
// The culling is right only while a style reads no backdrop outside its footprint: the recording it
// resolves has to stay inside the layer bounds up to its pixel rounding, and a cut layer may read
// the reach beyond those bounds, which is why it is widened here. A shape measured wider than it
// draws would break that, so the stroke measurement of rects, round rects and ovals stopped adding
// a miter those shapes cannot reach.
void DisplayList::collectBackgroundFootprints(const Layer* layer, const Rect& bounds, bool clipped,
                                              bool covered, std::vector<Rect>* footprints,
                                              float alpha) {
  if (layer->maxBackgroundOutset <= 0 || !layer->bitFields.visible || layer->_alpha <= 0 ||
      !Rect::Intersects(bounds, layer->renderBounds)) {
    return;
  }
  if (layer->_scrollRect != nullptr || layer->hasValidMask()) {
    clipped = true;
    covered = false;
  }
  if (!covered && layer->hasOwnBackgroundStyle(alpha)) {
    auto footprint = layer->renderBounds;
    if (clipped) {
      footprint.outset(layer->maxBackgroundOutset, layer->maxBackgroundOutset);
    }
    footprints->push_back(footprint);
    if (clipped) {
      return;
    }
    covered = true;
  }
  for (const auto& child : layer->_children) {
    collectBackgroundFootprints(child.get(), bounds, clipped, covered, footprints,
                                alpha * child->_alpha);
  }
}

std::vector<Rect> DisplayList::collectBackgroundFootprints(const Rect& worldBounds) const {
  std::vector<Rect> footprints = {};
  collectBackgroundFootprints(_root.get(), worldBounds, false, false, &footprints, 1.0f);
  return footprints;
}

bool DisplayList::HasVisibleBackgroundStyle(const Layer* layer, const Rect& unionRect,
                                            const std::vector<Rect>& rects, float scale) {
  if (layer->maxBackgroundOutset <= 0 || !Rect::Intersects(unionRect, layer->renderBounds)) {
    return false;
  }
  // The root is drawn without its own visibility, alpha or matrix, so only its children apply them.
  auto isRoot = layer->_parent == nullptr;
  if (!isRoot && (!layer->bitFields.visible || layer->_alpha <= 0)) {
    return false;
  }
  auto layerScale =
      isRoot ? scale : scale * layer->getMatrixWithScrollRect().asMatrix().getMaxScale();
  for (const auto& style : layer->_layerStyles) {
    if (style == nullptr || (style->extraSourceType() &
                             static_cast<uint32_t>(LayerStyleExtraSourceType::Background)) == 0) {
      continue;
    }
    // The draw path leaves such a style out entirely, so nothing would read its snapshot.
    if (BackgroundHandler::IsNegligibleBackgroundStyle(style.get(), layerScale)) {
      continue;
    }
    // The union covers the gaps between scattered rects; the capture culls against each rect, so
    // a layer that only touches a gap would produce nothing.
    for (const auto& rect : rects) {
      if (Rect::Intersects(rect, layer->renderBounds)) {
        return true;
      }
    }
    return false;
  }
  for (const auto& child : layer->_children) {
    if (HasVisibleBackgroundStyle(child.get(), unionRect, rects, layerScale)) {
      return true;
    }
  }
  return false;
}

std::vector<Rect> DisplayList::makeStyleSourceRects(const std::vector<Rect>& renderRects,
                                                    const Matrix& viewMatrix) const {
  Matrix inverse = Matrix::I();
  if (!viewMatrix.invert(&inverse)) {
    return {};
  }
  std::vector<Rect> worldRects = {};
  worldRects.reserve(renderRects.size());
  for (const auto& rect : renderRects) {
    if (rect.isEmpty()) {
      continue;
    }
    auto worldRect = inverse.mapRect(rect);
    worldRect.roundOut();
    worldRect.outset(_root->maxBackgroundOutset, _root->maxBackgroundOutset);
    worldRects.push_back(worldRect);
  }
  return worldRects;
}

std::unique_ptr<BackgroundSnapshotMap> DisplayList::captureBackgrounds(
    Surface* surface, const std::vector<Rect>& renderRects, const Matrix& viewMatrix,
    const std::vector<Rect>& styleSourceRects) const {
  GpuProbeTag probeTag("capture");
  DEBUG_ASSERT(surface != nullptr);
  if (!_root->hasBackgroundStyle(1.0f)) {
    return nullptr;
  }
  if (renderRects.empty()) {
    return nullptr;
  }
  auto context = surface->getContext();
  if (context == nullptr) {
    return nullptr;
  }
  DEBUG_ASSERT(viewMatrix.invertible());
  // createBackgroundSource takes a surface-pixel-space rect for sizing; BackgroundCapturer::Run
  // takes world-space renderRects (pre-outset by blur sampling range) for per-rect culling.
  Matrix inverse = Matrix::I();
  viewMatrix.invert(&inverse);
  Rect surfaceUnion = Rect::MakeEmpty();
  std::vector<Rect> worldRects = {};
  worldRects.reserve(renderRects.size());
  for (const auto& drawRect : renderRects) {
    if (drawRect.isEmpty()) {
      continue;
    }
    surfaceUnion.join(drawRect);
    auto worldRect = inverse.mapRect(drawRect);
    worldRect.roundOut();
    worldRects.push_back(worldRect);
  }
  if (worldRects.empty()) {
    return nullptr;
  }
  auto visibleRects = worldRects;
  // A background style whose blur radius is under a pixel on the device cannot change the picture:
  // it is the same rule the draw path uses to skip such a style. When every style in range is like
  // that, the capture would draw the tree and produce nothing, so skip it, because a capture that
  // produces no snapshot is pure cost.
  auto deviceScale = viewMatrix.getMaxScale();
  // Quick reject: renderBounds covers the subtree, so one test against the union of the rects
  // settles whether any layer inside can contribute at all.
  Rect unionRect = Rect::MakeEmpty();
  for (const auto& rect : worldRects) {
    unionRect.join(rect);
  }
  if (!HasVisibleBackgroundStyle(_root.get(), unionRect, worldRects, deviceScale)) {
    return nullptr;
  }
  // Expand by the max blur outset so layers whose bounds sit just outside the dirty rects but
  // still contribute pixels to the blur sampling region are not culled by the capture pass.
  for (auto& rect : worldRects) {
    rect.outset(_root->maxBackgroundOutset, _root->maxBackgroundOutset);
  }
  DrawArgs args(context);
  args.dstColorSpace = surface->colorSpace();
  args.subtreeCacheMaxSize = _subtreeCacheMaxSize;
  args.tileBudget = tileBudget;
  auto bgSource =
      _root->createBackgroundSource(context, surfaceUnion, viewMatrix, false, args.dstColorSpace);
  if (bgSource == nullptr) {
    return nullptr;
  }
  // The layers to draw can be narrowed to the area the background styles sample, while the surface
  // still covers every render rect: keeping the captured area unchanged keeps the multi-pass
  // structure (and the style output sharing that depends on it) intact.
  auto culledRects = computeCaptureCullRects(worldRects, bgSource->backgroundMatrix());
  auto cullWorldRects = worldRects;
  if (!culledRects.empty()) {
    cullWorldRects = std::move(culledRects);
  }
  auto snapshotMap = std::make_unique<BackgroundSnapshotMap>();
  snapshotMap->multiPass = renderRects.size() > 1;
  snapshotMap->visibleRects = std::move(visibleRects);
  snapshotMap->renderTargetWidth = surface->width();
  snapshotMap->renderTargetHeight = surface->height();
  {
    auto unionRect = Rect::MakeEmpty();
    for (const auto& rect : renderRects) {
      unionRect.join(rect);
    }
    auto sumArea = 0.0;
    for (const auto& rect : renderRects) {
      sumArea += static_cast<double>(rect.width()) * static_cast<double>(rect.height());
    }
    constexpr double MaxUnionOverSum = 2.0;
    snapshotMap->styleShareCompact =
        sumArea <= 0.0 ||
        static_cast<double>(unionRect.width()) * static_cast<double>(unionRect.height()) <=
            MaxUnionOverSum * sumArea;
  }
  // Draw backgroundColor before the layer tree so that the capture pass includes it as part of
  // the background for blur/backdrop effects.
  if (_backgroundColor != Color::Transparent()) {
    bgSource->getCanvas()->drawColor(_backgroundColor, BlendMode::SrcOver);
  }
  // The cached layer style sources are reused by every pass drawing with this map, so they follow
  // styleSourceRects when the caller passes a wider area than the captured one.
  snapshotMap->styleSourceRects =
      styleSourceRects.empty() ? worldRects : makeStyleSourceRects(styleSourceRects, viewMatrix);
  gCaptureProbe.inCapture = true;
  BackgroundCapturer::Run(_root.get(), args, std::move(bgSource), snapshotMap.get(), cullWorldRects,
                          snapshotMap->styleSourceRects, 1.0f);
  gCaptureProbe.inCapture = false;
  return snapshotMap;
}

void DisplayList::updateMousePosition() {
  if (lastZoomScaleInt == _zoomScaleInt) {
    mousePosition += (lastContentOffset - _contentOffset);
    return;
  }

  const auto scale = ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision) /
                     ToZoomScaleFloat(lastZoomScaleInt, _zoomScalePrecision);
  DEBUG_ASSERT(scale != 1.0f);
  const auto invScaleFactor = 1.0f / (1.0f - scale);
  mousePosition = (_contentOffset - lastContentOffset * scale) * invScaleFactor;
  mousePosition -= _contentOffset;
}

}  // namespace tgfx

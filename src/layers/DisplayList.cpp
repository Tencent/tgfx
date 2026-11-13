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
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "core/utils/DecomposeRects.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/TileSortCompareFunc.h"
#include "gpu/DrawingManager.h"
#include "layers/BackgroundHandler.h"
#include "layers/BackgroundSnapshotMap.h"
#include "layers/BackgroundSource.h"
#include "layers/DrawArgs.h"
#include "layers/RootLayer.h"
#include "layers/TileCluster.h"
#include "layers/TilePool.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/layers/layerstyles/BackgroundBlurStyle.h"

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
// ClusterTileRects growth allowed when merging footprints, and the number of rects the per-layer
// cull loop can afford.
static constexpr double FOOTPRINT_CLUSTER_GROWTH = 4.0;
static constexpr size_t MAX_CULL_RECTS = 32;

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
   * Returns the level of the tree tiles this task rasterizes, or kExactLevel if it rasterizes grid
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

 private:
  // Hold strong references to tiles to ensure the pool doesn't evict them.
  std::vector<std::shared_ptr<Tile>> tiles = {};
  Rect _sourceRect = {};
  Rect _tileRect = {};
  Rect _strictRect = {};
  bool _identityScale = true;
  bool _coversTiles = false;
  int _treeLevel = kExactLevel;

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

// Looks up every tile of the levels L to L+3 inside the viewport. Hits are touched regardless of
// whether the frame displays them, which is what keeps the coarse levels from being evicted.
static std::vector<TreeLevelCoverage> CollectTreeCoverages(TilePool* tilePool,
                                                           const BlankTileSet* blankTiles,
                                                           float zoomScale, const Rect& renderRect,
                                                           int tileSize) {
  std::vector<TreeLevelCoverage> coverages = {};
  coverages.reserve(TREE_LEVEL_COUNT);
  auto baseLevel = GetTileLevel(zoomScale);
  auto tileSizeFloat = static_cast<float>(tileSize);
  for (int i = 0; i < TREE_LEVEL_COUNT; ++i) {
    TreeLevelCoverage coverage = {};
    coverage.level = baseLevel + i;
    coverage.zoomToRaster = GetTileRasterScale(coverage.level) / zoomScale;
    auto rasterRect = coverage.toRaster(renderRect);
    coverage.startX = FloatFloorToInt(rasterRect.left / tileSizeFloat);
    coverage.startY = FloatFloorToInt(rasterRect.top / tileSizeFloat);
    coverage.countX = FloatCeilToInt(rasterRect.right / tileSizeFloat) - coverage.startX;
    coverage.countY = FloatCeilToInt(rasterRect.bottom / tileSizeFloat) - coverage.startY;
    auto cellCount = static_cast<size_t>(coverage.countX * coverage.countY);
    coverage.tiles.resize(cellCount);
    coverage.blanks.resize(cellCount, false);
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

// Returns the tiles of a level that are neither cached nor blank under any of the given rects, each
// listed once, in the zoomed display list grid.
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

// Frames with a still viewport that rasterize nothing and show no missing content are skipped to
// keep the log readable.

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
  if (_root->hasBackgroundStyle()) {
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
  if (_root->hasBackgroundStyle()) {
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

std::vector<Rect> DisplayList::renderTiled(Surface* surface, bool autoClear,
                                           const std::vector<Rect>& dirtyRegions) {
  if (!surfaceCaches.empty() && (surfaceCaches.front()->getContext() != surface->getContext() ||
                                 !ColorSpace::Equals(surfaceCaches.front()->colorSpace().get(),
                                                     surface->colorSpace().get()))) {
    resetCaches();
  }
  checkTileCount(surface);
  const auto zoomChanged = lastZoomScaleInt != _zoomScaleInt;
  // No draw task outlives a frame, so nothing references the grid tiles of the previous zoom scale.
  if (zoomChanged) {
    recycleGridTiles();
  }
  auto tileTasks = invalidateTileCaches(dirtyRegions, GetViewportRect(surface, _contentOffset));
  std::vector<Rect> skippedRects = {};
  auto screenTasks = collectScreenTasks(surface, &tileTasks, &skippedRects);
  // Skipped rects must keep the tiled path; only fall back when both are empty.
  if (screenTasks.empty() && skippedRects.empty()) {
    recycleCurrentTileTasks(tileTasks);
    return renderDirect(surface, autoClear);
  }
  auto zoomScale = ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision);
  std::vector<std::unique_ptr<BackgroundSnapshotMap>> captureMaps = {};
  std::vector<int> taskCapture = {};
  captureTileBackgrounds(surface, tileTasks, zoomScale, &captureMaps, &taskCapture);
  auto dirtyRects = drawTileTasks(tileTasks, captureMaps, taskCapture, surface, zoomScale);
  drawScreenTasks(std::move(screenTasks), std::move(skippedRects), surface, autoClear);
  return dirtyRects;
}

void DisplayList::captureTileBackgrounds(
    Surface* surface, const std::vector<DrawTask>& tileTasks, float zoomScale,
    std::vector<std::unique_ptr<BackgroundSnapshotMap>>* captureMaps,
    std::vector<int>* taskCapture) const {
  // Each capture covers one cluster of tiles at a single raster scale, so a tile is captured with
  // the density it is drawn with. That is what lets tree tiles take part: their scale is 2^-level,
  // not the current zoom.
  captureMaps->clear();
  taskCapture->assign(tileTasks.size(), -1);
  if (_root->hasBackgroundStyle()) {
    // Group by level first: one capture transform cannot serve two raster scales.
    std::vector<std::pair<int, std::vector<size_t>>> levels = {};
    for (size_t i = 0; i < tileTasks.size(); ++i) {
      auto level = tileTasks[i].treeLevel();
      auto it = std::find_if(levels.begin(), levels.end(),
                             [level](const std::pair<int, std::vector<size_t>>& entry) {
                               return entry.first == level;
                             });
      if (it == levels.end()) {
        levels.push_back({level, {i}});
      } else {
        it->second.push_back(i);
      }
    }
    for (const auto& levelEntry : levels) {
      auto level = levelEntry.first;
      auto rasterScale = level == kExactLevel ? zoomScale : GetTileRasterScale(level);
      if (FloatNearlyZero(rasterScale)) {
        continue;
      }
      std::vector<Rect> levelRects = {};
      levelRects.reserve(levelEntry.second.size());
      for (auto index : levelEntry.second) {
        levelRects.push_back(tileTasks[index].tileRect());
      }
      auto clusters = ClusterTileRects(levelRects);
      auto viewMatrix = Matrix::MakeScale(rasterScale);
      for (const auto& cluster : clusters) {
        std::vector<Rect> clusterRects = {};
        clusterRects.reserve(cluster.indices.size());
        for (auto index : cluster.indices) {
          clusterRects.push_back(levelRects[index]);
        }
        // A background style only samples the area under its own bounds plus the blur
        // reach, so cull the capture against those footprints instead of the whole tile.
        // Layers that overlap the tile but sit under no background style are then not
        // drawn. The captured area stays the cluster union, so the surfaces and the way
        // sibling styles share their output do not change.
        std::vector<Rect> cullRects = clusterRects;
        Matrix inverse = Matrix::I();
        if (viewMatrix.invert(&inverse)) {
          auto worldRect = Rect::MakeEmpty();
          for (const auto& rect : clusterRects) {
            worldRect.join(inverse.mapRect(rect));
          }
          auto footprints = collectBackgroundFootprints(worldRect, viewMatrix);
          if (!footprints.empty()) {
            // Keep the raw rects while the per-layer cull loop can afford them: separate rects
            // keep the capture multi-pass, which is what enables style output sharing.
            std::vector<Rect> rects = {};
            for (const auto& footprint : footprints) {
              rects.push_back(viewMatrix.mapRect(footprint));
            }
            if (rects.size() > MAX_CULL_RECTS) {
              auto merged = ClusterTileRects(footprints, FOOTPRINT_CLUSTER_GROWTH);
              rects.clear();
              for (const auto& entry : merged) {
                // Back to the capture surface space that captureBackgrounds expects.
                rects.push_back(viewMatrix.mapRect(entry.bounds));
              }
            }
            if (rects.size() <= MAX_CULL_RECTS) {
              cullRects = std::move(rects);
            }
          }
        }
        auto map = captureBackgrounds(surface, clusterRects, viewMatrix, cullRects);
        auto captureIndex = static_cast<int>(captureMaps->size());
        captureMaps->push_back(std::move(map));
        for (auto offset : cluster.indices) {
          (*taskCapture)[levelEntry.second[offset]] = captureIndex;
        }
      }
    }
  }
}

std::vector<Rect> DisplayList::drawTileTasks(
    std::vector<DrawTask>& tileTasks,
    const std::vector<std::unique_ptr<BackgroundSnapshotMap>>& captureMaps,
    const std::vector<int>& taskCapture, Surface* surface, float zoomScale) {
  std::vector<Rect> dirtyRects = {};
  auto surfaceRect = Rect::MakeWH(surface->width(), surface->height());
  for (size_t taskIndex = 0; taskIndex < tileTasks.size(); ++taskIndex) {
    auto& task = tileTasks[taskIndex];
    auto isTreeTask = task.treeLevel() != kExactLevel;
    auto captureIndex = taskCapture[taskIndex];
    auto* snapshots =
        captureIndex >= 0 ? captureMaps[static_cast<size_t>(captureIndex)].get() : nullptr;
    auto hasContent = drawTileTask(task, snapshots);
    if (!hasContent && task.coversTiles()) {
      recycleBlankTiles(task);
    }
    auto dirtyRect = task.tileRect();
    if (isTreeTask) {
      auto displayScale = zoomScale / GetTileRasterScale(task.treeLevel());
      dirtyRect.scale(displayScale, displayScale);
    }
    dirtyRect.offset(_contentOffset.x, _contentOffset.y);
    if (dirtyRect.intersect(surfaceRect)) {
      dirtyRects.emplace_back(dirtyRect);
    }
  }
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
  auto treeQuota =
      static_cast<size_t>(std::lround(static_cast<double>(totalTileCount) * treeRatio));
  tilePool->setQuota(TileType::Tree, treeQuota);
  tilePool->setQuota(TileType::Grid, static_cast<size_t>(totalTileCount) - treeQuota);
  blankTiles->setMaxCount(BLANK_MARKS_PER_TILE * static_cast<size_t>(totalTileCount));
}

std::vector<DrawTask> DisplayList::invalidateTileCaches(const std::vector<Rect>& dirtyRegions,
                                                        const Rect& viewportRect) {
  if (dirtyRegions.empty()) {
    return {};
  }
  std::vector<DrawTask> tileTasks = {};
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
    const auto coarsestLevel = baseLevel + TREE_LEVEL_COUNT - 1;
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
    emptyTiles.push_back(tilePool->erase(key));
  }
}

void DisplayList::recycleCurrentTileTasks(const std::vector<DrawTask>& tileTasks) {
  for (auto& task : tileTasks) {
    for (auto& tile : task.getTiles()) {
      auto key = task.treeLevel() == kExactLevel
                     ? TileKey::MakeGrid(tile->tileX, tile->tileY, _zoomScaleInt)
                     : TileKey::MakeTree(tile->tileX, tile->tileY, task.treeLevel());
      if (auto recycled = tilePool->erase(key)) {
        emptyTiles.push_back(std::move(recycled));
      }
    }
  }
}

void DisplayList::recycleGridTiles() {
  auto tiles = tilePool->clear(TileType::Grid);
  emptyTiles.insert(emptyTiles.end(), tiles.begin(), tiles.end());
  blankTiles->clearGrid();
}

void DisplayList::recycleBlankTiles(const DrawTask& task) {
  for (auto& tile : task.getTiles()) {
    auto key = task.treeLevel() == kExactLevel
                   ? TileKey::MakeGrid(tile->tileX, tile->tileY, _zoomScaleInt)
                   : TileKey::MakeTree(tile->tileX, tile->tileY, task.treeLevel());
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
  const auto useFallback = _tileUpdateMode != TileUpdateMode::Immediate;
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
  auto renderRect = GetViewportRect(surface, _contentOffset);

  auto renderBounds = _root->renderBounds;
  auto zoomScale = ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision);
  renderBounds.scale(zoomScale, zoomScale);
  renderBounds.roundOut();
  if (!renderRect.intersect(renderBounds)) {
    return {};
  }
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
  if (useFallback) {
    coverages =
        CollectTreeCoverages(tilePool.get(), blankTiles.get(), zoomScale, renderRect, _tileSize);
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
    if (!missingGrids.empty() && dirtyGrids.size() == missingGrids.size()) {
      hasZoomBlurTiles = true;
    }
    // Takes free tiles for the given cells of a tree level and queues them for rasterization.
    // Returns how many cells got a tile.
    auto rasterizeTreeTiles = [&](TreeLevelCoverage* coverage,
                                  const std::vector<std::pair<int, int>>& cells) -> size_t {
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
    };
    if (!fallbackGrids.empty()) {
      hasZoomBlurTiles = true;
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
        rasterizeTreeTiles(&coarsest, CollectMissingTreeTiles(coarsest, uncoveredRects, _tileSize));
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
          auto refined = rasterizeTreeTiles(&coverages[i], cellsByLevel[i]);
          refineBudget -= static_cast<int>(refined);
        }
      }
      // The passes above may have added tiles, so the coverage is queried again: the finest
      // complete level is displayed, and cells nothing covers fall back to the background.
      for (auto& grid : fallbackGrids) {
        auto gridRect = getGridRect(grid);
        auto coverage = FindFinestCoverage(&coverages, gridRect, _tileSize);
        if (coverage != nullptr) {
          AppendFallbackScreenTasks(coverage, gridRect, _tileSize, &screenTasks, skippedRects);
        } else {
          skippedRects->push_back(gridRect);
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
      // Top up the levels above L from L+2 towards L, only once the grid is complete.
      auto chainBudget = gridComplete ? std::max(_maxTilesRefinedPerFrame, 0) : 0;
      const auto lowestLevel = gridComplete ? size_t{1} : size_t{2};
      if (mayRefine) {
        // coverages[0] is L and coverages[3] is L+3.
        for (auto level = coverages.size() - 1; level >= lowestLevel && chainBudget > 0; --level) {
          auto& coverage = coverages[level - 1];
          auto cells = CollectMissingTreeTiles(coverage, {renderRect}, _tileSize);
          if (cells.empty()) {
            continue;
          }
          if (cells.size() > static_cast<size_t>(chainBudget)) {
            cells.resize(static_cast<size_t>(chainBudget));
          }
          auto refined = rasterizeTreeTiles(&coverage, cells);
          chainBudget -= static_cast<int>(refined);
        }
      }
      // Report the state left after this frame: a skipped frame still has to keep the loop alive,
      // and a frame that filled everything has to stop it.
      bool chainPending = false;
      for (auto level = coverages.size() - 1; level >= lowestLevel; --level) {
        if (!CollectMissingTreeTiles(coverages[level - 1], {renderRect}, _tileSize).empty()) {
          chainPending = true;
          break;
        }
      }
      // Without a budget the chain can never progress, so it must not keep the loop running.
      if (chainPending && _maxTilesRefinedPerFrame > 0) {
        hasZoomBlurTiles = true;
      }
    }
    freeTiles = getFreeTiles(surface, TileType::Grid, dirtyGrids.size());
    if (freeTiles.size() < dirtyGrids.size()) {
      if (!useFallback) {
        emptyTiles.insert(emptyTiles.end(), freeTiles.begin(), freeTiles.end());
        LOGE("DisplayList::collectScreenTasks() Not enough free tiles available.");
        return {};
      }
      // The next frame selects the remaining grids again, so they only need something to show now.
      hasZoomBlurTiles = true;
      for (auto i = freeTiles.size(); i < dirtyGrids.size(); ++i) {
        auto gridRect = getGridRect(dirtyGrids[i]);
        auto coverage = FindFinestCoverage(&coverages, gridRect, _tileSize);
        if (coverage != nullptr) {
          AppendFallbackScreenTasks(coverage, gridRect, _tileSize, &screenTasks, skippedRects);
        } else {
          skippedRects->push_back(gridRect);
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

bool DisplayList::drawTileTask(const DrawTask& task, BackgroundSnapshotMap* snapshots) const {
  auto surface = surfaceCaches[task.sourceIndex()].get();
  DEBUG_ASSERT(surface != nullptr);
  auto canvas = surface->getCanvas();
  AutoCanvasRestore autoRestore(canvas);
  auto rasterScale = task.treeLevel() == kExactLevel
                         ? ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision)
                         : GetTileRasterScale(task.treeLevel());
  DEBUG_ASSERT(rasterScale != 0.0f);
  auto viewMatrix = Matrix::MakeScale(rasterScale);
  auto& tileRect = task.tileRect();
  auto& sourceRect = task.sourceRect();
  auto offsetX = sourceRect.left - tileRect.left;
  auto offsetY = sourceRect.top - tileRect.top;
  viewMatrix.postTranslate(offsetX, offsetY);
  auto clipRect = tileRect;
  clipRect.offset(offsetX, offsetY);
  return drawRootLayer(surface, clipRect, viewMatrix, true, snapshots);
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
  blankTiles->clear();
  surfaceCaches = {};
  totalTileCount = 0;
  emptyTiles.clear();
}

bool DisplayList::drawRootLayer(Surface* surface, const Rect& drawRect, const Matrix& viewMatrix,
                                bool autoClear, BackgroundSnapshotMap* snapshots) const {
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
  Matrix inverse = Matrix::I();
  viewMatrix.invert(&inverse);
  auto renderRect = inverse.mapRect(drawRect);
  renderRect.roundOut();
  std::vector<Rect> renderRectsVec = {renderRect};
  args.renderRects = &renderRectsVec;
  args.dstColorSpace = surface->colorSpace();
  args.subtreeCacheMaxSize = _subtreeCacheMaxSize;
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
  args.backgroundHandler = snapshots ? &consumer : BackgroundHandler::NoOp();
  // The clear and the background color above must not count as content.
  auto drawingManager = context->drawingManager();
  auto drawMark = drawingManager->drawMark();
  _root->drawLayer(args, surface->getCanvas(), 1.0f, BlendMode::SrcOver);
  return drawingManager->hasDrawnSince(drawMark);
}

std::vector<Rect> DisplayList::collectBackgroundFootprints(const Rect& worldBounds,
                                                           const Matrix& viewMatrix) const {
  std::vector<Rect> footprints = {};
  auto deviceScale = viewMatrix.getMaxScale();
  bool safe = true;
  auto collect = [](const Layer* layer, const Rect& bounds, float deviceScale, bool& safe,
                    std::vector<Rect>& out, const auto& self) -> void {
    if (layer->maxBackgroundOutset <= 0 || !layer->bitFields.visible || layer->_alpha <= 0 ||
        !Rect::Intersects(bounds, layer->renderBounds)) {
      return;
    }
    // A layer that keeps its own 3D context is composited through the BSP, and a non-affine 3D
    // matrix leaves the 2D renderBounds without a usable extent for the sampled area. Either way
    // bail out and let the caller keep the whole tile.
    const bool nonAffine3D = !layer->matrix3D().isIdentity() && layer->matrix().isIdentity();
    if (layer->canPreserve3D() || nonAffine3D) {
      safe = false;
      return;
    }
    auto layerScale = deviceScale * layer->getMatrixWithScrollRect().asMatrix().getMaxScale();
    for (const auto& style : layer->_layerStyles) {
      if (style == nullptr ||
          (style->extraSourceType() &
           static_cast<uint32_t>(LayerStyleExtraSourceType::Background)) == 0 ||
          BackgroundHandler::IsNegligibleBackgroundStyle(style.get(), layerScale)) {
        continue;
      }
      // The style samples the backdrop under its own bounds plus the blur reach.
      auto footprint = layer->renderBounds;
      footprint.outset(layer->maxBackgroundOutset, layer->maxBackgroundOutset);
      out.push_back(footprint);
      break;
    }
    for (const auto& child : layer->_children) {
      self(child.get(), bounds, deviceScale, safe, out, self);
    }
  };
  collect(_root.get(), worldBounds, deviceScale, safe, footprints, collect);
  if (!safe) {
    return {};
  }
  return footprints;
}

std::unique_ptr<BackgroundSnapshotMap> DisplayList::captureBackgrounds(
    Surface* surface, const std::vector<Rect>& renderRects, const Matrix& viewMatrix,
    const std::vector<Rect>& cullRects) const {
  DEBUG_ASSERT(surface != nullptr);
  if (!_root->hasBackgroundStyle()) {
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
  auto hasVisibleBackgroundStyle = [](const Layer* layer, const Rect& unionRect,
                                      const std::vector<Rect>& rects, float scale,
                                      const auto& self) -> bool {
    if (layer->maxBackgroundOutset <= 0 || !layer->bitFields.visible || layer->_alpha <= 0 ||
        !Rect::Intersects(unionRect, layer->renderBounds)) {
      return false;
    }
    auto layerScale = scale * layer->getMatrixWithScrollRect().asMatrix().getMaxScale();
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
      if (self(child.get(), unionRect, rects, layerScale, self)) {
        return true;
      }
    }
    return false;
  };
  if (!hasVisibleBackgroundStyle(_root.get(), unionRect, worldRects, deviceScale,
                                 hasVisibleBackgroundStyle)) {
    return nullptr;
  }
  // Expand by the max blur outset so layers whose bounds sit just outside the dirty rects but
  // still contribute pixels to the blur sampling region are not culled by the capture pass.
  for (auto& rect : worldRects) {
    rect.outset(_root->maxBackgroundOutset, _root->maxBackgroundOutset);
  }
  // The layers to draw can be narrowed to the area the background styles actually sample, while
  // the surface still covers every render rect: a style never reads outside its footprint, and
  // keeping the captured area unchanged keeps the multi-pass structure (and the style output
  // sharing that depends on it) intact.
  std::vector<Rect> cullWorldRects = worldRects;
  if (!cullRects.empty()) {
    cullWorldRects.clear();
    cullWorldRects.reserve(cullRects.size());
    for (const auto& cullRect : cullRects) {
      auto worldRect = inverse.mapRect(cullRect);
      worldRect.roundOut();
      worldRect.outset(_root->maxBackgroundOutset, _root->maxBackgroundOutset);
      cullWorldRects.push_back(worldRect);
    }
  }
  DrawArgs args(context);
  args.dstColorSpace = surface->colorSpace();
  args.subtreeCacheMaxSize = _subtreeCacheMaxSize;
  auto bgSource =
      _root->createBackgroundSource(context, surfaceUnion, viewMatrix, false, args.dstColorSpace);
  if (bgSource == nullptr) {
    return nullptr;
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
  BackgroundCapturer::Run(_root.get(), args, std::move(bgSource), snapshotMap.get(),
                          cullWorldRects);
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

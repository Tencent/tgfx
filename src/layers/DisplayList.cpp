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
#include "core/utils/DecomposeRects.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/TileSortCompareFunc.h"
#include "inspect/InspectorMark.h"
#include "layers/DrawArgs.h"
#include "layers/RootLayer.h"
#include "layers/TileCache.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {
static constexpr size_t MAX_DIRTY_REGION_FRAMES = 5;
static constexpr float DIRTY_REGION_ANTIALIAS_MARGIN = 0.5f;
static constexpr int MIN_TILE_SIZE = 16;
static constexpr int MAX_TILE_SIZE = 2048;
static constexpr int FALLBACK_GRID_SIZE = 64;
static constexpr int MAX_ATLAS_SIZE = 8192;

class DrawTask {
 public:
  DrawTask(std::shared_ptr<Tile> tile, int tileSize, const Rect& drawRect = {}, float scale = 1.0f)
      : tiles({std::move(tile)}) {
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

 private:
  // Hold strong references to tiles to ensure they aren't reused by TileCache.
  std::vector<std::shared_ptr<Tile>> tiles = {};
  Rect _sourceRect = {};
  Rect _tileRect = {};

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
    _tileRect.round();
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

DisplayList::DisplayList() : _root(RootLayer::Make()) {
  _root->_root = _root.get();
  SET_DISPLAY_LIST(this);
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
  if (!tileCaches.empty()) {
    std::unordered_map<int64_t, TileCache*> newCaches = {};
    for (auto& item : tileCaches) {
      auto zoomScaleInt = ChangeZoomScalePrecision(item.first, oldPrecision, _zoomScalePrecision);
      newCaches[zoomScaleInt] = item.second;
    }
    tileCaches = std::move(newCaches);
  }
}

void DisplayList::setContentOffset(float offsetX, float offsetY) {
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

void DisplayList::setMaxTileCount(int count) {
  if (count < 0) {
    count = 0;
  }
  if (_maxTileCount == count) {
    return;
  }
  _maxTileCount = count;
  resetCaches();
}

Color DisplayList::backgroundColor() const {
  return _root->backgroundColor();
}

void DisplayList::setBackgroundColor(const Color& color) {
  if (_root->setBackgroundColor(color)) {
    resetCaches();
  }
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
  RENDER_VISABLE_OBJECT(surface->getContext());
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
}

std::vector<Rect> DisplayList::renderDirect(Surface* surface, bool autoClear) const {
  auto surfaceRect = Rect::MakeWH(surface->width(), surface->height());
  drawRootLayer(surface, surfaceRect, getViewMatrix(), autoClear);
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
  std::vector<Rect> drawRects = {};
  if (cacheChanged || lastZoomScaleInt != _zoomScaleInt || lastContentOffset != _contentOffset) {
    drawRects = {surfaceRect};
    lastZoomScaleInt = _zoomScaleInt;
    lastContentOffset = _contentOffset;
  } else {
    drawRects = MapDirtyRegions(dirtyRegions, viewMatrix, true, &surfaceRect);
  }
  auto canvas = surface->getCanvas();
  for (auto& drawRect : drawRects) {
    drawRootLayer(partialCache.get(), drawRect, viewMatrix, true);
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
  return drawRects;
}

std::vector<Rect> DisplayList::renderTiled(Surface* surface, bool autoClear,
                                           const std::vector<Rect>& dirtyRegions) {
  if (!surfaceCaches.empty() && surfaceCaches.front()->getContext() != surface->getContext()) {
    resetCaches();
  }
  checkTileCount(surface);
  auto tileTasks = invalidateTileCaches(dirtyRegions);
  auto screenTasks = collectScreenTasks(surface, &tileTasks);
  if (screenTasks.empty()) {
    return renderDirect(surface, autoClear);
  }
  std::vector<Rect> dirtyRects = {};
  auto surfaceRect = Rect::MakeWH(surface->width(), surface->height());
  for (auto& task : tileTasks) {
    drawTileTask(task);
    auto dirtyRect = task.tileRect();
    dirtyRect.offset(roundf(_contentOffset.x), roundf(_contentOffset.y));
    if (dirtyRect.intersect(surfaceRect)) {
      dirtyRects.emplace_back(dirtyRect);
    }
  }
  drawScreenTasks(std::move(screenTasks), surface, autoClear);
  return dirtyRects;
}

void DisplayList::checkTileCount(Surface* renderSurface) {
  DEBUG_ASSERT(renderSurface != nullptr);
  auto tileCountX =
      ceilf(static_cast<float>(renderSurface->width()) / static_cast<float>(_tileSize));
  auto tileCountY =
      ceilf(static_cast<float>(renderSurface->height()) / static_cast<float>(_tileSize));
  auto minTileCount = static_cast<int>(tileCountX + 1) * static_cast<int>(tileCountY + 1);
  if (totalTileCount > 0) {
    if (totalTileCount >= minTileCount) {
      return;
    }
    resetCaches();
  }
  totalTileCount = std::max(minTileCount, _maxTileCount);
  auto maxTileCountPerAtlas = getMaxTileCountPerAtlas(renderSurface->getContext());
  if (maxTileCountPerAtlas <= 0) {
    return;
  }
  auto remainingTileCount = totalTileCount % maxTileCountPerAtlas;
  totalTileCount -= remainingTileCount;
  int width = static_cast<int>(sqrtf(static_cast<float>(remainingTileCount)));
  int height =
      static_cast<int>(ceilf(static_cast<float>(remainingTileCount) / static_cast<float>(width)));
  totalTileCount += width * height;
}

std::vector<DrawTask> DisplayList::invalidateTileCaches(const std::vector<Rect>& dirtyRegions) {
  if (dirtyRegions.empty()) {
    return {};
  }
  std::vector<DrawTask> tileTasks = {};
  std::vector<int64_t> emptyScales = {};
  for (auto& [scaleInt, tileCache] : tileCaches) {
    if (scaleInt == _zoomScaleInt) {
      invalidateCurrentTileCache(tileCache, dirtyRegions, &tileTasks);
      continue;
    }
    auto zoomMatrix = Matrix::MakeScale(ToZoomScaleFloat(scaleInt, _zoomScalePrecision));
    auto dirtyRects = MapDirtyRegions(dirtyRegions, zoomMatrix);
    for (auto& dirtyRect : dirtyRects) {
      auto tiles = tileCache->getTilesUnderRect(dirtyRect);
      for (auto& tile : tiles) {
        tileCache->removeTile(tile->tileX, tile->tileY);
      }
      emptyTiles.insert(emptyTiles.end(), tiles.begin(), tiles.end());
    }
    if (tileCache->empty()) {
      emptyScales.push_back(scaleInt);
    }
  }
  for (auto& scale : emptyScales) {
    auto result = tileCaches.find(scale);
    if (result != tileCaches.end()) {
      delete result->second;
      tileCaches.erase(result);
    }
  }
  return tileTasks;
}

void DisplayList::invalidateCurrentTileCache(const TileCache* tileCache,
                                             const std::vector<Rect>& dirtyRegions,
                                             std::vector<DrawTask>* tileTasks) const {
  auto zoomMatrix = Matrix::MakeScale(ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision));
  auto dirtyRects = MapDirtyRegions(dirtyRegions, zoomMatrix, true);
  for (auto& dirtyRect : dirtyRects) {
    bool continuous = false;
    auto tiles = tileCache->getTilesUnderRect(dirtyRect, false, &continuous);
    if (continuous) {
      tileTasks->emplace_back(std::move(tiles), _tileSize, dirtyRect);
    } else {
      for (auto& tile : tiles) {
        auto drawRect = tile->getTileRect(_tileSize);
        if (drawRect.intersect(dirtyRect)) {
          tileTasks->emplace_back(tile, _tileSize, drawRect);
        }
      }
    }
  }
}

std::vector<DrawTask> DisplayList::collectScreenTasks(const Surface* surface,
                                                      std::vector<DrawTask>* tileTasks) {
  auto maxRefinedCount = _maxTilesRefinedPerFrame;
  if (lastContentOffset != _contentOffset || lastZoomScaleInt != _zoomScaleInt) {
    updateMousePosition();
    lastContentOffset = _contentOffset;
    lastZoomScaleInt = _zoomScaleInt;
    // To ensure smooth user interactions, we skip refinement when the offset or zoom scale is
    // changing.
    maxRefinedCount = 0;
  }
  hasZoomBlurTiles = false;
  TileCache* currentTileCache = nullptr;
  auto result = tileCaches.find(_zoomScaleInt);
  if (result != tileCaches.end()) {
    currentTileCache = result->second;
  } else {
    currentTileCache = new TileCache(_tileSize);
    tileCaches[_zoomScaleInt] = currentTileCache;
  }
  auto renderRect = Rect::MakeWH(surface->width(), surface->height());
  renderRect.offset(-_contentOffset.x, -_contentOffset.y);

  auto renderBounds = _root->renderBounds;
  auto zoomScale = ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision);
  renderBounds.scale(zoomScale, zoomScale);
  renderBounds.roundOut();
  if (!renderRect.intersect(renderBounds)) {
    return {};
  }
  int startX = static_cast<int>(floorf(renderRect.left / static_cast<float>(_tileSize)));
  int startY = static_cast<int>(floorf(renderRect.top / static_cast<float>(_tileSize)));
  int endX = static_cast<int>(ceilf(renderRect.right / static_cast<float>(_tileSize)));
  int endY = static_cast<int>(ceilf(renderRect.bottom / static_cast<float>(_tileSize)));

  if (startX >= endX || startY >= endY) {
    return {};
  }

  std::vector<DrawTask> screenTasks = {};
  std::vector<std::pair<int, int>> dirtyGrids = {};
  auto tileCount = static_cast<size_t>(endX - startX) * static_cast<size_t>(endY - startY);
  screenTasks.reserve(tileCount);
  dirtyGrids.reserve(tileCount);
  auto sortedCaches = getSortedTileCaches();
  auto fallbackTileCaches = getFallbackTileCaches(sortedCaches);
  auto sortedTiles = GetSortedTiles(startX, endX, startY, endY, _tileSize, mousePosition);
  for (const auto& [tileX, tileY] : sortedTiles) {
    auto tile = currentTileCache->getTile(tileX, tileY);
    if (tile != nullptr) {
      auto tileRect = tile->getTileRect(_tileSize, &renderRect);
      screenTasks.emplace_back(tile, _tileSize, tileRect);
    } else {
      if (_allowZoomBlur) {
        auto fallbackTasks = getFallbackDrawTasks(tileX, tileY, fallbackTileCaches);
        if (!fallbackTasks.empty()) {
          if (maxRefinedCount <= 0) {
            screenTasks.insert(screenTasks.end(), fallbackTasks.begin(), fallbackTasks.end());
            hasZoomBlurTiles = true;
            continue;
          }
          maxRefinedCount--;
        }
      }
      dirtyGrids.emplace_back(tileX, tileY);
    }
  }
  std::vector<std::shared_ptr<Tile>> freeTiles = {};
  bool continuous = false;
  if (screenTasks.empty()) {
    freeTiles = createContinuousTiles(surface, endX - startX, endY - startY);
    continuous = !freeTiles.empty();
    dirtyGrids = GenerateGridTiles(startX, endX, startY, endY);
  }
  if (freeTiles.empty()) {
    freeTiles = getFreeTiles(surface, dirtyGrids.size(), sortedCaches);
  }
  if (dirtyGrids.size() != freeTiles.size()) {
    DEBUG_ASSERT(freeTiles.size() < dirtyGrids.size());
    LOGE("DisplayList::collectRenderTiles() Not enough free tiles available.");
    return {};
  }
  size_t tileIndex = 0;
  std::vector<std::shared_ptr<Tile>> taskTiles = {};
  for (auto& grid : dirtyGrids) {
    auto& tile = freeTiles[tileIndex++];
    tile->tileX = grid.first;
    tile->tileY = grid.second;
    taskTiles.push_back(tile);
    auto tileRect = tile->getTileRect(_tileSize, &renderRect);
    screenTasks.emplace_back(tile, _tileSize, tileRect);
    currentTileCache->addTile(tile);
  }
  if (continuous && !hasZoomBlurTiles) {
    // If we have continuous tiles, we can draw a single rectangle for the entire area.
    auto drawRect = Rect::MakeXYWH(startX * _tileSize, startY * _tileSize,
                                   (endX - startX) * _tileSize, (endY - startY) * _tileSize);
    tileTasks->emplace_back(std::move(freeTiles), _tileSize, drawRect);
  } else {
    for (auto& tile : taskTiles) {
      tileTasks->emplace_back(tile, _tileSize);
    }
  }
  return screenTasks;
}

static float ScaleRatio(float scaleA, float zoomScale) {
  auto ratio = fabsf(scaleA / zoomScale);
  if (ratio < 1.0f) {
    ratio = 1.0f / ratio;
  }
  return ratio;
}

std::vector<std::pair<float, TileCache*>> DisplayList::getSortedTileCaches() const {
  std::vector<std::pair<float, TileCache*>> sortedTileCaches;
  sortedTileCaches.reserve(tileCaches.size());
  for (auto& [scaleInt, tileCache] : tileCaches) {
    auto zoomScale = ToZoomScaleFloat(scaleInt, _zoomScalePrecision);
    sortedTileCaches.emplace_back(zoomScale, tileCache);
  }
  std::sort(sortedTileCaches.begin(), sortedTileCaches.end(),
            [](const std::pair<float, TileCache*>& a, const std::pair<float, TileCache*>& b) {
              return a.first < b.first;
            });
  return sortedTileCaches;
}

std::vector<std::pair<float, TileCache*>> DisplayList::getFallbackTileCaches(
    const std::vector<std::pair<float, TileCache*>>& sortedCaches) const {
  if (!_allowZoomBlur) {
    return {};
  }
  auto currentZoomScale = ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision);
  DEBUG_ASSERT(currentZoomScale != 0.0f);
  auto cacheCount = sortedCaches.size();
  std::vector<std::pair<float, TileCache*>> fallbackCaches = {};
  fallbackCaches.reserve(cacheCount);
  size_t firstGreaterIndex = 0;
  for (auto& item : sortedCaches) {
    if (item.first < currentZoomScale) {
      firstGreaterIndex++;
    } else {
      fallbackCaches.push_back(item);
    }
  }
  for (size_t index = firstGreaterIndex; index > 0; index--) {
    auto& item = sortedCaches[index - 1];
    fallbackCaches.push_back(item);
  }
  return fallbackCaches;
}

std::vector<DrawTask> DisplayList::getFallbackDrawTasks(
    int tileX, int tileY, const std::vector<std::pair<float, TileCache*>>& fallbackCaches) const {
  auto currentZoomScale = ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision);
  DEBUG_ASSERT(currentZoomScale != 0.0f);
  auto gridCount = std::max(_tileSize / FALLBACK_GRID_SIZE, 1);
  auto gridSize = _tileSize / gridCount;
  auto tileStartX = tileX * _tileSize;
  auto tileStartY = tileY * _tileSize;
  std::vector<DrawTask> tasks = {};
  auto renderBounds = _root->renderBounds;
  renderBounds.scale(currentZoomScale, currentZoomScale);
  renderBounds.roundOut();
  for (int gridX = 0; gridX < gridCount; gridX++) {
    for (int gridY = 0; gridY < gridCount; gridY++) {
      auto gridStartX = tileStartX + gridX * gridSize;
      auto gridStartY = tileStartY + gridY * gridSize;
      bool found = false;
      auto gridRect = Rect::MakeXYWH(gridStartX, gridStartY, gridSize, gridSize);
      if (!gridRect.intersect(renderBounds)) {
        continue;
      }
      for (auto& [scale, tileCache] : fallbackCaches) {
        if (scale == currentZoomScale || tileCache->empty()) {
          continue;
        }
        auto scaleRatio = scale / currentZoomScale;
        DEBUG_ASSERT(scaleRatio != 0.0f);
        auto zoomedRect = gridRect;
        zoomedRect.scale(scaleRatio, scaleRatio);
        auto tiles = tileCache->getTilesUnderRect(zoomedRect, true);
        if (tiles.empty()) {
          continue;
        }
        for (auto& tile : tiles) {
          auto drawRect = tile->getTileRect(_tileSize);
          if (drawRect.intersect(zoomedRect)) {
            tasks.emplace_back(tile, _tileSize, drawRect, 1.0f / scaleRatio);
          }
        }
        found = true;
        break;
      }
      if (!found) {
        return {};
      }
    }
  }
  return tasks;
}

std::vector<std::shared_ptr<Tile>> DisplayList::getFreeTiles(
    const Surface* renderSurface, size_t tileCount,
    const std::vector<std::pair<float, TileCache*>>& sortedCaches) {
  std::vector<std::shared_ptr<Tile>> tiles = {};
  tiles.reserve(tileCount);
  while (emptyTiles.size() < tileCount) {
    if (!createEmptyTiles(renderSurface)) {
      break;
    }
  }
  if (emptyTiles.size() >= tileCount) {
    tiles.insert(tiles.end(), emptyTiles.end() - static_cast<int>(tileCount), emptyTiles.end());
    emptyTiles.resize(emptyTiles.size() - tileCount);
    return tiles;
  }
  tiles.insert(tiles.end(), emptyTiles.begin(), emptyTiles.end());
  emptyTiles.clear();
  auto currentZoomScale = ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision);
  DEBUG_ASSERT(currentZoomScale != 0.0f);
  // Reverse iterate through sorted caches to get the farest tiles first.
  for (size_t i = 0, j = sortedCaches.size(); i < j;) {
    auto& [scaleS, tileCacheS] = sortedCaches.at(i);
    auto& [scaleL, tileCacheL] = sortedCaches.at(j - 1);
    auto scale = scaleS;
    auto tileCache = tileCacheS;
    if (ScaleRatio(scaleS, currentZoomScale) < ScaleRatio(scaleL, currentZoomScale)) {
      scale = scaleL;
      tileCache = tileCacheL;
      j--;
    } else {
      i++;
    }
    auto centerX = static_cast<float>(renderSurface->width()) * 0.5f - _contentOffset.x;
    auto centerY = static_cast<float>(renderSurface->height()) * 0.5f - _contentOffset.y;
    centerX *= scale / currentZoomScale;
    centerY *= scale / currentZoomScale;
    auto reusableTiles = tileCache->getReusableTiles(centerX, centerY);
    for (auto& tile : reusableTiles) {
      tileCache->removeTile(tile->tileX, tile->tileY);
      tiles.push_back(std::move(tile));
      if (tiles.size() >= tileCount) {
        break;
      }
    }
    if (tiles.size() >= tileCount) {
      break;
    }
  }
  if (tiles.size() < tileCount) {
    emptyTiles = std::move(tiles);
    return {};
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
  int countX = static_cast<int>(sqrtf(static_cast<float>(tileCount)));
  int countY = static_cast<int>(ceilf(static_cast<float>(tileCount) / static_cast<float>(countX)));
  if (countX < requestCountX) {
    countX = requestCountX;
    countY = static_cast<int>(ceilf(static_cast<float>(tileCount) / static_cast<float>(countX)));
  } else if (countY < requestCountY) {
    countY = requestCountY;
    countX = static_cast<int>(ceilf(static_cast<float>(tileCount) / static_cast<float>(countY)));
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
  int countX = static_cast<int>(sqrtf(static_cast<float>(tileCount)));
  int countY = static_cast<int>(ceilf(static_cast<float>(tileCount) / static_cast<float>(countX)));
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

void DisplayList::drawTileTask(const DrawTask& task) const {
  auto surface = surfaceCaches[task.sourceIndex()].get();
  DEBUG_ASSERT(surface != nullptr);
  auto canvas = surface->getCanvas();
  AutoCanvasRestore autoRestore(canvas);
  auto currentZoomScale = ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision);
  DEBUG_ASSERT(currentZoomScale != 0.0f);
  auto viewMatrix = Matrix::MakeScale(currentZoomScale);
  auto& tileRect = task.tileRect();
  auto& sourceRect = task.sourceRect();
  auto offsetX = sourceRect.left - tileRect.left;
  auto offsetY = sourceRect.top - tileRect.top;
  viewMatrix.postTranslate(offsetX, offsetY);
  auto clipRect = tileRect;
  clipRect.offset(offsetX, offsetY);
  drawRootLayer(surface, clipRect, viewMatrix, true);
}

void DisplayList::drawScreenTasks(std::vector<DrawTask> screenTasks, Surface* surface,
                                  bool autoClear) const {
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
  static SamplingOptions sampling(FilterMode::Nearest, MipmapMode::None);
  canvas->setMatrix(Matrix::MakeTrans(_contentOffset.x, _contentOffset.y));
  Rect tileRect = {};
  for (auto& task : screenTasks) {
    auto surfaceCache = surfaceCaches[task.sourceIndex()];
    DEBUG_ASSERT(surfaceCache != nullptr);
    auto image = surfaceCache->makeImageSnapshot();
    canvas->drawImageRect(image, task.sourceRect(), task.tileRect(), sampling, &paint,
                          SrcRectConstraint::Strict);
    tileRect.join(task.tileRect());
  }

  auto screenRect = Rect::MakeWH(surface->width(), surface->height());
  screenRect.offset(-_contentOffset.x, -_contentOffset.y);
  screenRect.roundOut();
  if (tileRect.contains(screenRect)) {
    return;
  }

  paint.setColor(_root->backgroundColor());
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
  for (auto& item : tileCaches) {
    delete item.second;
  }
  tileCaches = {};
  surfaceCaches = {};
  totalTileCount = 0;
  emptyTiles.clear();
}

void DisplayList::drawRootLayer(Surface* surface, const Rect& drawRect, const Matrix& viewMatrix,
                                bool autoClear) const {
  DEBUG_ASSERT(surface != nullptr);
  auto canvas = surface->getCanvas();
  auto context = surface->getContext();
  AutoCanvasRestore autoRestore(canvas);
  auto surfaceRect = Rect::MakeWH(surface->width(), surface->height());
  bool fullScreen = drawRect == surfaceRect;
  if (!fullScreen) {
    canvas->clipRect(drawRect);
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
  args.renderRect = &renderRect;
  args.blurBackground =
      _root->createBackgroundContext(context, drawRect, viewMatrix, false, args.dstColorSpace);
  args.dstColorSpace = surface->colorSpace();
  _root->drawLayer(args, canvas, 1.0f, BlendMode::SrcOver);
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

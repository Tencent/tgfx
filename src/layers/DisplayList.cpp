/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "layers/DrawArgs.h"
#include "layers/RootLayer.h"
#include "layers/TileCache.h"

namespace tgfx {
static constexpr size_t MAX_DIRTY_REGION_FRAMES = 5;
static constexpr float DIRTY_REGION_ANTIALIAS_MARGIN = 0.5f;
static constexpr int MIN_TILE_SIZE = 16;
static constexpr int MAX_TILE_SIZE = 2048;

struct TileRenderTask {
  TileRenderTask(const Tile* tile, int tileSize, const Rect& drawRect)
      : surfaceIndex(tile->sourceIndex), drawRect(drawRect) {
    surfaceX = tile->sourceX * tileSize;
    surfaceY = tile->sourceY * tileSize;
    surfaceX += static_cast<int>(drawRect.left) - tile->tileX * tileSize;
    surfaceY += static_cast<int>(drawRect.top) - tile->tileY * tileSize;
  }

  size_t surfaceIndex = 0;  // The index of the surface in the surfaceCaches.
  int surfaceX = 0;         // The x coordinate on the surface where this tile will be drawn.
  int surfaceY = 0;         // The y coordinate on the surface where this tile will be drawn.
  Rect drawRect = {};       // The rectangle to draw the tile in the zoomed display list grid.
};

struct AtlasArgs {
  std::shared_ptr<Image> atlas = nullptr;
  std::vector<Matrix> matrices = {};
  std::vector<Rect> rects = {};
};

DisplayList::DisplayList() : _root(RootLayer::Make()) {
  _root->_root = _root.get();
}

DisplayList::~DisplayList() {
  resetCaches();
}

Layer* DisplayList::root() const {
  return _root.get();
}

void DisplayList::setZoomScale(float zoomScale) {
  if (zoomScale < 0.0f || FloatNearlyZero(zoomScale)) {
    zoomScale = 0.0f;
  } else if (zoomScale < 1.0f) {
    zoomScale = static_cast<float>(_zoomScalePrecision) /
                std::roundf(static_cast<float>(_zoomScalePrecision) / zoomScale);
  } else {
    zoomScale = std::roundf(zoomScale * static_cast<float>(_zoomScalePrecision)) /
                static_cast<float>(_zoomScalePrecision);
  }
  if (_zoomScale == zoomScale) {
    return;
  }
  _hasContentChanged = true;
  _zoomScale = zoomScale;
}

void DisplayList::setZoomScalePrecision(int precision) {
  _zoomScalePrecision = std::max(precision, 1);
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

void DisplayList::setAllowZoomBlur(bool allow) {
  _allowZoomBlur = allow;
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
  if (_hasContentChanged || _root->bitFields.dirtyDescendents) {
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
  if (_zoomScale == 0.0f) {
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
  auto viewMatrix = getViewMatrix();
  auto inverse = Matrix::I();
  DEBUG_ASSERT(viewMatrix.invertible());
  viewMatrix.invert(&inverse);
  auto canvas = surface->getCanvas();
  canvas->setMatrix(viewMatrix);
  if (autoClear) {
    canvas->clear();
  }
  DrawArgs args(surface->getContext());
  auto surfaceRect = Rect::MakeWH(surface->width(), surface->height());
  auto renderRect = inverse.mapRect(surfaceRect);
  args.renderRect = &renderRect;
  BackgroundArgs backgroundArgs = {surface->getContext(), surfaceRect, viewMatrix};
  args.backgroundArgs = &backgroundArgs;
  _root->drawLayer(args, canvas, 1.0f, BlendMode::SrcOver);
  return {surfaceRect};
}

static std::vector<Rect> MapDirtyRegions(const std::vector<Rect>& dirtyRegions,
                                         const Matrix& viewMatrix, bool decompose = false,
                                         const Rect* clipRect = nullptr) {
  std::vector<Rect> dirtyRects = {};
  dirtyRects.reserve(dirtyRegions.size());
  for (auto& region : dirtyRegions) {
    auto dirtyRect = viewMatrix.mapRect(region);
    // Expand by 0.5 pixels to preserve antialiasing results.
    dirtyRect.outset(DIRTY_REGION_ANTIALIAS_MARGIN, DIRTY_REGION_ANTIALIAS_MARGIN);
    // Snap to pixel boundaries to avoid subpixel clipping artifacts.
    dirtyRect.roundOut();
    if (!clipRect || dirtyRect.intersect(*clipRect)) {
      dirtyRects.push_back(dirtyRect);
    }
  }
  if (decompose) {
    DecomposeRects(dirtyRects);
  }
  return dirtyRects;
}
std::vector<Rect> DisplayList::renderPartial(Surface* surface, bool autoClear,
                                             const std::vector<Rect>& dirtyRegions) {
  auto context = surface->getContext();
  bool cacheChanged = false;
  auto partialCache = surfaceCaches.empty() ? nullptr : surfaceCaches.front();
  if (partialCache == nullptr || partialCache->getContext() != context ||
      partialCache->width() != surface->width() || partialCache->height() != surface->height()) {
    partialCache = Surface::Make(context, surface->width(), surface->height(), ColorType::RGBA_8888,
                                 1, false, surface->renderFlags());
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
  if (cacheChanged || lastZoomScale != _zoomScale || lastContentOffset != _contentOffset) {
    drawRects = {surfaceRect};
    lastZoomScale = _zoomScale;
    lastContentOffset = _contentOffset;
  } else {
    drawRects = MapDirtyRegions(dirtyRegions, viewMatrix, true, &surfaceRect);
  }
  auto inverse = Matrix::I();
  DEBUG_ASSERT(viewMatrix.invertible());
  viewMatrix.invert(&inverse);
  auto cacheCanvas = partialCache->getCanvas();
  auto canvas = surface->getCanvas();
  for (auto& drawRect : drawRects) {
    AutoCanvasRestore autoRestore(cacheCanvas);
    cacheCanvas->clipRect(drawRect);
    cacheCanvas->clear();
    cacheCanvas->setMatrix(viewMatrix);
    DrawArgs args(context);
    auto renderRect = inverse.mapRect(drawRect);
    args.renderRect = &renderRect;
    BackgroundArgs backgroundArgs = {surface->getContext(), drawRect, viewMatrix};
    args.backgroundArgs = &backgroundArgs;
    _root->drawLayer(args, cacheCanvas, 1.0f, BlendMode::SrcOver);
  }
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
  auto renderTasks = invalidateTileCaches(dirtyRegions);
  auto renderTiles = collectRenderTiles(surface, &renderTasks);
  if (renderTiles.empty()) {
    return renderDirect(surface, autoClear);
  }
  std::vector<Rect> dirtyRects = {};
  auto contentOffsetX = roundf(_contentOffset.x);
  auto contentOffsetY = roundf(_contentOffset.y);
  auto surfaceRect = Rect::MakeWH(surface->width(), surface->height());
  for (auto& task : renderTasks) {
    auto dirtyRect = task.drawRect;
    dirtyRect.offset(contentOffsetX, contentOffsetY);
    if (dirtyRect.intersect(surfaceRect)) {
      dirtyRects.emplace_back(dirtyRect);
    }
    renderTileTask(task);
  }
  drawTilesToSurface(renderTiles, surface, autoClear);
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

std::vector<TileRenderTask> DisplayList::invalidateTileCaches(
    const std::vector<Rect>& dirtyRegions) {
  if (dirtyRegions.empty()) {
    return {};
  }
  std::vector<TileRenderTask> renderTasks = {};
  std::vector<float> emptyScales = {};
  for (auto& [scale, tileCache] : tileCaches) {
    if (scale == _zoomScale) {
      invalidateCurrentTileCache(tileCache, dirtyRegions, &renderTasks);
      continue;
    }
    auto zoomMatrix = Matrix::MakeScale(scale);
    auto dirtyRects = MapDirtyRegions(dirtyRegions, zoomMatrix);
    for (auto& dirtyRect : dirtyRects) {
      auto tiles = tileCache->getTilesUnderRect(dirtyRect);
      for (auto& tile : tiles) {
        tileCache->removeTile(tile->tileX, tile->tileY);
      }
      emptyTiles.insert(emptyTiles.end(), tiles.begin(), tiles.end());
    }
    if (tileCache->empty()) {
      emptyScales.push_back(scale);
    }
  }
  for (auto& scale : emptyScales) {
    auto result = tileCaches.find(scale);
    if (result != tileCaches.end()) {
      delete result->second;
      tileCaches.erase(result);
    }
  }
  return renderTasks;
}

void DisplayList::invalidateCurrentTileCache(const TileCache* tileCache,
                                             const std::vector<Rect>& dirtyRegions,
                                             std::vector<TileRenderTask>* renderTasks) const {
  auto zoomMatrix = Matrix::MakeScale(_zoomScale);
  auto dirtyRects = MapDirtyRegions(dirtyRegions, zoomMatrix, true);
  for (auto& dirtyRect : dirtyRects) {
    bool continuous = false;
    auto tiles = tileCache->getTilesUnderRect(dirtyRect, &continuous);
    if (continuous) {
      renderTasks->emplace_back(tiles.front().get(), _tileSize, dirtyRect);
    } else {
      for (auto& tile : tiles) {
        auto drawRect = tile->getTileRect(_tileSize);
        if (drawRect.intersect(dirtyRect)) {
          renderTasks->emplace_back(tile.get(), _tileSize, drawRect);
        }
      }
    }
  }
}

std::vector<std::shared_ptr<Tile>> DisplayList::collectRenderTiles(
    const Surface* surface, std::vector<TileRenderTask>* renderTasks) {
  TileCache* tileCache = nullptr;
  auto result = tileCaches.find(_zoomScale);
  if (result != tileCaches.end()) {
    tileCache = result->second;
  } else {
    tileCache = new TileCache(_tileSize);
    tileCaches[_zoomScale] = tileCache;
  }
  auto renderRect = Rect::MakeWH(surface->width(), surface->height());
  renderRect.offset(-_contentOffset.x, -_contentOffset.y);
  int startX = static_cast<int>(floorf(renderRect.left / static_cast<float>(_tileSize)));
  int startY = static_cast<int>(floorf(renderRect.top / static_cast<float>(_tileSize)));
  int endX = static_cast<int>(ceilf(renderRect.right / static_cast<float>(_tileSize)));
  int endY = static_cast<int>(ceilf(renderRect.bottom / static_cast<float>(_tileSize)));
  std::vector<std::shared_ptr<Tile>> renderTiles = {};
  std::vector<std::pair<int, int>> dirtyGrids = {};
  auto tileCount = static_cast<size_t>(endX - startX) * static_cast<size_t>(endY - startY);
  renderTiles.reserve(tileCount);
  dirtyGrids.reserve(tileCount);
  for (int tileY = startY; tileY < endY; ++tileY) {
    for (int tileX = startX; tileX < endX; ++tileX) {
      auto tile = tileCache->getTile(tileX, tileY);
      if (tile != nullptr) {
        renderTiles.push_back(std::move(tile));
      } else {
        dirtyGrids.emplace_back(tileX, tileY);
      }
    }
  }
  std::vector<std::shared_ptr<Tile>> freeTiles = {};
  bool continuous = false;
  if (renderTiles.empty()) {
    freeTiles = createContinuousTiles(surface, endX - startX, endY - startY);
    continuous = !freeTiles.empty();
  }
  if (freeTiles.empty()) {
    freeTiles = getFreeTiles(dirtyGrids.size(), surface);
  }
  if (dirtyGrids.size() != freeTiles.size()) {
    DEBUG_ASSERT(freeTiles.size() < dirtyGrids.size());
    LOGE("DisplayList::collectRenderTiles() Not enough free tiles available.");
    return {};
  }
  size_t tileIndex = 0;
  for (auto& grid : dirtyGrids) {
    auto& tile = freeTiles[tileIndex++];
    tile->tileX = grid.first;
    tile->tileY = grid.second;
    if (!continuous) {
      renderTasks->emplace_back(tile.get(), _tileSize, tile->getTileRect(_tileSize));
    }
    tileCache->addTile(tile);
    renderTiles.push_back(tile);
  }
  if (continuous) {
    // If we have continuous tiles, we can draw a single rectangle for the entire area.
    auto drawRect = Rect::MakeXYWH(startX * _tileSize, startY * _tileSize,
                                   (endX - startX) * _tileSize, (endY - startY) * _tileSize);
    renderTasks->emplace_back(freeTiles.front().get(), _tileSize, drawRect);
  }
  return renderTiles;
}

static float ScaleRatio(float scaleA, float zoomScale) {
  auto ratio = fabsf(scaleA / zoomScale);
  if (ratio < 1.0f) {
    ratio = 1.0f / ratio;
  }
  return ratio;
}

std::vector<std::shared_ptr<Tile>> DisplayList::getFreeTiles(size_t tileCount,
                                                             const Surface* renderSurface) {
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
  std::vector<std::pair<float, TileCache*>> sortedTileCaches;
  sortedTileCaches.reserve(tileCaches.size());
  for (auto& [scale, tileCache] : tileCaches) {
    sortedTileCaches.emplace_back(scale, tileCache);
  }
  std::sort(sortedTileCaches.begin(), sortedTileCaches.end(),
            [this](const std::pair<float, TileCache*>& a, const std::pair<float, TileCache*>& b) {
              return ScaleRatio(a.first, _zoomScale) < ScaleRatio(b.first, _zoomScale);
            });

  for (auto& [scale, tileCache] : sortedTileCaches) {
    auto centerX = static_cast<float>(renderSurface->width()) * 0.5f - _contentOffset.x;
    auto centerY = static_cast<float>(renderSurface->height()) * 0.5f - _contentOffset.y;
    centerX *= scale / _zoomScale;
    centerY *= scale / _zoomScale;
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
  auto surface = Surface::Make(context, countX * _tileSize, countY * _tileSize,
                               ColorType::RGBA_8888, 1, false, renderSurface->renderFlags());
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
  auto surface = Surface::Make(context, countX * _tileSize, countY * _tileSize,
                               ColorType::RGBA_8888, 1, false, renderSurface->renderFlags());
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
  auto maxTextureSize = context->caps()->maxTextureSize;
  return (maxTextureSize / _tileSize) * (maxTextureSize / _tileSize);
}

void DisplayList::renderTileTask(const TileRenderTask& task) const {
  auto surface = surfaceCaches[task.surfaceIndex].get();
  DEBUG_ASSERT(surface != nullptr);
  auto canvas = surface->getCanvas();
  AutoCanvasRestore autoRestore(canvas);
  auto viewMatrix = Matrix::MakeScale(_zoomScale);
  auto& drawRect = task.drawRect;
  auto offsetX = static_cast<float>(task.surfaceX) - drawRect.left;
  auto offsetY = static_cast<float>(task.surfaceY) - drawRect.top;
  viewMatrix.postTranslate(offsetX, offsetY);
  auto clipRect = drawRect;
  clipRect.offset(offsetX, offsetY);
  canvas->clipRect(clipRect);
  canvas->clear();
  canvas->setMatrix(viewMatrix);
  DrawArgs args(surface->getContext());
  auto renderRect = drawRect;
  renderRect.scale(1.0f / _zoomScale, 1.0f / _zoomScale);
  renderRect.roundOut();
  BackgroundArgs backgroundArgs = {surface->getContext(), drawRect, viewMatrix};
  args.backgroundArgs = &backgroundArgs;
  args.renderRect = &renderRect;
  _root->drawLayer(args, canvas, 1.0f, BlendMode::SrcOver);
}

void DisplayList::drawTilesToSurface(const std::vector<std::shared_ptr<Tile>>& tiles,
                                     Surface* surface, bool autoClear) const {
  std::vector<AtlasArgs> atlasArgs = {};
  atlasArgs.reserve(surfaceCaches.size());
  for (auto& surfaceAtlas : surfaceCaches) {
    AtlasArgs args = {};
    args.atlas = surfaceAtlas->makeImageSnapshot();
    args.matrices.reserve(tiles.size());
    args.rects.reserve(tiles.size());
    atlasArgs.push_back(std::move(args));
  }
  for (auto& tile : tiles) {
    auto& args = atlasArgs[tile->sourceIndex];
    auto offsetX = static_cast<float>(tile->tileX * _tileSize) + _contentOffset.x;
    auto offsetY = static_cast<float>(tile->tileY * _tileSize) + _contentOffset.y;
    args.matrices.push_back(Matrix::MakeTrans(offsetX, offsetY));
    auto rect = tile->getSourceRect(_tileSize);
    args.rects.push_back(rect);
  }
  auto canvas = surface->getCanvas();
  canvas->resetMatrix();
  Paint paint = {};
  paint.setAntiAlias(false);
  if (autoClear) {
    paint.setBlendMode(BlendMode::Src);
  }
  static SamplingOptions sampling(FilterMode::Nearest);
  for (auto& args : atlasArgs) {
    if (args.atlas == nullptr || args.rects.empty()) {
      continue;
    }
    canvas->drawAtlas(args.atlas, args.matrices.data(), args.rects.data(), nullptr,
                      args.matrices.size(), sampling, &paint);
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
  for (auto& regions : lastDirtyRegions) {
    for (auto& region : regions) {
      canvas->drawRect(region, dirtyPaint);
    }
  }
}

Matrix DisplayList::getViewMatrix() const {
  auto viewMatrix = Matrix::MakeScale(_zoomScale);
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

}  // namespace tgfx

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
#include "core/SsaaDebugProbe.h"
#include "core/utils/DecomposeRects.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/TileSortCompareFunc.h"
#include "gpu/RenderContext.h"
#include "layers/BackgroundHandler.h"
#include "layers/BackgroundSnapshotMap.h"
#include "layers/BackgroundSource.h"
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
static constexpr int SSAA_SCALE = 2;

// SSAA Linear-sampling probe: counts how many times each Linear-sampling code path
// is hit per renderTiled() call. Prints only when at least one counter is non-zero.
// Remove once profiling of the 2025美颜需求 SSAA hotspot is done.
namespace {
struct SsaaSamplingCounters {
  int ssaaDownsample = 0;       // SSAA tile → atlas (Linear); also serves as SSAA-path tile count
  int nonSsaaTileCount = 0;     // Non-SSAA-path tile count (same meaning as ssaaDownsample when
                                // _useSSAA is false — used to keep LOGI's "tiles=" field valid in
                                // both paths).
  int tilesFromDirty = 0;       // tileTasks pushed by invalidateCurrentTileCache (dirty-clipped)
  int tilesFromFill = 0;        // tileTasks pushed by collectScreenTasks (full-tile refill path)
  int screenLinear = 0;         // atlas → screen non-identity scale (Linear)
  int screenNearest = 0;        // atlas → screen identity scale (Nearest)
  int throttleScreenLinear = 0; // throttle screen tasks (Linear)
  // Per-frame timings (microseconds) for the SSAA hot path inside drawTileTask.
  int64_t ssaaTileDrawUs = 0;     // sum of drawRootLayer time for all SSAA tiles this frame
  int64_t ssaaTileSnapshotUs = 0; // sum of makeImageSnapshot time
  int64_t ssaaDownsampleUs = 0;   // sum of downsample drawImageRect time
  int64_t ssaaTilePixels = 0;     // sum of ssaaTile width*height (super-sampled pixels)
  int ssaaMaxTileW = 0;           // largest SSAA tile width this frame
  int ssaaMaxTileH = 0;           // largest SSAA tile height this frame
  // SSAA drawRootLayer inner-call counters, aggregated from ssaa_debug::gProbe after each SSAA
  // tile finishes. These expose blend/image behavior driving the drawRootLayer cost.
  int saveLayerCount = 0;
  int drawImageCount = 0;
  int drawImageFilterCount = 0;
  int64_t drawImageSrcPixels = 0;
  int64_t drawImageDstPixels = 0;
  // Subtree cache aggregation (from Layer::drawWithSubtreeCache / getValidSubtreeCache).
  int subtreeCacheEligible = 0;
  int subtreeCacheHit = 0;
  int subtreeCacheBuilt = 0;
  int subtreeCacheOversized = 0;
  int subtreeCacheDisabled = 0;
  int subtreeCacheMissOther = 0;
  // [SSAA-DBG] GPU-synced segment timings for subtree cache. Segment mapping:
  //   segment 1a = subtreeCacheBuildUs  (createSubtreeCacheImage cost, per build)
  //   segment 1b = subtreeCacheDrawHitUs (SubtreeCache::draw cost, per hit)
  //   segment 3  = ssaaTileDrawUs - subtreeCacheBuildUs - subtreeCacheDrawHitUs
  int64_t subtreeCacheBuildUs = 0;
  int64_t subtreeCacheDrawHitUs = 0;
  int subtreeCacheBuildCount = 0;
  int subtreeCacheDrawHitCount = 0;
  // [SSAA-DBG] Round 2 fields — Oversized bucket distribution and fallback direct-draw
  // timing, separated by reason. Nested fallbacks are double-counted (parent's time includes
  // its child's fallback time); read the numbers as "total cost of this fallback family"
  // rather than "each layer's own direct cost".
  int oversizedBucket_le2048 = 0;
  int oversizedBucket_2k_3k = 0;
  int oversizedBucket_3k_4k = 0;
  int oversizedBucket_4k_6k = 0;
  int oversizedBucket_6k_plus = 0;
  int oversizedMinLongEdge = 0;
  int oversizedMaxLongEdge = 0;
  int64_t oversizedFallbackDrawUs = 0;
  int oversizedFallbackDrawCount = 0;
  int64_t otherFallbackDrawUs = 0;
  int otherFallbackDrawCount = 0;
  // [SSAA-DBG] Round 5: otherFallback split. "inRec" = nested inside createSubtreeCacheImage
  // recording (time overlaps with seg1a_build); "direct" = tile main draw chain fallback
  // (time is independent of seg1a). Their sum equals otherFallbackDraw{Us,Count} above.
  int64_t otherFallbackInRecUs = 0;
  int otherFallbackInRecCount = 0;
  int64_t otherFallbackDirectUs = 0;
  int otherFallbackDirectCount = 0;
  // [SSAA-DBG] Round 6: key/entry sanity for SubtreeCache.
  int hasCacheKeyMiss = 0;
  int hasCacheProxyMiss = 0;
  int builtSecondEntry = 0;
  int64_t builtEntriesSizeSum = 0;
  // [SSAA-DBG] Round 7: invalidation churn probes.
  int subtreeInvalidated = 0;
  int builtFromEmpty = 0;
  // [SSAA-DBG] Round 4: physical edge histogram of successfully built cache images. Tells us
  // where the currently-cached layers cluster.
  int builtEdge_le64 = 0;
  int builtEdge_64_128 = 0;
  int builtEdge_128_256 = 0;
  int builtEdge_256_512 = 0;
  int builtEdge_512_1024 = 0;
  int builtEdge_1024_2048 = 0;
  int builtEdge_2048_plus = 0;
  int builtEdgeMin = 0;
  int builtEdgeMax = 0;
  // [SSAA-DBG] Op-type + shader-type distribution mirrored from ssaa_debug::gProbe. Aggregated
  // per renderTiled() and reset at frame boundary.
  int opRect = 0;
  int opRRect = 0;
  int opPath = 0;
  int opShape = 0;
  int opText = 0;
  int opMesh = 0;
  int opAtlas = 0;
  int opFill = 0;
  int opLayer = 0;
  int opImage = 0;
  int shaderNone = 0;
  int shaderColor = 0;
  int shaderImage = 0;
  int shaderGradient = 0;
  int shaderColorFilter = 0;
  int shaderMatrix = 0;
  int shaderBlend = 0;
  int shaderPerlinNoise = 0;
  int shaderOther = 0;
  // [SSAA-DBG] Round 8: per-category GPU us (only populated with gGpuSyncEnabled=true).
  // Index order matches Probe::OpCategory: rect/rrect/path/shape/text/mesh/atlas/fill/layer/image.
  int64_t opGpuUs[10] = {};
  int opGpuFlushCount = 0;
  int frameNo = 0;
};
static SsaaSamplingCounters gSsaaCounters;
}  // namespace

// Definition of the shared Canvas-side probe declared in core/SsaaDebugProbe.h.
namespace ssaa_debug {
Probe gProbe;
std::string gDebugTag;
bool gGpuSyncEnabled = false;
// [SSAA-DBG] Round 7: invalidation counter definition. Populated by
// Layer::invalidateSubtree() between renders; drained by DisplayList's SSAA-DBG log line.
int gInvalidateCounter = 0;
}  // namespace ssaa_debug

void DisplayList::setDebugTag(std::string tag) {
  ssaa_debug::gDebugTag = std::move(tag);
}

void DisplayList::setDebugGpuSyncEnabled(bool enabled) {
  ssaa_debug::gGpuSyncEnabled = enabled;
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

 private:
  // Hold strong references to tiles to ensure they aren't reused by TileCache.
  std::vector<std::shared_ptr<Tile>> tiles = {};
  Rect _sourceRect = {};
  Rect _tileRect = {};
  Rect _strictRect = {};
  bool _identityScale = true;

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

DisplayList::DisplayList() : _root(RootLayer::Make()) {
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

void DisplayList::setUseSSAA(bool use) {
  if (_useSSAA == use) {
    return;
  }
  _useSSAA = use;
  // tileCaches keys off _zoomScaleInt only, so existing atlas tiles rendered in the previous
  // SSAA mode would otherwise be treated as clean and mixed with newly re-rasterized tiles.
  // Drop the whole tile cache (atlas surfaces + tile bookkeeping + ssaaTileSurface) so only
  // one mode's atlas is resident at any time.
  resetCaches();
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
    snapshotMap = captureBackgrounds(surface, {surfaceRect});
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
    snapshotMap = captureBackgrounds(surface, renderRects);
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
  auto tileTasks = invalidateTileCaches(dirtyRegions);
  std::vector<Rect> skippedRects = {};
  std::vector<DrawTask> throttleScreenTasks = {};
  auto screenTasks = collectScreenTasks(surface, !dirtyRegions.empty(), autoClear, &tileTasks,
                                        &skippedRects, &throttleScreenTasks);
  // Throttle tasks and skipped rects must keep the tiled path; only fall back when all are empty.
  if (screenTasks.empty() && throttleScreenTasks.empty() && skippedRects.empty()) {
    recycleCurrentTileTasks(tileTasks);
    return renderDirect(surface, autoClear);
  }
  // tileRect is in zoom space; offset by contentOffset to line it up with surface pixel coords.
  std::unique_ptr<BackgroundSnapshotMap> snapshotMap = nullptr;
  if (_root->hasBackgroundStyle()) {
    std::vector<Rect> captureRects = {};
    captureRects.reserve(tileTasks.size());
    for (auto& task : tileTasks) {
      auto captureRect = task.tileRect();
      captureRect.offset(_contentOffset.x, _contentOffset.y);
      captureRects.push_back(captureRect);
    }
    snapshotMap = captureBackgrounds(surface, captureRects, _useSSAA);
  }
  std::vector<Rect> dirtyRects = {};
  auto surfaceRect = Rect::MakeWH(surface->width(), surface->height());
  for (auto& task : tileTasks) {
    drawTileTask(task, snapshotMap.get(), surface);
    auto dirtyRect = task.tileRect();
    dirtyRect.offset(_contentOffset.x, _contentOffset.y);
    if (dirtyRect.intersect(surfaceRect)) {
      dirtyRects.emplace_back(dirtyRect);
    }
  }
  drawScreenTasks(std::move(screenTasks), std::move(skippedRects), surface, autoClear);
  drawThrottleScreenTasks(std::move(throttleScreenTasks), surface, autoClear);
  // Only emit SSAA-DBG when a benchmark/replay tag is set. This filters out iframe preload
  // / initial render noise so the console only carries replay-period frames aligned by tag.
  if (!ssaa_debug::gDebugTag.empty() &&
      (gSsaaCounters.ssaaDownsample || gSsaaCounters.nonSsaaTileCount ||
       gSsaaCounters.screenLinear || gSsaaCounters.screenNearest ||
       gSsaaCounters.throttleScreenLinear)) {
    // [SSAA-DBG] Round 7: drain the module-global invalidate counter into this frame's log
    // line. This captures all invalidateSubtree() calls that happened between the previous
    // LOGI and now — including those fired during property mutation between renders.
    gSsaaCounters.subtreeInvalidated += ssaa_debug::gInvalidateCounter;
    ssaa_debug::gInvalidateCounter = 0;
    // Total wall-clock spent inside the SSAA hot path this frame (drawRoot + snapshot + downsample).
    const int64_t ssaaTotalUs = gSsaaCounters.ssaaTileDrawUs + gSsaaCounters.ssaaTileSnapshotUs +
                                gSsaaCounters.ssaaDownsampleUs;
    // Total super-sampled pixels rasterized this frame (SSAA tile pixel budget).
    const double megaPixels = static_cast<double>(gSsaaCounters.ssaaTilePixels) / 1e6;
    const double diSrcMpx = static_cast<double>(gSsaaCounters.drawImageSrcPixels) / 1e6;
    const double diDstMpx = static_cast<double>(gSsaaCounters.drawImageDstPixels) / 1e6;
    const int tileCount = gSsaaCounters.ssaaDownsample + gSsaaCounters.nonSsaaTileCount;
    // [SSAA-DBG] Segment analysis LOGI. GPU-synced timings for the subtree cache build/hit
    // and fallback direct-draw are only populated when Window::setMeasureGpu(true) is active;
    // otherwise those fields are 0 (barriers collapse to no-ops so the pipeline is unmodified).
    // ssaaTileDrawUs / ssaaTileSnapshotUs / ssaaDownsampleUs are always populated but only
    // reflect real GPU cost when the sync switch is on.
    // Segment derivation (only meaningful when sync=on):
    //   1a = buildUs   (subtree cache 2x image build)
    //   1b = drawHitUs (subtree cache sample into tile)
    //   3  = drawUs - buildUs - drawHitUs   (non-cached direct draw, 4x fragment cost)
    //   4  = snapUs + downUs                (snapshot + downsample to atlas)
    const bool syncOn = ssaa_debug::gGpuSyncEnabled;
    const double drawMs = static_cast<double>(gSsaaCounters.ssaaTileDrawUs) / 1000.0;
    const double buildMs = static_cast<double>(gSsaaCounters.subtreeCacheBuildUs) / 1000.0;
    const double drawHitMs = static_cast<double>(gSsaaCounters.subtreeCacheDrawHitUs) / 1000.0;
    const double directMs = drawMs - buildMs - drawHitMs;
    const double oversizedFallbackMs =
        static_cast<double>(gSsaaCounters.oversizedFallbackDrawUs) / 1000.0;
    const double otherFallbackMs =
        static_cast<double>(gSsaaCounters.otherFallbackDrawUs) / 1000.0;
    const double otherFallbackInRecMs =
        static_cast<double>(gSsaaCounters.otherFallbackInRecUs) / 1000.0;
    const double otherFallbackDirectMs =
        static_cast<double>(gSsaaCounters.otherFallbackDirectUs) / 1000.0;
    LOGI(
        "[SSAA-DBG tag=%s f%d ssaa=%d sync=%d] tiles=%d(dirty=%d fill=%d) screen(near/lin)=%d/%d throttle=%d "
        "draw=%.2fms snap=%.2fms down=%.2fms total=%.2fms tilePx=%.2fM maxTile=%dx%d "
        "seg1a_build=%.2fms(x%d) seg1b_drawHit=%.2fms(x%d) seg3_direct=%.2fms "
        "oversizedFB=%.2fms(x%d nested-double-counted) otherFB=%.2fms(x%d rec=%.2fms/x%d dir=%.2fms/x%d) "
        "oversizedBuckets(<=2k/2-3k/3-4k/4-6k/6k+)=%d/%d/%d/%d/%d minLE=%d maxLE=%d "
        "builtEdge(<=64/64-128/128-256/256-512/512-1k/1k-2k/>2k)=%d/%d/%d/%d/%d/%d/%d minBE=%d maxBE=%d "
        "sl=%d di=%d(f=%d) diSrc=%.2fMpx diDst=%.2fMpx "
        "op(rect/rrect/path/shape/text/mesh/atlas/fill/layer/image)=%d/%d/%d/%d/%d/%d/%d/%d/%d/%d "
        "shader(none/color/img/grad/cf/mat/blend/perlin/other)=%d/%d/%d/%d/%d/%d/%d/%d/%d "
        "stc=%d(hit=%d built=%d over=%d dis=%d miss=%d) "
        "keyProbe(keyMiss=%d proxyMiss=%d secondEntry=%d entriesAvg=%.2f) "
        "invalProbe(invalidated=%d fromEmpty=%d) "
        "opGpu(rect=%.2f/rr=%.2f/path=%.2f/shape=%.2f/text=%.2f/mesh=%.2f/atlas=%.2f/fill=%.2f/layer=%.2f/image=%.2fms flushes=%d)",
        ssaa_debug::gDebugTag.c_str(),
        gSsaaCounters.frameNo, _useSSAA ? 1 : 0, syncOn ? 1 : 0, tileCount,
        gSsaaCounters.tilesFromDirty, gSsaaCounters.tilesFromFill,
        gSsaaCounters.screenNearest, gSsaaCounters.screenLinear,
        gSsaaCounters.throttleScreenLinear,
        drawMs,
        static_cast<double>(gSsaaCounters.ssaaTileSnapshotUs) / 1000.0,
        static_cast<double>(gSsaaCounters.ssaaDownsampleUs) / 1000.0,
        static_cast<double>(ssaaTotalUs) / 1000.0, megaPixels,
        gSsaaCounters.ssaaMaxTileW, gSsaaCounters.ssaaMaxTileH,
        buildMs, gSsaaCounters.subtreeCacheBuildCount,
        drawHitMs, gSsaaCounters.subtreeCacheDrawHitCount,
        directMs,
        oversizedFallbackMs, gSsaaCounters.oversizedFallbackDrawCount,
        otherFallbackMs, gSsaaCounters.otherFallbackDrawCount,
        otherFallbackInRecMs, gSsaaCounters.otherFallbackInRecCount,
        otherFallbackDirectMs, gSsaaCounters.otherFallbackDirectCount,
        gSsaaCounters.oversizedBucket_le2048, gSsaaCounters.oversizedBucket_2k_3k,
        gSsaaCounters.oversizedBucket_3k_4k, gSsaaCounters.oversizedBucket_4k_6k,
        gSsaaCounters.oversizedBucket_6k_plus,
        gSsaaCounters.oversizedMinLongEdge, gSsaaCounters.oversizedMaxLongEdge,
        gSsaaCounters.builtEdge_le64, gSsaaCounters.builtEdge_64_128,
        gSsaaCounters.builtEdge_128_256, gSsaaCounters.builtEdge_256_512,
        gSsaaCounters.builtEdge_512_1024, gSsaaCounters.builtEdge_1024_2048,
        gSsaaCounters.builtEdge_2048_plus,
        gSsaaCounters.builtEdgeMin, gSsaaCounters.builtEdgeMax,
        gSsaaCounters.saveLayerCount, gSsaaCounters.drawImageCount,
        gSsaaCounters.drawImageFilterCount, diSrcMpx, diDstMpx,
        gSsaaCounters.opRect, gSsaaCounters.opRRect, gSsaaCounters.opPath,
        gSsaaCounters.opShape, gSsaaCounters.opText, gSsaaCounters.opMesh,
        gSsaaCounters.opAtlas, gSsaaCounters.opFill, gSsaaCounters.opLayer,
        gSsaaCounters.opImage,
        gSsaaCounters.shaderNone, gSsaaCounters.shaderColor, gSsaaCounters.shaderImage,
        gSsaaCounters.shaderGradient, gSsaaCounters.shaderColorFilter,
        gSsaaCounters.shaderMatrix, gSsaaCounters.shaderBlend,
        gSsaaCounters.shaderPerlinNoise, gSsaaCounters.shaderOther,
        gSsaaCounters.subtreeCacheEligible, gSsaaCounters.subtreeCacheHit,
        gSsaaCounters.subtreeCacheBuilt, gSsaaCounters.subtreeCacheOversized,
        gSsaaCounters.subtreeCacheDisabled, gSsaaCounters.subtreeCacheMissOther,
        gSsaaCounters.hasCacheKeyMiss, gSsaaCounters.hasCacheProxyMiss,
        gSsaaCounters.builtSecondEntry,
        gSsaaCounters.subtreeCacheBuilt > 0
            ? static_cast<double>(gSsaaCounters.builtEntriesSizeSum) /
                  static_cast<double>(gSsaaCounters.subtreeCacheBuilt)
            : 0.0,
        gSsaaCounters.subtreeInvalidated, gSsaaCounters.builtFromEmpty,
        static_cast<double>(gSsaaCounters.opGpuUs[0]) / 1000.0,
        static_cast<double>(gSsaaCounters.opGpuUs[1]) / 1000.0,
        static_cast<double>(gSsaaCounters.opGpuUs[2]) / 1000.0,
        static_cast<double>(gSsaaCounters.opGpuUs[3]) / 1000.0,
        static_cast<double>(gSsaaCounters.opGpuUs[4]) / 1000.0,
        static_cast<double>(gSsaaCounters.opGpuUs[5]) / 1000.0,
        static_cast<double>(gSsaaCounters.opGpuUs[6]) / 1000.0,
        static_cast<double>(gSsaaCounters.opGpuUs[7]) / 1000.0,
        static_cast<double>(gSsaaCounters.opGpuUs[8]) / 1000.0,
        static_cast<double>(gSsaaCounters.opGpuUs[9]) / 1000.0,
        gSsaaCounters.opGpuFlushCount);
  }
  ++gSsaaCounters.frameNo;
  gSsaaCounters.ssaaDownsample = 0;
  gSsaaCounters.nonSsaaTileCount = 0;
  gSsaaCounters.tilesFromDirty = 0;
  gSsaaCounters.tilesFromFill = 0;
  gSsaaCounters.screenLinear = 0;
  gSsaaCounters.screenNearest = 0;
  gSsaaCounters.throttleScreenLinear = 0;
  gSsaaCounters.ssaaTileDrawUs = 0;
  gSsaaCounters.ssaaTileSnapshotUs = 0;
  gSsaaCounters.ssaaDownsampleUs = 0;
  gSsaaCounters.ssaaTilePixels = 0;
  gSsaaCounters.ssaaMaxTileW = 0;
  gSsaaCounters.ssaaMaxTileH = 0;
  gSsaaCounters.saveLayerCount = 0;
  gSsaaCounters.drawImageCount = 0;
  gSsaaCounters.drawImageFilterCount = 0;
  gSsaaCounters.drawImageSrcPixels = 0;
  gSsaaCounters.drawImageDstPixels = 0;
  gSsaaCounters.subtreeCacheEligible = 0;
  gSsaaCounters.subtreeCacheHit = 0;
  gSsaaCounters.subtreeCacheBuilt = 0;
  gSsaaCounters.subtreeCacheOversized = 0;
  gSsaaCounters.subtreeCacheDisabled = 0;
  gSsaaCounters.subtreeCacheMissOther = 0;
  gSsaaCounters.subtreeCacheBuildUs = 0;
  gSsaaCounters.subtreeCacheDrawHitUs = 0;
  gSsaaCounters.subtreeCacheBuildCount = 0;
  gSsaaCounters.subtreeCacheDrawHitCount = 0;
  gSsaaCounters.oversizedBucket_le2048 = 0;
  gSsaaCounters.oversizedBucket_2k_3k = 0;
  gSsaaCounters.oversizedBucket_3k_4k = 0;
  gSsaaCounters.oversizedBucket_4k_6k = 0;
  gSsaaCounters.oversizedBucket_6k_plus = 0;
  gSsaaCounters.oversizedMinLongEdge = 0;
  gSsaaCounters.oversizedMaxLongEdge = 0;
  gSsaaCounters.oversizedFallbackDrawUs = 0;
  gSsaaCounters.oversizedFallbackDrawCount = 0;
  gSsaaCounters.otherFallbackDrawUs = 0;
  gSsaaCounters.otherFallbackDrawCount = 0;
  gSsaaCounters.otherFallbackInRecUs = 0;
  gSsaaCounters.otherFallbackInRecCount = 0;
  gSsaaCounters.otherFallbackDirectUs = 0;
  gSsaaCounters.otherFallbackDirectCount = 0;
  gSsaaCounters.hasCacheKeyMiss = 0;
  gSsaaCounters.hasCacheProxyMiss = 0;
  gSsaaCounters.builtSecondEntry = 0;
  gSsaaCounters.builtEntriesSizeSum = 0;
  gSsaaCounters.subtreeInvalidated = 0;
  gSsaaCounters.builtFromEmpty = 0;
  for (int i = 0; i < ssaa_debug::Probe::OpCatCount; ++i) {
    gSsaaCounters.opGpuUs[i] = 0;
  }
  gSsaaCounters.opGpuFlushCount = 0;
  gSsaaCounters.builtEdge_le64 = 0;
  gSsaaCounters.builtEdge_64_128 = 0;
  gSsaaCounters.builtEdge_128_256 = 0;
  gSsaaCounters.builtEdge_256_512 = 0;
  gSsaaCounters.builtEdge_512_1024 = 0;
  gSsaaCounters.builtEdge_1024_2048 = 0;
  gSsaaCounters.builtEdge_2048_plus = 0;
  gSsaaCounters.builtEdgeMin = 0;
  gSsaaCounters.builtEdgeMax = 0;
  gSsaaCounters.opRect = 0;
  gSsaaCounters.opRRect = 0;
  gSsaaCounters.opPath = 0;
  gSsaaCounters.opShape = 0;
  gSsaaCounters.opText = 0;
  gSsaaCounters.opMesh = 0;
  gSsaaCounters.opAtlas = 0;
  gSsaaCounters.opFill = 0;
  gSsaaCounters.opLayer = 0;
  gSsaaCounters.opImage = 0;
  gSsaaCounters.shaderNone = 0;
  gSsaaCounters.shaderColor = 0;
  gSsaaCounters.shaderImage = 0;
  gSsaaCounters.shaderGradient = 0;
  gSsaaCounters.shaderColorFilter = 0;
  gSsaaCounters.shaderMatrix = 0;
  gSsaaCounters.shaderBlend = 0;
  gSsaaCounters.shaderPerlinNoise = 0;
  gSsaaCounters.shaderOther = 0;
  return dirtyRects;
}

void DisplayList::checkTileCount(Surface* renderSurface) {
  DEBUG_ASSERT(renderSurface != nullptr);
  const auto tileSizeFloat = static_cast<float>(_tileSize);
  const auto tileCountX =
      FloatCeilToInt(static_cast<float>(renderSurface->width()) / tileSizeFloat);
  const auto tileCountY =
      FloatCeilToInt(static_cast<float>(renderSurface->height()) / tileSizeFloat);
  auto minTileCount = (tileCountX + 1) * (tileCountY + 1);
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
  auto width = FloatSaturateToInt(sqrtf(static_cast<float>(remainingTileCount)));
  int height = FloatCeilToInt(static_cast<float>(remainingTileCount) / static_cast<float>(width));
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

void DisplayList::recycleCurrentTileTasks(const std::vector<DrawTask>& tileTasks) {
  if (tileTasks.empty()) {
    return;
  }
  auto result = tileCaches.find(_zoomScaleInt);
  DEBUG_ASSERT(result != tileCaches.end());
  if (result == tileCaches.end()) {
    return;
  }
  auto tileCache = result->second;
  for (auto& task : tileTasks) {
    for (auto& tile : task.getTiles()) {
      if (tileCache->removeTile(tile->tileX, tile->tileY)) {
        emptyTiles.push_back(tile);
      }
    }
  }
  if (tileCache->empty()) {
    delete tileCache;
    tileCaches.erase(result);
  }
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
      ++gSsaaCounters.tilesFromDirty;
    } else {
      for (auto& tile : tiles) {
        auto drawRect = tile->getTileRect(_tileSize);
        if (drawRect.intersect(dirtyRect)) {
          tileTasks->emplace_back(tile, _tileSize, drawRect);
          ++gSsaaCounters.tilesFromDirty;
        }
      }
    }
  }
}

std::vector<DrawTask> DisplayList::collectScreenTasks(const Surface* surface, bool hasDirtyRegions,
                                                      bool autoClear,
                                                      std::vector<DrawTask>* tileTasks,
                                                      std::vector<Rect>* skippedRects,
                                                      std::vector<DrawTask>* throttleScreenTasks) {
  auto maxBudget = _maxTilesRefinedPerFrame;
  const auto useFallback = _tileUpdateMode != TileUpdateMode::Immediate;
  if (lastContentOffset != _contentOffset || lastZoomScaleInt != _zoomScaleInt) {
    updateMousePosition();
    lastContentOffset = _contentOffset;
    lastZoomScaleInt = _zoomScaleInt;
    maxBudget = 0;
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
  const auto tileSizeFloat = static_cast<float>(_tileSize);
  const auto startX = FloatFloorToInt(renderRect.left / tileSizeFloat);
  const auto startY = FloatFloorToInt(renderRect.top / tileSizeFloat);
  const auto endX = FloatCeilToInt(renderRect.right / tileSizeFloat);
  const auto endY = FloatCeilToInt(renderRect.bottom / tileSizeFloat);

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
      continue;
    }
    if (!useFallback || maxBudget > 0) {
      if (useFallback) {
        maxBudget--;
      }
      dirtyGrids.emplace_back(tileX, tileY);
    } else {
      auto fallbackTasks = getFallbackDrawTasks(tileX, tileY, fallbackTileCaches);
      if (!fallbackTasks.empty()) {
        hasZoomBlurTiles = true;
        screenTasks.insert(screenTasks.end(), fallbackTasks.begin(), fallbackTasks.end());
        continue;
      }
      // Rasterize in Smooth mode, or when this frame has dirty regions: dirty regions also
      // clear fallback tiles at other scales, so a skipRect would render as background color.
      if (_tileUpdateMode == TileUpdateMode::Smooth || hasDirtyRegions) {
        dirtyGrids.emplace_back(tileX, tileY);
        continue;
      }
      hasZoomBlurTiles = true;
      skippedRects->emplace_back(
          Rect::MakeXYWH(tileX * _tileSize, tileY * _tileSize, _tileSize, _tileSize));
      if (!autoClear) {
        // Under autoClear=false, throttle fallback tiles are not drawn because SrcOver blending
        // would let farther tiles bleed through near-tile alpha edges, producing cross-scale
        // artifacts. Skip collection entirely to avoid wasted work.
        continue;
      }
      auto throttleFallback = getThrottleFallbackTasks(tileX, tileY, fallbackTileCaches);
      if (!throttleFallback.empty()) {
        throttleScreenTasks->insert(throttleScreenTasks->end(), throttleFallback.begin(),
                                    throttleFallback.end());
      }
    }
  }
  std::vector<std::shared_ptr<Tile>> freeTiles = {};
  bool continuous = false;
  if (screenTasks.empty() && throttleScreenTasks->empty()) {
    freeTiles = createContinuousTiles(surface, endX - startX, endY - startY);
    continuous = !freeTiles.empty();
    dirtyGrids = GenerateGridTiles(startX, endX, startY, endY);
    skippedRects->clear();
    hasZoomBlurTiles = false;
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
  if (continuous) {
    // If we have continuous tiles, we can draw a single rectangle for the entire area.
    auto drawRect = Rect::MakeXYWH(startX * _tileSize, startY * _tileSize,
                                   (endX - startX) * _tileSize, (endY - startY) * _tileSize);
    tileTasks->emplace_back(std::move(freeTiles), _tileSize, drawRect);
    ++gSsaaCounters.tilesFromFill;
  } else {
    for (auto& tile : taskTiles) {
      tileTasks->emplace_back(tile, _tileSize);
      ++gSsaaCounters.tilesFromFill;
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
  if (_tileUpdateMode == TileUpdateMode::Immediate) {
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

std::vector<DrawTask> DisplayList::getThrottleFallbackTasks(
    int tileX, int tileY, const std::vector<std::pair<float, TileCache*>>& fallbackCaches) const {
  auto currentZoomScale = ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision);
  DEBUG_ASSERT(currentZoomScale != 0.0f);
  auto tileRect = Rect::MakeXYWH(tileX * _tileSize, tileY * _tileSize, _tileSize, _tileSize);
  auto renderBounds = _root->renderBounds;
  renderBounds.scale(currentZoomScale, currentZoomScale);
  renderBounds.roundOut();
  if (!tileRect.intersect(renderBounds)) {
    return {};
  }
  // Sort fallback caches farthest-first so that nearer (higher-quality) tiles are drawn on top
  // of farther (lower-quality) ones during rendering. When two caches have the same ScaleRatio
  // (e.g. 0.5x and 2.0x relative to 1.0x), the upsample cache is placed before the downsample
  // cache so the downsample output (sharper) overlays the upsample output (blurrier).
  auto orderedCaches = fallbackCaches;
  std::sort(orderedCaches.begin(), orderedCaches.end(),
            [currentZoomScale](const std::pair<float, TileCache*>& a,
                               const std::pair<float, TileCache*>& b) {
              auto ratioA = ScaleRatio(a.first, currentZoomScale);
              auto ratioB = ScaleRatio(b.first, currentZoomScale);
              if (ratioA != ratioB) {
                return ratioA > ratioB;
              }
              return a.first < b.first;
            });
  std::vector<DrawTask> tasks = {};
  for (auto& [scale, tileCache] : orderedCaches) {
    if (scale == currentZoomScale || tileCache->empty()) {
      continue;
    }
    auto scaleRatio = scale / currentZoomScale;
    DEBUG_ASSERT(scaleRatio != 0.0f);
    auto zoomedRect = tileRect;
    zoomedRect.scale(scaleRatio, scaleRatio);
    auto covering = tileCache->getTilesUnderRect(zoomedRect, false);
    if (covering.empty()) {
      continue;
    }
    for (auto& t : covering) {
      auto drawRect = t->getTileRect(_tileSize);
      if (drawRect.intersect(zoomedRect)) {
        tasks.emplace_back(t, _tileSize, drawRect, 1.0f / scaleRatio);
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
  int countX = FloatSaturateToInt(sqrtf(static_cast<float>(tileCount)));
  int countY = FloatCeilToInt(static_cast<float>(tileCount) / static_cast<float>(countX));
  if (countX < requestCountX) {
    countX = requestCountX;
    countY = FloatCeilToInt(static_cast<float>(tileCount) / static_cast<float>(countX));
  } else if (countY < requestCountY) {
    countY = requestCountY;
    countX = FloatCeilToInt(static_cast<float>(tileCount) / static_cast<float>(countY));
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
  int countX = FloatSaturateToInt(sqrtf(static_cast<float>(tileCount)));
  int countY = FloatCeilToInt(static_cast<float>(tileCount) / static_cast<float>(countX));
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

void DisplayList::drawTileTask(const DrawTask& task, BackgroundSnapshotMap* snapshots,
                               const Surface* renderSurface) {
  auto atlasSurface = surfaceCaches[task.sourceIndex()].get();
  DEBUG_ASSERT(atlasSurface != nullptr);
  auto currentZoomScale = ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision);
  DEBUG_ASSERT(currentZoomScale != 0.0f);
  auto& tileRect = task.tileRect();
  auto& sourceRect = task.sourceRect();

  // SSAA path for Tiled mode: render each tile at 2x resolution with NoAA into a reusable SSAA
  // tile surface, then downsample to the atlas with linear sampling. This provides anti-aliasing
  // via supersampling and avoids edge bleeding caused by coverage-based AA when layers stack.
  if (_useSSAA) {
    const auto ssaaTileWidth = tileRect.width() * SSAA_SCALE;
    const auto ssaaTileHeight = tileRect.height() * SSAA_SCALE;
    const auto surfaceWidth = FloatCeilToInt(ssaaTileWidth);
    const auto surfaceHeight = FloatCeilToInt(ssaaTileHeight);
    auto tileSurface = getOrCreateSSAATileSurface(renderSurface, surfaceWidth, surfaceHeight);
    if (tileSurface != nullptr) {
      // Render to the SSAA tile surface at 2x scale starting from (0, 0).
      auto viewMatrix = Matrix::MakeScale(currentZoomScale * SSAA_SCALE);
      viewMatrix.postTranslate(-tileRect.left * SSAA_SCALE, -tileRect.top * SSAA_SCALE);
      auto tileClipRect = Rect::MakeWH(ssaaTileWidth, ssaaTileHeight);
      // forceNoEdgeAA=true is written into the root DrawArgs by drawRootLayer and propagates
      // along the DrawArgs copy chain into every Layer::allowsEdgeAntialiasing read site as
      // well as every derived intermediate surface (layer style offscreen, background snapshot,
      // 3D leaf surface). All draws on this SSAA tile surface therefore go through with
      // AAType::None.
      // [SSAA-DBG] time drawRootLayer + snapshot + downsample separately.
      // Also accumulate per-tile pixel counts to expose "big tile vs many tiles".
      const int64_t ssaaTilePixels =
          static_cast<int64_t>(surfaceWidth) * static_cast<int64_t>(surfaceHeight);
      gSsaaCounters.ssaaTilePixels += ssaaTilePixels;
      if (surfaceWidth > gSsaaCounters.ssaaMaxTileW) {
        gSsaaCounters.ssaaMaxTileW = surfaceWidth;
      }
      if (surfaceHeight > gSsaaCounters.ssaaMaxTileH) {
        gSsaaCounters.ssaaMaxTileH = surfaceHeight;
      }
      auto ssaaT0 = std::chrono::steady_clock::now();
      // [SSAA-DBG] Turn on the Canvas-side probe so saveLayer/drawImage inside this specific
      // drawRootLayer call are counted; aggregate right after and clear before leaving.
      ssaa_debug::gProbe = {};
      ssaa_debug::gProbe.active = true;
      // [SSAA-DBG] GPU-synced timing. Pre-barrier isolates prior work; the drawRootLayer /
      // makeImageSnapshot / downsample sections are each bracketed by flushAndSubmit(true) so
      // wall-clock reflects real GPU cost per segment. Only paid when a bench debug tag is set
      // (gProbe.active is scoped to this branch).
      auto* ssaaCtx = tileSurface->getContext();
      if (ssaaCtx && ssaa_debug::gGpuSyncEnabled) {
        ssaaCtx->flushAndSubmit(true);
      }
      auto ssaaDrawT0 = std::chrono::steady_clock::now();
      drawRootLayer(tileSurface, tileClipRect, viewMatrix, true, snapshots, true,
                    static_cast<float>(SSAA_SCALE));
      if (ssaaCtx && ssaa_debug::gGpuSyncEnabled) {
        ssaaCtx->flushAndSubmit(true);
      }
      auto ssaaDrawT1 = std::chrono::steady_clock::now();
      // [SSAA-DBG] Round 8: close the trailing op-category window opened by the last op in
      // drawRootLayer. The flush above already synced GPU work; attribute the wall-clock
      // elapsed since opCategoryT0Ns to the still-open category and reset for next tile.
      if (ssaa_debug::gGpuSyncEnabled &&
          ssaa_debug::gProbe.currentOpCategory != ssaa_debug::Probe::OpCatNone) {
        auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         ssaaDrawT1.time_since_epoch())
                         .count();
        auto elapsedUs = (nowNs - ssaa_debug::gProbe.opCategoryT0Ns) / 1000;
        auto cat = ssaa_debug::gProbe.currentOpCategory;
        if (cat >= 0 && cat < ssaa_debug::Probe::OpCatCount) {
          ssaa_debug::gProbe.opGpuUs[cat] += elapsedUs;
        }
        ssaa_debug::gProbe.currentOpCategory = ssaa_debug::Probe::OpCatNone;
      }
      ssaa_debug::gProbe.active = false;
      gSsaaCounters.saveLayerCount += ssaa_debug::gProbe.saveLayerCount;
      gSsaaCounters.drawImageCount += ssaa_debug::gProbe.drawImageCount;
      gSsaaCounters.drawImageFilterCount += ssaa_debug::gProbe.drawImageFilterCount;
      gSsaaCounters.drawImageSrcPixels += ssaa_debug::gProbe.drawImageSrcPixels;
      gSsaaCounters.drawImageDstPixels += ssaa_debug::gProbe.drawImageDstPixels;
      gSsaaCounters.subtreeCacheEligible += ssaa_debug::gProbe.subtreeCacheEligible;
      gSsaaCounters.subtreeCacheHit += ssaa_debug::gProbe.subtreeCacheHit;
      gSsaaCounters.subtreeCacheBuilt += ssaa_debug::gProbe.subtreeCacheBuilt;
      gSsaaCounters.subtreeCacheOversized += ssaa_debug::gProbe.subtreeCacheOversized;
      gSsaaCounters.subtreeCacheDisabled += ssaa_debug::gProbe.subtreeCacheDisabled;
      gSsaaCounters.subtreeCacheMissOther += ssaa_debug::gProbe.subtreeCacheMissOther;
      // [SSAA-DBG] Segment 1a/1b GPU-synced times aggregated from inside drawRootLayer.
      gSsaaCounters.subtreeCacheBuildUs += ssaa_debug::gProbe.subtreeCacheBuildUs;
      gSsaaCounters.subtreeCacheDrawHitUs += ssaa_debug::gProbe.subtreeCacheDrawHitUs;
      gSsaaCounters.subtreeCacheBuildCount += ssaa_debug::gProbe.subtreeCacheBuildCount;
      gSsaaCounters.subtreeCacheDrawHitCount += ssaa_debug::gProbe.subtreeCacheDrawHitCount;
      // [SSAA-DBG] Round 2: Oversized histogram + fallback direct-draw split.
      gSsaaCounters.oversizedBucket_le2048 += ssaa_debug::gProbe.oversizedBucket_le2048;
      gSsaaCounters.oversizedBucket_2k_3k += ssaa_debug::gProbe.oversizedBucket_2k_3k;
      gSsaaCounters.oversizedBucket_3k_4k += ssaa_debug::gProbe.oversizedBucket_3k_4k;
      gSsaaCounters.oversizedBucket_4k_6k += ssaa_debug::gProbe.oversizedBucket_4k_6k;
      gSsaaCounters.oversizedBucket_6k_plus += ssaa_debug::gProbe.oversizedBucket_6k_plus;
      if (ssaa_debug::gProbe.oversizedMinLongEdge > 0 &&
          (gSsaaCounters.oversizedMinLongEdge == 0 ||
           ssaa_debug::gProbe.oversizedMinLongEdge < gSsaaCounters.oversizedMinLongEdge)) {
        gSsaaCounters.oversizedMinLongEdge = ssaa_debug::gProbe.oversizedMinLongEdge;
      }
      if (ssaa_debug::gProbe.oversizedMaxLongEdge > gSsaaCounters.oversizedMaxLongEdge) {
        gSsaaCounters.oversizedMaxLongEdge = ssaa_debug::gProbe.oversizedMaxLongEdge;
      }
      gSsaaCounters.oversizedFallbackDrawUs += ssaa_debug::gProbe.oversizedFallbackDrawUs;
      gSsaaCounters.oversizedFallbackDrawCount += ssaa_debug::gProbe.oversizedFallbackDrawCount;
      gSsaaCounters.otherFallbackDrawUs += ssaa_debug::gProbe.otherFallbackDrawUs;
      gSsaaCounters.otherFallbackDrawCount += ssaa_debug::gProbe.otherFallbackDrawCount;
      // [SSAA-DBG] Round 5: inRec vs direct split.
      gSsaaCounters.otherFallbackInRecUs += ssaa_debug::gProbe.otherFallbackInRecUs;
      gSsaaCounters.otherFallbackInRecCount += ssaa_debug::gProbe.otherFallbackInRecCount;
      gSsaaCounters.otherFallbackDirectUs += ssaa_debug::gProbe.otherFallbackDirectUs;
      gSsaaCounters.otherFallbackDirectCount += ssaa_debug::gProbe.otherFallbackDirectCount;
      // [SSAA-DBG] Round 6: SubtreeCache key/entry probes.
      gSsaaCounters.hasCacheKeyMiss     += ssaa_debug::gProbe.hasCacheKeyMiss;
      gSsaaCounters.hasCacheProxyMiss   += ssaa_debug::gProbe.hasCacheProxyMiss;
      gSsaaCounters.builtSecondEntry    += ssaa_debug::gProbe.builtSecondEntry;
      gSsaaCounters.builtEntriesSizeSum += ssaa_debug::gProbe.builtEntriesSizeSum;
      // [SSAA-DBG] Round 7: invalidation churn probes.
      gSsaaCounters.subtreeInvalidated  += ssaa_debug::gProbe.subtreeInvalidated;
      gSsaaCounters.builtFromEmpty      += ssaa_debug::gProbe.builtFromEmpty;
      // Note: subtreeInvalidated from gProbe is always 0 in Round 7 onward — the real value
      // is drained once per LOGI line from the module-global gInvalidateCounter (see below).
      // [SSAA-DBG] Round 8: per-category GPU us aggregation.
      for (int i = 0; i < ssaa_debug::Probe::OpCatCount; ++i) {
        gSsaaCounters.opGpuUs[i] += ssaa_debug::gProbe.opGpuUs[i];
      }
      gSsaaCounters.opGpuFlushCount += ssaa_debug::gProbe.opGpuFlushCount;
      // [SSAA-DBG] Round 4 aggregation.
      gSsaaCounters.builtEdge_le64      += ssaa_debug::gProbe.builtEdge_le64;
      gSsaaCounters.builtEdge_64_128    += ssaa_debug::gProbe.builtEdge_64_128;
      gSsaaCounters.builtEdge_128_256   += ssaa_debug::gProbe.builtEdge_128_256;
      gSsaaCounters.builtEdge_256_512   += ssaa_debug::gProbe.builtEdge_256_512;
      gSsaaCounters.builtEdge_512_1024  += ssaa_debug::gProbe.builtEdge_512_1024;
      gSsaaCounters.builtEdge_1024_2048 += ssaa_debug::gProbe.builtEdge_1024_2048;
      gSsaaCounters.builtEdge_2048_plus += ssaa_debug::gProbe.builtEdge_2048_plus;
      if (ssaa_debug::gProbe.builtEdgeMin > 0 &&
          (gSsaaCounters.builtEdgeMin == 0 ||
           ssaa_debug::gProbe.builtEdgeMin < gSsaaCounters.builtEdgeMin)) {
        gSsaaCounters.builtEdgeMin = ssaa_debug::gProbe.builtEdgeMin;
      }
      if (ssaa_debug::gProbe.builtEdgeMax > gSsaaCounters.builtEdgeMax) {
        gSsaaCounters.builtEdgeMax = ssaa_debug::gProbe.builtEdgeMax;
      }
      // [SSAA-DBG] Op-type + shader-type aggregation (SSAA tile path).
      gSsaaCounters.opRect          += ssaa_debug::gProbe.opRect;
      gSsaaCounters.opRRect         += ssaa_debug::gProbe.opRRect;
      gSsaaCounters.opPath          += ssaa_debug::gProbe.opPath;
      gSsaaCounters.opShape         += ssaa_debug::gProbe.opShape;
      gSsaaCounters.opText          += ssaa_debug::gProbe.opText;
      gSsaaCounters.opMesh          += ssaa_debug::gProbe.opMesh;
      gSsaaCounters.opAtlas         += ssaa_debug::gProbe.opAtlas;
      gSsaaCounters.opFill          += ssaa_debug::gProbe.opFill;
      gSsaaCounters.opLayer         += ssaa_debug::gProbe.opLayer;
      gSsaaCounters.opImage         += ssaa_debug::gProbe.opImage;
      gSsaaCounters.shaderNone      += ssaa_debug::gProbe.shaderNone;
      gSsaaCounters.shaderColor     += ssaa_debug::gProbe.shaderColor;
      gSsaaCounters.shaderImage     += ssaa_debug::gProbe.shaderImage;
      gSsaaCounters.shaderGradient  += ssaa_debug::gProbe.shaderGradient;
      gSsaaCounters.shaderColorFilter += ssaa_debug::gProbe.shaderColorFilter;
      gSsaaCounters.shaderMatrix    += ssaa_debug::gProbe.shaderMatrix;
      gSsaaCounters.shaderBlend     += ssaa_debug::gProbe.shaderBlend;
      gSsaaCounters.shaderPerlinNoise += ssaa_debug::gProbe.shaderPerlinNoise;
      gSsaaCounters.shaderOther     += ssaa_debug::gProbe.shaderOther;
      auto ssaaT1 = std::chrono::steady_clock::now();
      auto image = tileSurface->makeImageSnapshot();
      if (ssaaCtx && ssaa_debug::gGpuSyncEnabled) {
        ssaaCtx->flushAndSubmit(true);
      }
      auto ssaaT2 = std::chrono::steady_clock::now();
      // ssaaTileDrawUs is now measured only across the GPU-synced drawRootLayer window so it
      // aligns with the "segment 1a+1b+3" definition; subtreeCacheBuild/DrawHit are already
      // counted inside this window and can be subtracted to derive segment 3.
      gSsaaCounters.ssaaTileDrawUs +=
          std::chrono::duration_cast<std::chrono::microseconds>(ssaaDrawT1 - ssaaDrawT0).count();
      gSsaaCounters.ssaaTileSnapshotUs +=
          std::chrono::duration_cast<std::chrono::microseconds>(ssaaT2 - ssaaT1).count();
      (void)ssaaT0;

      // Downsample to the atlas with linear sampling.
      auto atlasCanvas = atlasSurface->getCanvas();
      AutoCanvasRestore autoRestore(atlasCanvas);
      atlasCanvas->resetMatrix();
      Paint paint = {};
      paint.setAntiAlias(false);
      paint.setBlendMode(BlendMode::Src);
      static SamplingOptions linearSampling(FilterMode::Linear, MipmapMode::None);
      ++gSsaaCounters.ssaaDownsample;
      // [SSAA-DBG] Segment 4: GPU-synced downsample timing. Pre-barrier isolates prior work
      // (drawRootLayer / snapshot already synced above); post-barrier forces the linear sample
      // shader to complete so downT1 - downT0 reflects real GPU downsample cost.
      auto* atlasCtx = atlasSurface->getContext();
      if (atlasCtx && ssaa_debug::gGpuSyncEnabled) {
        atlasCtx->flushAndSubmit(true);
      }
      auto downT0 = std::chrono::steady_clock::now();
      atlasCanvas->drawImageRect(std::move(image), tileClipRect, sourceRect, linearSampling, &paint,
                                 SrcRectConstraint::Strict);
      atlasSurface->renderContext->flush();
      if (atlasCtx && ssaa_debug::gGpuSyncEnabled) {
        atlasCtx->flushAndSubmit(true);
      }
      auto downT1 = std::chrono::steady_clock::now();
      gSsaaCounters.ssaaDownsampleUs +=
          std::chrono::duration_cast<std::chrono::microseconds>(downT1 - downT0).count();
      return;
    }
  }

  // Non-SSAA path: render directly to atlas (forceNoEdgeAA defaults to false).
  auto canvas = atlasSurface->getCanvas();
  AutoCanvasRestore autoRestore(canvas);
  auto viewMatrix = Matrix::MakeScale(currentZoomScale);
  auto offsetX = sourceRect.left - tileRect.left;
  auto offsetY = sourceRect.top - tileRect.top;
  viewMatrix.postTranslate(offsetX, offsetY);
  auto clipRect = tileRect;
  clipRect.offset(offsetX, offsetY);
  // [SSAA-DBG] Reuse the same probe for the non-SSAA path so we can compare per-tile draw call
  // counts (di/sl) between SSAA on/off with identical playback. Also record the drawRootLayer
  // wall-clock time here into ssaaTileDrawUs so the same LOGI line reports it.
  const int64_t nonSsaaTilePixels =
      static_cast<int64_t>(std::max(0.0f, tileRect.width())) *
      static_cast<int64_t>(std::max(0.0f, tileRect.height()));
  gSsaaCounters.ssaaTilePixels += nonSsaaTilePixels;
  const int tileW = FloatCeilToInt(tileRect.width());
  const int tileH = FloatCeilToInt(tileRect.height());
  if (tileW > gSsaaCounters.ssaaMaxTileW) {
    gSsaaCounters.ssaaMaxTileW = tileW;
  }
  if (tileH > gSsaaCounters.ssaaMaxTileH) {
    gSsaaCounters.ssaaMaxTileH = tileH;
  }
  auto nonSsaaT0 = std::chrono::steady_clock::now();
  ssaa_debug::gProbe = {};
  ssaa_debug::gProbe.active = true;
  drawRootLayer(atlasSurface, clipRect, viewMatrix, true, snapshots);
  // [SSAA-DBG] Round 8: close trailing op-category window (non-SSAA path). Same rationale
  // as the SSAA branch — the last op cluster before drawRootLayer returns has been paying
  // GPU cost that's still outstanding until this flush.
  if (ssaa_debug::gGpuSyncEnabled &&
      ssaa_debug::gProbe.currentOpCategory != ssaa_debug::Probe::OpCatNone) {
    auto* ctx = atlasSurface->getContext();
    if (ctx != nullptr) {
      ctx->flushAndSubmit(true);
      auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();
      auto elapsedUs = (nowNs - ssaa_debug::gProbe.opCategoryT0Ns) / 1000;
      auto cat = ssaa_debug::gProbe.currentOpCategory;
      if (cat >= 0 && cat < ssaa_debug::Probe::OpCatCount) {
        ssaa_debug::gProbe.opGpuUs[cat] += elapsedUs;
      }
      ssaa_debug::gProbe.currentOpCategory = ssaa_debug::Probe::OpCatNone;
    }
  }
  ssaa_debug::gProbe.active = false;
  ++gSsaaCounters.nonSsaaTileCount;
  gSsaaCounters.saveLayerCount += ssaa_debug::gProbe.saveLayerCount;
  gSsaaCounters.drawImageCount += ssaa_debug::gProbe.drawImageCount;
  gSsaaCounters.drawImageFilterCount += ssaa_debug::gProbe.drawImageFilterCount;
  gSsaaCounters.drawImageSrcPixels += ssaa_debug::gProbe.drawImageSrcPixels;
  gSsaaCounters.drawImageDstPixels += ssaa_debug::gProbe.drawImageDstPixels;
  gSsaaCounters.subtreeCacheEligible += ssaa_debug::gProbe.subtreeCacheEligible;
  gSsaaCounters.subtreeCacheHit += ssaa_debug::gProbe.subtreeCacheHit;
  gSsaaCounters.subtreeCacheBuilt += ssaa_debug::gProbe.subtreeCacheBuilt;
  gSsaaCounters.subtreeCacheOversized += ssaa_debug::gProbe.subtreeCacheOversized;
  gSsaaCounters.subtreeCacheDisabled += ssaa_debug::gProbe.subtreeCacheDisabled;
  gSsaaCounters.subtreeCacheMissOther += ssaa_debug::gProbe.subtreeCacheMissOther;
  gSsaaCounters.subtreeCacheBuildUs += ssaa_debug::gProbe.subtreeCacheBuildUs;
  gSsaaCounters.subtreeCacheDrawHitUs += ssaa_debug::gProbe.subtreeCacheDrawHitUs;
  gSsaaCounters.subtreeCacheBuildCount += ssaa_debug::gProbe.subtreeCacheBuildCount;
  gSsaaCounters.subtreeCacheDrawHitCount += ssaa_debug::gProbe.subtreeCacheDrawHitCount;
  // [SSAA-DBG] Round 2 aggregation (non-SSAA path).
  gSsaaCounters.oversizedBucket_le2048 += ssaa_debug::gProbe.oversizedBucket_le2048;
  gSsaaCounters.oversizedBucket_2k_3k += ssaa_debug::gProbe.oversizedBucket_2k_3k;
  gSsaaCounters.oversizedBucket_3k_4k += ssaa_debug::gProbe.oversizedBucket_3k_4k;
  gSsaaCounters.oversizedBucket_4k_6k += ssaa_debug::gProbe.oversizedBucket_4k_6k;
  gSsaaCounters.oversizedBucket_6k_plus += ssaa_debug::gProbe.oversizedBucket_6k_plus;
  if (ssaa_debug::gProbe.oversizedMinLongEdge > 0 &&
      (gSsaaCounters.oversizedMinLongEdge == 0 ||
       ssaa_debug::gProbe.oversizedMinLongEdge < gSsaaCounters.oversizedMinLongEdge)) {
    gSsaaCounters.oversizedMinLongEdge = ssaa_debug::gProbe.oversizedMinLongEdge;
  }
  if (ssaa_debug::gProbe.oversizedMaxLongEdge > gSsaaCounters.oversizedMaxLongEdge) {
    gSsaaCounters.oversizedMaxLongEdge = ssaa_debug::gProbe.oversizedMaxLongEdge;
  }
  gSsaaCounters.oversizedFallbackDrawUs += ssaa_debug::gProbe.oversizedFallbackDrawUs;
  gSsaaCounters.oversizedFallbackDrawCount += ssaa_debug::gProbe.oversizedFallbackDrawCount;
  gSsaaCounters.otherFallbackDrawUs += ssaa_debug::gProbe.otherFallbackDrawUs;
  gSsaaCounters.otherFallbackDrawCount += ssaa_debug::gProbe.otherFallbackDrawCount;
  // [SSAA-DBG] Round 5: inRec vs direct split (non-SSAA path).
  gSsaaCounters.otherFallbackInRecUs += ssaa_debug::gProbe.otherFallbackInRecUs;
  gSsaaCounters.otherFallbackInRecCount += ssaa_debug::gProbe.otherFallbackInRecCount;
  gSsaaCounters.otherFallbackDirectUs += ssaa_debug::gProbe.otherFallbackDirectUs;
  gSsaaCounters.otherFallbackDirectCount += ssaa_debug::gProbe.otherFallbackDirectCount;
  // [SSAA-DBG] Round 6: SubtreeCache key/entry probes (non-SSAA path).
  gSsaaCounters.hasCacheKeyMiss     += ssaa_debug::gProbe.hasCacheKeyMiss;
  gSsaaCounters.hasCacheProxyMiss   += ssaa_debug::gProbe.hasCacheProxyMiss;
  gSsaaCounters.builtSecondEntry    += ssaa_debug::gProbe.builtSecondEntry;
  gSsaaCounters.builtEntriesSizeSum += ssaa_debug::gProbe.builtEntriesSizeSum;
  // [SSAA-DBG] Round 7: invalidation churn probes (non-SSAA path).
  gSsaaCounters.subtreeInvalidated  += ssaa_debug::gProbe.subtreeInvalidated;
  gSsaaCounters.builtFromEmpty      += ssaa_debug::gProbe.builtFromEmpty;
  // [SSAA-DBG] Round 8: per-category GPU us (non-SSAA path).
  for (int i = 0; i < ssaa_debug::Probe::OpCatCount; ++i) {
    gSsaaCounters.opGpuUs[i] += ssaa_debug::gProbe.opGpuUs[i];
  }
  gSsaaCounters.opGpuFlushCount += ssaa_debug::gProbe.opGpuFlushCount;
  // [SSAA-DBG] Round 4 aggregation (non-SSAA path).
  gSsaaCounters.builtEdge_le64      += ssaa_debug::gProbe.builtEdge_le64;
  gSsaaCounters.builtEdge_64_128    += ssaa_debug::gProbe.builtEdge_64_128;
  gSsaaCounters.builtEdge_128_256   += ssaa_debug::gProbe.builtEdge_128_256;
  gSsaaCounters.builtEdge_256_512   += ssaa_debug::gProbe.builtEdge_256_512;
  gSsaaCounters.builtEdge_512_1024  += ssaa_debug::gProbe.builtEdge_512_1024;
  gSsaaCounters.builtEdge_1024_2048 += ssaa_debug::gProbe.builtEdge_1024_2048;
  gSsaaCounters.builtEdge_2048_plus += ssaa_debug::gProbe.builtEdge_2048_plus;
  if (ssaa_debug::gProbe.builtEdgeMin > 0 &&
      (gSsaaCounters.builtEdgeMin == 0 ||
       ssaa_debug::gProbe.builtEdgeMin < gSsaaCounters.builtEdgeMin)) {
    gSsaaCounters.builtEdgeMin = ssaa_debug::gProbe.builtEdgeMin;
  }
  if (ssaa_debug::gProbe.builtEdgeMax > gSsaaCounters.builtEdgeMax) {
    gSsaaCounters.builtEdgeMax = ssaa_debug::gProbe.builtEdgeMax;
  }
  // [SSAA-DBG] Op-type + shader-type aggregation (non-SSAA path).
  gSsaaCounters.opRect          += ssaa_debug::gProbe.opRect;
  gSsaaCounters.opRRect         += ssaa_debug::gProbe.opRRect;
  gSsaaCounters.opPath          += ssaa_debug::gProbe.opPath;
  gSsaaCounters.opShape         += ssaa_debug::gProbe.opShape;
  gSsaaCounters.opText          += ssaa_debug::gProbe.opText;
  gSsaaCounters.opMesh          += ssaa_debug::gProbe.opMesh;
  gSsaaCounters.opAtlas         += ssaa_debug::gProbe.opAtlas;
  gSsaaCounters.opFill          += ssaa_debug::gProbe.opFill;
  gSsaaCounters.opLayer         += ssaa_debug::gProbe.opLayer;
  gSsaaCounters.opImage         += ssaa_debug::gProbe.opImage;
  gSsaaCounters.shaderNone      += ssaa_debug::gProbe.shaderNone;
  gSsaaCounters.shaderColor     += ssaa_debug::gProbe.shaderColor;
  gSsaaCounters.shaderImage     += ssaa_debug::gProbe.shaderImage;
  gSsaaCounters.shaderGradient  += ssaa_debug::gProbe.shaderGradient;
  gSsaaCounters.shaderColorFilter += ssaa_debug::gProbe.shaderColorFilter;
  gSsaaCounters.shaderMatrix    += ssaa_debug::gProbe.shaderMatrix;
  gSsaaCounters.shaderBlend     += ssaa_debug::gProbe.shaderBlend;
  gSsaaCounters.shaderPerlinNoise += ssaa_debug::gProbe.shaderPerlinNoise;
  gSsaaCounters.shaderOther     += ssaa_debug::gProbe.shaderOther;
  auto nonSsaaT1 = std::chrono::steady_clock::now();
  gSsaaCounters.ssaaTileDrawUs +=
      std::chrono::duration_cast<std::chrono::microseconds>(nonSsaaT1 - nonSsaaT0).count();
}

Surface* DisplayList::getOrCreateSSAATileSurface(const Surface* renderSurface, int requiredWidth,
                                                 int requiredHeight) {
  if (ssaaTileSurface != nullptr && ssaaTileSurface->getContext() == renderSurface->getContext() &&
      ssaaTileSurface->width() == requiredWidth && ssaaTileSurface->height() == requiredHeight &&
      ColorSpace::Equals(ssaaTileSurface->colorSpace().get(), renderSurface->colorSpace().get())) {
    return ssaaTileSurface.get();
  }
  ssaaTileSurface = Surface::Make(renderSurface->getContext(), requiredWidth, requiredHeight,
                                  ColorType::RGBA_8888, 1, false, renderSurface->renderFlags(),
                                  renderSurface->colorSpace());
  if (ssaaTileSurface == nullptr) {
    LOGE("DisplayList::getOrCreateSSAATileSurface() Failed to create SSAA tile surface!");
  }
  return ssaaTileSurface.get();
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
    if (task.identityScale()) {
      ++gSsaaCounters.screenNearest;
    } else {
      ++gSsaaCounters.screenLinear;
    }
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

void DisplayList::drawThrottleScreenTasks(std::vector<DrawTask> throttleScreenTasks,
                                          Surface* surface, bool autoClear) const {
  if (throttleScreenTasks.empty()) {
    return;
  }
  auto canvas = surface->getCanvas();
  AutoCanvasRestore autoRestore(canvas);
  canvas->setMatrix(Matrix::MakeTrans(_contentOffset.x, _contentOffset.y));
  Paint paint = {};
  paint.setAntiAlias(false);
  paint.setBlendMode(autoClear ? BlendMode::Src : BlendMode::SrcOver);
  static SamplingOptions linearSampling(FilterMode::Linear, MipmapMode::None);
  for (auto& task : throttleScreenTasks) {
    auto surfaceCache = surfaceCaches[task.sourceIndex()];
    DEBUG_ASSERT(surfaceCache != nullptr);
    auto image = surfaceCache->makeImageSnapshot();
    ++gSsaaCounters.throttleScreenLinear;
    canvas->drawImageRect(std::move(image), task.sourceRect(), task.tileRect(), linearSampling,
                          &paint, SrcRectConstraint::Strict, &task.strictRect());
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
  ssaaTileSurface = nullptr;
  totalTileCount = 0;
  emptyTiles.clear();
}

void DisplayList::drawRootLayer(Surface* surface, const Rect& drawRect, const Matrix& viewMatrix,
                                bool autoClear, BackgroundSnapshotMap* snapshots,
                                bool forceNoEdgeAA,
                                float subtreeContentScaleDivisor) const {
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
  args.forceNoEdgeAA = forceNoEdgeAA;
  // [SSAA-DBG] SSAA tile path also forces image sampling to nearest — reuses forceNoEdgeAA as
  // the SSAA-only signal (drawRootLayer is the only caller that flips it to true).
  args.forceImageSamplingNearest = forceNoEdgeAA;
  args.subtreeContentScaleDivisor = subtreeContentScaleDivisor;
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
  BackgroundConsumer consumer(snapshots);
  args.backgroundHandler = snapshots ? &consumer : BackgroundHandler::NoOp();
  _root->drawLayer(args, surface->getCanvas(), 1.0f, BlendMode::SrcOver);
}

std::unique_ptr<BackgroundSnapshotMap> DisplayList::captureBackgrounds(
    Surface* surface, const std::vector<Rect>& renderRects, bool forceNoEdgeAA) const {
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
  auto viewMatrix = getViewMatrix();
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
  // Expand by the max blur outset so layers whose bounds sit just outside the dirty rects but
  // still contribute pixels to the blur sampling region are not culled by the capture pass.
  for (auto& rect : worldRects) {
    rect.outset(_root->maxBackgroundOutset, _root->maxBackgroundOutset);
  }
  DrawArgs args(context);
  args.forceNoEdgeAA = forceNoEdgeAA;
  args.forceImageSamplingNearest = forceNoEdgeAA;
  args.dstColorSpace = surface->colorSpace();
  args.subtreeCacheMaxSize = _subtreeCacheMaxSize;
  auto bgSource =
      _root->createBackgroundSource(context, surfaceUnion, viewMatrix, false, args.dstColorSpace);
  if (bgSource == nullptr) {
    return nullptr;
  }
  auto snapshotMap = std::make_unique<BackgroundSnapshotMap>();
  // Draw backgroundColor before the layer tree so that the capture pass includes it as part of
  // the background for blur/backdrop effects.
  if (_backgroundColor != Color::Transparent()) {
    bgSource->getCanvas()->drawColor(_backgroundColor, BlendMode::SrcOver);
  }
  BackgroundCapturer::Run(_root.get(), args, std::move(bgSource), snapshotMap.get(), worldRects);
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

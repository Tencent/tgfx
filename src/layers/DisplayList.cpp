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
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include "core/utils/DecomposeRects.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/TileProbe.h"
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
// Levels looked up beyond the L to L+3 window when a cell is composed from several levels.
static constexpr int MOSAIC_FINER_LEVELS = 2;
static constexpr int MOSAIC_COARSER_LEVELS = 2;
// A level that would need more tile lookups than this for the viewport is left out.
static constexpr int MOSAIC_MAX_LEVEL_CELLS = 2048;
// Tiles kept for display after their content changed, until they are redrawn.
static constexpr size_t MAX_STALE_TILES = 96;
static constexpr size_t BLANK_MARKS_PER_TILE = 4;
// ClusterTileRects growth allowed when merging footprints, and the number of rects the per-layer
// cull loop can afford.
static constexpr double FOOTPRINT_CLUSTER_GROWTH = 4.0;
static constexpr size_t MAX_CULL_RECTS = 32;
// Frames in a row that may leave every optional tile undrawn before one is forced through.
static constexpr int BUDGET_STARVE_FRAMES = 4;
// Estimated cost of one background style footprint in a capture and in the tiles that consume it,
// measured on the L+3 clusters of a zoomed-out document. The measured cost of the last base tile of
// the same level is the other estimate, which also covers tiles whose cost is plain content.
static constexpr double BUDGET_MS_PER_FOOTPRINT = 0.3;
static constexpr double BUDGET_RT_PER_FOOTPRINT = 14.0;

/**
 * Why a tile is drawn. Must tiles repair dirty regions and are always drawn. Base tiles are the
 * coarsest tree level that covers cells nothing else covers; each is drawn when its estimated cost
 * fits the frame budget, and the cells stay the background color otherwise. The rest sharpen what
 * is already on screen and are dropped once the frame budget is spent.
 */
enum class TaskClass { Must, Base, Grid, Refine, Chain };

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

  TaskClass taskClass() const {
    return _taskClass;
  }

  void setTaskClass(TaskClass taskClass) {
    _taskClass = taskClass;
  }

  bool optional() const {
    return _taskClass != TaskClass::Must;
  }

  /**
   * Returns the order a screen task is composited in: tasks with a lower layer are drawn first, so
   * a finer tile can cover the part of a coarser one it overlaps.
   */
  int layer() const {
    return _layer;
  }

  void setLayer(int layer) {
    _layer = layer;
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
  TaskClass _taskClass = TaskClass::Must;
  int _layer = 0;

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

/**
 * One background capture shared by a cluster of tiles at a single raster scale. The region is
 * planned before any tile is drawn; the footprints and the capture itself run when the first tile of
 * the cluster is about to be drawn.
 */
struct CapturePlan {
  int level = kExactLevel;
  Matrix viewMatrix = {};
  std::vector<Rect> clusterRects = {};
  bool prepared = false;
  bool captured = false;
  size_t footprintCount = 0;
  std::vector<Rect> cullRects = {};
  std::unique_ptr<BackgroundSnapshotMap> map = nullptr;
  TILE_PROBE(int probeFootprints = 0; Rect probeFootprintBox = {}; double probeFootprintMs = 0;)
};

/**
 * Cost spent by the current tiled frame, counted from the start of the frame.
 */
struct TileBudgetClock {
  using Clock = std::chrono::steady_clock;

  DrawingManager* manager = nullptr;
  Clock::time_point start = {};
  uint32_t startSwitches = 0;
  float timeMs = 0;
  int rtSwitches = 0;
  bool enabled = false;

  double elapsedMs() const {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
  }

  uint32_t usedSwitches() const {
    return manager->renderTargetSwitchCount() - startSwitches;
  }

  // Returns "time" or "rt" once a limit is exceeded, otherwise nullptr.
  const char* exceeded() const {
    if (!enabled) {
      return nullptr;
    }
    if (timeMs > 0 && elapsedMs() > static_cast<double>(timeMs)) {
      return "time";
    }
    if (rtSwitches > 0 && usedSwitches() > static_cast<uint32_t>(rtSwitches)) {
      return "rt";
    }
    return nullptr;
  }
};

// Temporary tile probe. Prints one [TileProbe] line for every tiled frame that rasterizes tiles,
// runs slow or leaves uncovered cells, and one [TileProbeSlow] line for every expensive tile.
// Switched by TILE_PROBE_ENABLED in core/utils/TileProbe.h.
#if TILE_PROBE_ENABLED

using ProbeClock = std::chrono::steady_clock;

static constexpr double PROBE_SLOW_TILE_MS = 2.0;
static constexpr uint64_t PROBE_SLOW_TILE_CALLS = 2000;
static constexpr double PROBE_SLOW_FRAME_MS = 8.0;
static constexpr double PROBE_SLOW_CAPTURE_MS = 1.0;
static constexpr size_t PROBE_OVERLAP_LIMIT = 400;
// Tree depths L..L+3, plus one slot for any other depth.
static constexpr int PROBE_TREE_SLOTS = TREE_LEVEL_COUNT + 1;

static double ProbeMs(ProbeClock::time_point from, ProbeClock::time_point to) {
  return std::chrono::duration<double, std::milli>(to - from).count();
}

struct ProbeKind {
  int newTiles = 0;
  int patches = 0;
  uint64_t calls = 0;
  double ms = 0;
  uint32_t blurStyles = 0;
  uint32_t blurCalls = 0;
  uint32_t blurSwitches = 0;
  uint32_t tileSwitches = 0;
};

struct ProbeFrame {
  bool active = false;
  uint64_t index = 0;
  int level = 0;
  bool viewportChanged = false;
  bool continuous = false;
  int cells = 0;
  int hit = 0;
  int blank = 0;
  int missing = 0;
  int fallbackCells = 0;
  int paintGrids = 0;
  int uncovered = 0;
  int mosaic = 0;
  int uncLost = 0;
  int uncCoarse = 0;
  int uncFiner = 0;
  int uncNone = 0;
  int uncLostNone = 0;
  int uncLostGrid = 0;
  int uncLostBlank = 0;
  int uncLostTree[TREE_LEVEL_COUNT] = {};
  int fallbackDepth[TREE_LEVEL_COUNT] = {};
  ProbeKind treeDepth[PROBE_TREE_SLOTS] = {};
  int screenTasks = 0;
  int captureCalls = 0;
  int captureRan = 0;
  ProbeKind tree = {};
  ProbeKind grid = {};
  double dirtyMs = 0;
  double invalidateMs = 0;
  double collectMs = 0;
  double captureMs = 0;
  double tilesMs = 0;
  double screenMs = 0;
  double flagsMs = 0;
  double runMs = 0;
};

static ProbeFrame tileProbe = {};
static uint64_t tileProbeCounter = 0;

static void ProbeFallback(size_t depth) {
  tileProbe.fallbackCells++;
  if (depth < static_cast<size_t>(TREE_LEVEL_COUNT)) {
    tileProbe.fallbackDepth[depth]++;
  }
}

// Buckets every background blur style of the region by its radius in tile pixels, including the
// ones the current radius already skips, so a different radius can be read off the same frame.
static void ProbeBlurSigma(const std::vector<float>& radii) {
  auto& info = FootprintInfo();
  info.sigmaCount = static_cast<uint32_t>(radii.size());
  for (auto radius : radii) {
    if (radius < 0.3f) {
      info.sigma[0]++;
    } else if (radius < 0.35f) {
      info.sigma[1]++;
    } else if (radius < 0.4f) {
      info.sigma[2]++;
    } else if (radius < 0.45f) {
      info.sigma[3]++;
    } else if (radius < 0.5f) {
      info.sigma[4]++;
    } else {
      info.sigma[5]++;
    }
  }
}

// Every entry is the style's content bounds, its footprint, and the longer side of the content
// bounds in the pixels of the tile it is drawn into.
static void ProbeFootprintShape(const std::vector<std::tuple<Rect, Rect, float>>& styles,
                                float deviceScale) {
  auto& info = FootprintInfo();
  info = {};
  info.count = static_cast<uint32_t>(styles.size());
  if (styles.empty()) {
    return;
  }
  info.minOutsetWorld = std::numeric_limits<float>::max();
  std::vector<bool> overlapped(styles.size(), false);
  // The pairwise test is quadratic, so only the first PROBE_OVERLAP_LIMIT styles are compared.
  auto limit = std::min(styles.size(), PROBE_OVERLAP_LIMIT);
  info.kept = static_cast<uint32_t>(styles.size());
  info.keptBox = Rect::MakeEmpty();
  for (size_t i = 0; i < styles.size(); ++i) {
    const auto& bounds = std::get<0>(styles[i]);
    const auto& footprint = std::get<1>(styles[i]);
    auto contentMaxSide = std::get<2>(styles[i]);
    if (contentMaxSide < 0.25f) {
      info.subPixel[0]++;
    } else if (contentMaxSide < 0.5f) {
      info.subPixel[1]++;
    } else if (contentMaxSide < 1.0f) {
      info.subPixel[2]++;
    } else if (contentMaxSide < 2.0f) {
      info.subPixel[3]++;
    }
    if (contentMaxSide < 1.0f) {
      info.kept--;
    } else {
      info.keptBox.join(footprint);
    }
    auto outset = (footprint.width() - bounds.width()) * 0.5f * deviceScale;
    info.maxOutsetWorld = std::max(info.maxOutsetWorld, outset);
    info.minOutsetWorld = std::min(info.minOutsetWorld, outset);
    info.sumOutsetWorld += outset;
    for (size_t j = i + 1; j < limit && i < limit; ++j) {
      auto& a = std::get<0>(styles[i]);
      auto& b = std::get<0>(styles[j]);
      if (!Rect::Intersects(a, b)) {
        continue;
      }
      info.overlapPairs++;
      overlapped[i] = true;
      overlapped[j] = true;
      if (a.contains(b) || b.contains(a)) {
        info.nested++;
      }
    }
  }
  for (bool flag : overlapped) {
    if (flag) {
      info.overlapped++;
    }
  }
  // styles is in paint order (depth first, parents before children, earlier siblings first), so a
  // style is dirty when anything painted before it has bounds inside the area it samples.
  info.checked = static_cast<uint32_t>(limit);
  for (size_t j = 0; j < limit; ++j) {
    auto& footprint = std::get<1>(styles[j]);
    auto& bounds = std::get<0>(styles[j]);
    bool dirty = false;
    bool byNested = false;
    for (size_t i = 0; i < j; ++i) {
      if (!Rect::Intersects(std::get<0>(styles[i]), footprint)) {
        continue;
      }
      dirty = true;
      if (std::get<0>(styles[i]).contains(bounds)) {
        byNested = true;
        break;
      }
    }
    if (dirty) {
      info.dirty++;
      if (byNested) {
        info.dirtyByNested++;
      }
    }
  }
}

static void ProbeCapture(int level, int tileSize, const std::vector<Rect>& clusterRects,
                         const BackgroundSnapshotMap* map, size_t cullCount, int footprintCount,
                         const Rect& footprintBox, double footprintMs, double captureMs,
                         uint64_t calls, const DrawingManager::TaskProbe& tasksBefore,
                         const DrawingManager::TaskProbe& tasksAfter) {
  tileProbe.captureCalls++;
  auto ran = map != nullptr;
  if (ran) {
    tileProbe.captureRan++;
  }
  if (!ran && captureMs < PROBE_SLOW_CAPTURE_MS) {
    return;
  }
  auto box = Rect::MakeEmpty();
  for (const auto& rect : clusterRects) {
    box.join(rect);
  }
  auto size = static_cast<float>(tileSize);
  auto isTree = level != kExactLevel;
  auto depth = isTree ? level - tileProbe.level : -1;
  const auto& snap = CaptureProbe();
  auto totOps = tasksAfter.ops - tasksBefore.ops;
  auto totRuntime = tasksAfter.runtime - tasksBefore.runtime;
  auto totCopies = tasksAfter.copies - tasksBefore.copies;
  auto totSwitches = tasksAfter.switches - tasksBefore.switches;
  auto bodyCalls = calls >= snap.drawCalls ? calls - snap.drawCalls : 0;
  auto bodyOps = totOps >= snap.ops ? totOps - snap.ops : 0;
  auto bodySwitches = totSwitches >= snap.switches ? totSwitches - snap.switches : 0;
  const auto& shape = FootprintInfo();
  auto outsetPx = shape.maxOutsetWorld;
  auto boxW = box.width();
  auto boxH = box.height();
  auto areaRatio = boxW > 0 && boxH > 0
                       ? (boxW + 2 * outsetPx) * (boxH + 2 * outsetPx) / (boxW * boxH)
                       : 0.0f;
  LOGI("[TileCapture] f=%llu type=%s level=%d depth=%d tiles=%d box=%d..%d,%d..%d rect=%.0fx%.0f "
       "fp=%d fpbox=(%.0f,%.0f,%.0f,%.0f) cull=%d snaps=%d ran=%d ms=%.2f fpms=%.2f calls=%llu "
       "| tot ops=%u rt=%u cp=%u sw=%u | snap n=%u calls=%u ops=%u cp=%u sw=%u | body calls=%llu "
       "ops=%u sw=%u | ov styles=%u pairs=%u ovl=%u nest=%u | outset px max=%.1f min=%.1f avg=%.1f "
       "area=%.2fx | dirty n=%u checked=%u bynest=%u | subpx <0.25=%u <0.5=%u <1=%u <2=%u "
       "keep=%u/%u keptbox=%.0fx%.0f | sigma n=%u <0.3=%u <0.35=%u <0.4=%u <0.45=%u <0.5=%u "
       ">=0.5=%u",
       static_cast<unsigned long long>(tileProbe.index), isTree ? "tree" : "grid",
       isTree ? level : 0, depth, static_cast<int>(clusterRects.size()),
       FloatFloorToInt(box.left / size), FloatCeilToInt(box.right / size) - 1,
       FloatFloorToInt(box.top / size), FloatCeilToInt(box.bottom / size) - 1, box.width(),
       box.height(), footprintCount, footprintBox.left, footprintBox.top, footprintBox.right,
       footprintBox.bottom, static_cast<int>(cullCount),
       ran ? static_cast<int>(map->snapshots.size()) : 0, ran ? 1 : 0, captureMs, footprintMs,
       static_cast<unsigned long long>(calls), totOps, totRuntime, totCopies, totSwitches,
       snap.styles, snap.drawCalls, snap.ops, snap.copies, snap.switches,
       static_cast<unsigned long long>(bodyCalls), bodyOps, bodySwitches, shape.count,
       shape.overlapPairs, shape.overlapped, shape.nested, static_cast<double>(outsetPx),
       static_cast<double>(shape.minOutsetWorld),
       shape.count > 0
           ? static_cast<double>(shape.sumOutsetWorld / static_cast<float>(shape.count))
           : 0.0,
       static_cast<double>(areaRatio), shape.dirty, shape.checked, shape.dirtyByNested,
       shape.subPixel[0], shape.subPixel[1], shape.subPixel[2], shape.subPixel[3], shape.kept,
       shape.count, static_cast<double>(shape.keptBox.width()),
       static_cast<double>(shape.keptBox.height()), shape.sigmaCount, shape.sigma[0],
       shape.sigma[1], shape.sigma[2], shape.sigma[3], shape.sigma[4], shape.sigma[5]);
}

static void ProbeRecordTile(const DrawTask& task, double ms, uint64_t calls, bool hasContent,
                            const ConsumeStyleProbe& blur, uint32_t tileSwitches,
                            const TileContentProbe& content) {
  auto isTree = task.treeLevel() != kExactLevel;
  auto tileCount = static_cast<int>(task.getTiles().size());
  auto accumulate = [&](ProbeKind& kind) {
    if (task.coversTiles()) {
      kind.newTiles += tileCount;
    } else {
      kind.patches++;
    }
    kind.calls += calls;
    kind.ms += ms;
    kind.blurStyles += blur.styles;
    kind.blurCalls += blur.drawCalls;
    kind.blurSwitches += blur.switches;
    kind.tileSwitches += tileSwitches;
  };
  int depth = -1;
  if (isTree) {
    depth = task.treeLevel() - tileProbe.level;
    auto slot = depth >= 0 && depth < TREE_LEVEL_COUNT ? depth : TREE_LEVEL_COUNT;
    accumulate(tileProbe.tree);
    accumulate(tileProbe.treeDepth[slot]);
  } else {
    accumulate(tileProbe.grid);
  }
  if (ms < PROBE_SLOW_TILE_MS && calls < PROBE_SLOW_TILE_CALLS) {
    return;
  }
  auto& rect = task.tileRect();
  auto& first = task.getTiles().front();
  LOGI("[TileProbeSlow] f=%llu type=%s level=%d depth=%d xy=(%d,%d) tiles=%d cover=%d "
       "rect=%.0fx%.0f ms=%.2f calls=%llu content=%d | blur styles=%u calls=%u sw=%u | sw=%u "
       "| layers=%u culled=%u cache=%u/%u off=%u blend=%u alpha=%u filter=%u mask=%u pt=%u "
       "masks=%u | group=%u leaf=%u empty=%u pure=%u | child %u/%u/%u/%u/%u "
       "| depth %u/%u/%u/%u/%u/%u/%u/%u max=%u | us mask=%.2f cache=%.2f src=%.2f content=%.2f "
       "n=%u",
       static_cast<unsigned long long>(tileProbe.index), isTree ? "tree" : "grid",
       isTree ? task.treeLevel() : 0, depth, first->tileX, first->tileY, tileCount,
       task.coversTiles() ? 1 : 0, rect.width(), rect.height(), ms,
       static_cast<unsigned long long>(calls), hasContent ? 1 : 0, blur.styles, blur.drawCalls,
       blur.switches, tileSwitches, content.layers, content.culled, content.cacheHits,
       content.cacheEligible, content.offscreen, content.offBlend, content.offGroupAlpha,
       content.offFilter, content.offMask, content.offPassThrough, content.masks, content.groups,
       content.leaves, content.emptyLeaves, content.pureGroups, content.childHist[0],
       content.childHist[1], content.childHist[2], content.childHist[3], content.childHist[4],
       content.depthHist[0], content.depthHist[1], content.depthHist[2], content.depthHist[3],
       content.depthHist[4], content.depthHist[5], content.depthHist[6], content.depthHist[7],
       content.maxDepth,
       content.samples > 0 ? content.maskUs / content.samples : 0.0,
       content.samples > 0 ? content.cacheUs / content.samples : 0.0,
       content.samples > 0 ? content.sourceUs / content.samples : 0.0,
       content.samples > 0 ? content.contentUs / content.samples : 0.0, content.samples);
}

static const char* const PROBE_DEPTH_LABELS[PROBE_TREE_SLOTS] = {"L", "L+1", "L+2", "L+3", "L?"};

static void ProbeReport(float zoomScale, double totalMs, int refineBudget) {
  auto& p = tileProbe;
  if (!p.active) {
    return;
  }
  auto worked = p.tree.newTiles + p.tree.patches + p.grid.newTiles + p.grid.patches;
  if (worked == 0 && totalMs < PROBE_SLOW_FRAME_MS && p.uncovered == 0) {
    return;
  }
  std::string levels = {};
  std::string depths = {};
  char buffer[96] = {};
  int refined = 0;
  for (int slot = 0; slot < PROBE_TREE_SLOTS; ++slot) {
    auto& kind = p.treeDepth[slot];
    if (slot < TREE_LEVEL_COUNT - 1) {
      refined += kind.newTiles;
    }
    if (kind.newTiles > 0) {
      if (!levels.empty()) {
        levels += ",";
      }
      levels += std::string(PROBE_DEPTH_LABELS[slot]) + ":" + std::to_string(kind.newTiles);
    }
    if (kind.newTiles > 0 || kind.patches > 0) {
      std::snprintf(buffer, sizeof(buffer), "%s:n%d/p%d/%.1fms/%lluc", PROBE_DEPTH_LABELS[slot],
                    kind.newTiles, kind.patches, kind.ms,
                    static_cast<unsigned long long>(kind.calls));
      if (!depths.empty()) {
        depths += " ";
      }
      depths += buffer;
    }
  }
  if (levels.empty()) {
    levels = "-";
  }
  auto must = p.treeDepth[TREE_LEVEL_COUNT - 1].newTiles;
  auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                   ProbeClock::now().time_since_epoch())
                   .count() %
               1000000;
  LOGI("[TileProbe] f=%llu t=%lld zoom=%.4f L=%d chg=%d cont=%d | cells=%d hit=%d blank=%d "
       "miss=%d fb=%d(L:%d,+1:%d,+2:%d,+3:%d) paint=%d unc=%d | tree new=%d(%s) patch=%d "
       "grid new=%d patch=%d scr=%d | ms dirty=%.1f inval=%.1f collect=%.1f capture=%.1f "
       "tiles=%.1f tree=%.1f grid=%.1f screen=%.1f flags=%.1f total=%.1f | calls tree=%llu "
       "grid=%llu | depth {%s} must=%d refine=%d/%d | cap n=%d ran=%d | blur tree n=%u c=%u "
       "sw=%u/%u grid n=%u c=%u sw=%u/%u | uncx lost=%d coarse=%d finer=%d none=%d lostNone=%d | lostSrc grid=%d blank=%d tree=%d,%d,%d,%d | mosaic=%d",
       static_cast<unsigned long long>(p.index), static_cast<long long>(nowMs),
       static_cast<double>(zoomScale), p.level, p.viewportChanged ? 1 : 0, p.continuous ? 1 : 0,
       p.cells, p.hit, p.blank, p.missing, p.fallbackCells, p.fallbackDepth[0],
       p.fallbackDepth[1], p.fallbackDepth[2], p.fallbackDepth[3], p.paintGrids, p.uncovered,
       p.tree.newTiles, levels.c_str(), p.tree.patches, p.grid.newTiles, p.grid.patches,
       p.screenTasks, p.dirtyMs, p.invalidateMs, p.collectMs, p.captureMs, p.tilesMs, p.tree.ms,
       p.grid.ms, p.screenMs, p.flagsMs, totalMs, static_cast<unsigned long long>(p.tree.calls),
       static_cast<unsigned long long>(p.grid.calls), depths.c_str(), must, refined, refineBudget,
       p.captureCalls, p.captureRan, p.tree.blurStyles, p.tree.blurCalls, p.tree.blurSwitches,
       p.tree.tileSwitches, p.grid.blurStyles, p.grid.blurCalls, p.grid.blurSwitches,
       p.grid.tileSwitches, p.uncLost, p.uncCoarse, p.uncFiner, p.uncNone, p.uncLostNone, p.uncLostGrid,
       p.uncLostBlank, p.uncLostTree[0], p.uncLostTree[1], p.uncLostTree[2], p.uncLostTree[3], p.mosaic);
}
#endif

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

// Looks up every tile of one tree level inside the viewport. Hits are touched when asked, which is
// what keeps the level from being evicted.
static TreeLevelCoverage MakeTreeCoverage(TilePool* tilePool, const BlankTileSet* blankTiles,
                                          float zoomScale, const Rect& renderRect, int tileSize,
                                          int level, bool touch, int maxCells = 0) {
  auto tileSizeFloat = static_cast<float>(tileSize);
  TreeLevelCoverage coverage = {};
  coverage.level = level;
  coverage.zoomToRaster = GetTileRasterScale(coverage.level) / zoomScale;
  auto rasterRect = coverage.toRaster(renderRect);
  coverage.startX = FloatFloorToInt(rasterRect.left / tileSizeFloat);
  coverage.startY = FloatFloorToInt(rasterRect.top / tileSizeFloat);
  coverage.countX = FloatCeilToInt(rasterRect.right / tileSizeFloat) - coverage.startX;
  coverage.countY = FloatCeilToInt(rasterRect.bottom / tileSizeFloat) - coverage.startY;
  if (maxCells > 0 && coverage.countX * coverage.countY > maxCells) {
    return coverage;
  }
  auto cellCount = static_cast<size_t>(coverage.countX * coverage.countY);
  coverage.tiles.resize(cellCount);
  coverage.blanks.resize(cellCount, false);
  for (auto tileY = coverage.startY; tileY < coverage.startY + coverage.countY; ++tileY) {
    for (auto tileX = coverage.startX; tileX < coverage.startX + coverage.countX; ++tileX) {
      auto key = TileKey::MakeTree(tileX, tileY, coverage.level);
      auto tile = tilePool->find(key);
      if (tile != nullptr) {
        if (touch) {
          tilePool->touch(key);
        }
        coverage.tileAt(tileX, tileY) = std::move(tile);
      } else if (blankTiles->contains(key)) {
        coverage.blanks[coverage.indexOf(tileX, tileY)] = true;
      }
    }
  }
  return coverage;
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
  for (int i = 0; i < TREE_LEVEL_COUNT; ++i) {
    coverages.push_back(MakeTreeCoverage(tilePool, blankTiles, zoomScale, renderRect, tileSize,
                                         baseLevel + i, true));
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

#if TILE_PROBE_ENABLED
// Tree levels beyond the L to L+3 window, looked up without touching, to tell whether a cell that
// nothing in the window covers could still be shown from a coarser or a finer level.
// What one viewport cell showed in the previous frame: its own grid tile, a known blank tile, or
// the tree level at the given depth (L = 0 .. L+3 = 3).
enum class ProbeSource { Grid, Blank, Tree };

struct ProbePrev {
  Rect rect = {};
  ProbeSource source = ProbeSource::Grid;
  int depth = 0;
};

struct ProbeExtension {
  std::vector<TreeLevelCoverage> coarser = {};
  std::vector<TreeLevelCoverage> finer = {};
  bool built = false;
};

static bool ProbeCovers(const std::vector<TreeLevelCoverage>& coverages, const Rect& zoomRect,
                        int tileSize) {
  for (auto& coverage : coverages) {
    bool covered = true;
    ForEachTileUnderRect(coverage.toRaster(zoomRect), tileSize, [&](int tileX, int tileY) {
      covered = coverage.isCovered(tileX, tileY);
      return covered;
    });
    if (covered) {
      return true;
    }
  }
  return false;
}

static Rect ProbeWorldRect(const Rect& zoomRect, float zoomScale) {
  auto rect = zoomRect;
  rect.scale(1.0f / zoomScale, 1.0f / zoomScale);
  return rect;
}

// Classifies a cell the frame leaves on the background color: was it covered by the previous frame
// (a visible tile turned into background), and could a level outside the window have covered it.
static void ProbeUncovered(TilePool* tilePool, const BlankTileSet* blankTiles,
                           ProbeExtension* extension, const std::vector<ProbePrev>& previousCovered,
                           const Rect& cell, float zoomScale, const Rect& renderRect,
                           int tileSize) {
  if (!extension->built) {
    extension->built = true;
    auto baseLevel = GetTileLevel(zoomScale);
    for (auto offset : {4, 5, 6}) {
      extension->coarser.push_back(MakeTreeCoverage(tilePool, blankTiles, zoomScale, renderRect,
                                                    tileSize, baseLevel + offset, false));
    }
    for (auto offset : {-1, -2, -3}) {
      extension->finer.push_back(MakeTreeCoverage(tilePool, blankTiles, zoomScale, renderRect,
                                                  tileSize, baseLevel + offset, false));
    }
  }
  auto world = ProbeWorldRect(cell, zoomScale);
  const float fractions[5][2] = {{0.5f, 0.5f}, {0.25f, 0.25f}, {0.75f, 0.25f},
                                 {0.25f, 0.75f}, {0.75f, 0.75f}};
  int covered = 0;
  const ProbePrev* centerSource = nullptr;
  for (auto& fraction : fractions) {
    auto x = world.left + world.width() * fraction[0];
    auto y = world.top + world.height() * fraction[1];
    for (auto& prev : previousCovered) {
      auto& rect = prev.rect;
      if (x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom) {
        covered++;
        if (centerSource == nullptr) {
          centerSource = &prev;
        }
        break;
      }
    }
  }
  bool lost = covered >= 3;
  if (lost && centerSource != nullptr) {
    if (centerSource->source == ProbeSource::Grid) {
      tileProbe.uncLostGrid++;
    } else if (centerSource->source == ProbeSource::Blank) {
      tileProbe.uncLostBlank++;
    } else if (centerSource->depth >= 0 && centerSource->depth < TREE_LEVEL_COUNT) {
      tileProbe.uncLostTree[centerSource->depth]++;
    }
  }
  bool coarse = ProbeCovers(extension->coarser, cell, tileSize);
  bool finer = ProbeCovers(extension->finer, cell, tileSize);
  tileProbe.uncCoarse += coarse ? 1 : 0;
  tileProbe.uncFiner += finer ? 1 : 0;
  tileProbe.uncNone += !coarse && !finer ? 1 : 0;
  if (lost) {
    tileProbe.uncLost++;
    tileProbe.uncLostNone += !coarse && !finer ? 1 : 0;
  }
}
#endif

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

// Splits rect into the parts that no claimed rect covers.
static std::vector<Rect> SubtractClaimedRects(const Rect& rect, const std::vector<Rect>& claimed) {
  constexpr float MIN_PIECE_SIZE = 0.01f;
  std::vector<Rect> pieces = {rect};
  for (const auto& hole : claimed) {
    std::vector<Rect> next = {};
    for (const auto& piece : pieces) {
      if (!Rect::Intersects(piece, hole)) {
        next.push_back(piece);
        continue;
      }
      auto overlap = piece;
      overlap.intersect(hole);
      Rect parts[4] = {Rect::MakeLTRB(piece.left, piece.top, piece.right, overlap.top),
                       Rect::MakeLTRB(piece.left, overlap.bottom, piece.right, piece.bottom),
                       Rect::MakeLTRB(piece.left, overlap.top, overlap.left, overlap.bottom),
                       Rect::MakeLTRB(overlap.right, overlap.top, piece.right, overlap.bottom)};
      for (const auto& part : parts) {
        if (part.width() > MIN_PIECE_SIZE && part.height() > MIN_PIECE_SIZE) {
          next.push_back(part);
        }
      }
    }
    pieces = std::move(next);
  }
  return pieces;
}

// Composes a cell that no single level covers completely from the tiles that do exist: finer
// levels claim their area first, coarser levels only fill what is left. `levels` is ordered from
// the finest to the coarsest level and may reach beyond the L to L+3 window, so a tile that was on
// screen a moment ago keeps showing, scaled, after the window moved away from it. Used tiles are
// touched, which keeps them from being evicted while they are on screen. Returns the number of
// tasks added, so a cell without any tile can be left to the background. Tasks carry a layer so
// they are drawn from the coarsest level to the finest.
struct StaleTileView {
  std::shared_ptr<Tile> tile = nullptr;
  int level = 0;
  float zoomToRaster = 1.0f;
};

static size_t AppendMosaicScreenTasks(const std::vector<TreeLevelCoverage*>& levels,
                                      const std::vector<StaleTileView>& staleViews,
                                      TilePool* tilePool, const Rect& zoomRect, int tileSize,
                                      std::vector<DrawTask>* tasks) {
  std::vector<Rect> claimed = {};
  size_t added = 0;
  for (size_t index = 0; index < levels.size(); ++index) {
    auto& coverage = *levels[index];
    auto rasterRect = coverage.toRaster(zoomRect);
    auto displayScale = 1.0f / coverage.zoomToRaster;
    ForEachTileUnderRect(rasterRect, tileSize, [&](int tileX, int tileY) {
      auto& tile = coverage.tileAt(tileX, tileY);
      if (tile == nullptr) {
        return true;
      }
      auto drawRect = Rect::MakeXYWH(tileX * tileSize, tileY * tileSize, tileSize, tileSize);
      if (!drawRect.intersect(rasterRect)) {
        return true;
      }
      auto zoomPiece = drawRect;
      zoomPiece.scale(displayScale, displayScale);
      bool used = false;
      for (const auto& piece : SubtractClaimedRects(zoomPiece, claimed)) {
        auto rasterPiece = coverage.toRaster(piece);
        DrawTask task(tile, tileSize, rasterPiece, displayScale);
        task.setLayer(static_cast<int>(levels.size() - index));
        tasks->push_back(std::move(task));
        added++;
        used = true;
      }
      if (used) {
        tilePool->touch(TileKey::MakeTree(tileX, tileY, coverage.level));
      }
      claimed.push_back(zoomPiece);
      return true;
    });
  }
  // Tiles whose content changed only fill what no current tile covers.
  for (const auto& view : staleViews) {
    auto displayScale = 1.0f / view.zoomToRaster;
    auto zoomTile = view.tile->getTileRect(tileSize);
    zoomTile.scale(displayScale, displayScale);
    auto visible = zoomTile;
    if (!visible.intersect(zoomRect)) {
      continue;
    }
    for (const auto& piece : SubtractClaimedRects(visible, claimed)) {
      auto rasterPiece = piece;
      rasterPiece.scale(view.zoomToRaster, view.zoomToRaster);
      DrawTask task(view.tile, tileSize, rasterPiece, displayScale);
      task.setLayer(0);
      tasks->push_back(std::move(task));
      added++;
    }
    claimed.push_back(visible);
  }
  return added;
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
  TILE_PROBE(tileProbe = {}; auto probeStart = ProbeClock::now();)
  _hasContentChanged = false;
  auto dirtyRegions = _root->updateDirtyRegions();
  TILE_PROBE(tileProbe.dirtyMs = ProbeMs(probeStart, ProbeClock::now());)
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
  TILE_PROBE(auto probeFlagsStart = ProbeClock::now();)
  _root->updateStaticSubtreeFlags();
  TILE_PROBE(auto probeEnd = ProbeClock::now();
             tileProbe.flagsMs = ProbeMs(probeFlagsStart, probeEnd);
             ProbeReport(ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision),
                         ProbeMs(probeStart, probeEnd), _maxTilesRefinedPerFrame);)
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
  TileBudgetClock budget = {};
  budget.manager = surface->getContext()->drawingManager();
  budget.start = TileBudgetClock::Clock::now();
  budget.startSwitches = budget.manager->renderTargetSwitchCount();
  budget.timeMs = _tileBudgetTimeMs;
  budget.rtSwitches = _tileBudgetRTSwitches;
  budget.enabled = _tileUpdateMode != TileUpdateMode::Immediate &&
                   (_tileBudgetTimeMs > 0 || _tileBudgetRTSwitches > 0);
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
  TILE_PROBE(auto probeInvalidateStart = ProbeClock::now();)
  // Every change is an edit until the layers can tell a sharper image from a different picture.
  auto tileTasks = invalidateTileCaches(dirtyRegions, {}, GetViewportRect(surface, _contentOffset));
  TILE_PROBE(auto probeCollectStart = ProbeClock::now();
             tileProbe.invalidateMs = ProbeMs(probeInvalidateStart, probeCollectStart);)
  pruneStaleTiles(GetViewportRect(surface, _contentOffset));
  if (!selectTileTasks(surface, &tileTasks)) {
    recycleCurrentTileTasks(tileTasks);
    return renderDirect(surface, autoClear);
  }
  auto zoomScale = ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision);
  std::vector<CapturePlan> capturePlans = {};
  std::vector<int> taskPlan = {};
  TILE_PROBE(tileProbe.active = true; tileProbe.index = ++tileProbeCounter;
             tileProbe.collectMs = ProbeMs(probeCollectStart, ProbeClock::now());
             auto probePlanStart = ProbeClock::now(); CurrentProbePhase() = ProbePhase::Capture;)
  planTileCaptures(tileTasks, zoomScale, &capturePlans, &taskPlan);
  TILE_PROBE(auto probeTilesStart = ProbeClock::now();
             tileProbe.captureMs += ProbeMs(probePlanStart, probeTilesStart);
             CurrentProbePhase() = ProbePhase::Tiles;)
  auto dirtyRects = drawTileTasks(tileTasks, &capturePlans, taskPlan, surface, zoomScale, budget);
  TILE_PROBE(auto probeScreenStart = ProbeClock::now();
             tileProbe.tilesMs = ProbeMs(probeTilesStart, probeScreenStart) - tileProbe.runMs;
             CurrentProbePhase() = ProbePhase::Screen;)
  std::vector<Rect> skippedRects = {};
  std::vector<Rect> underlayRects = {};
  auto screenTasks = assembleScreenTasks(surface, &skippedRects, &underlayRects);
  TILE_PROBE(tileProbe.screenTasks = static_cast<int>(screenTasks.size());)
  drawScreenTasks(std::move(screenTasks), std::move(skippedRects), std::move(underlayRects), surface,
                  autoClear);
  TILE_PROBE(tileProbe.screenMs = ProbeMs(probeScreenStart, ProbeClock::now());
             CurrentProbePhase() = ProbePhase::Other;)
  return dirtyRects;
}

void DisplayList::planTileCaptures(const std::vector<DrawTask>& tileTasks, float zoomScale,
                                   std::vector<CapturePlan>* plans,
                                   std::vector<int>* taskPlan) const {
  // Each capture covers one cluster of tiles at a single raster scale, so a tile is captured with
  // the density it is drawn with. That is what lets tree tiles take part: their scale is 2^-level,
  // not the current zoom. Only the must tiles share a cluster. An optional tile may be dropped on its
  // own, and a drawn one has to be affordable on its own, so it is captured alone and only once it
  // is about to be drawn.
  plans->clear();
  taskPlan->assign(tileTasks.size(), -1);
  if (!_root->hasBackgroundStyle()) {
    return;
  }
  auto rasterScaleOf = [zoomScale](int level) {
    return level == kExactLevel ? zoomScale : GetTileRasterScale(level);
  };
  // Group by level first: one capture transform cannot serve two raster scales.
  std::vector<std::pair<int, std::vector<size_t>>> levels = {};
  for (size_t i = 0; i < tileTasks.size(); ++i) {
    auto& task = tileTasks[i];
    auto level = task.treeLevel();
    if (task.optional()) {
      auto rasterScale = rasterScaleOf(level);
      if (FloatNearlyZero(rasterScale)) {
        continue;
      }
      CapturePlan plan = {};
      plan.level = level;
      plan.viewMatrix = Matrix::MakeScale(rasterScale);
      plan.clusterRects = {task.tileRect()};
      (*taskPlan)[i] = static_cast<int>(plans->size());
      plans->push_back(std::move(plan));
      continue;
    }
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
    auto rasterScale = rasterScaleOf(levelEntry.first);
    if (FloatNearlyZero(rasterScale)) {
      continue;
    }
    std::vector<Rect> levelRects = {};
    levelRects.reserve(levelEntry.second.size());
    for (auto index : levelEntry.second) {
      levelRects.push_back(tileTasks[index].tileRect());
    }
    auto clusters = ClusterTileRects(levelRects);
    for (const auto& cluster : clusters) {
      CapturePlan plan = {};
      plan.level = levelEntry.first;
      plan.viewMatrix = Matrix::MakeScale(rasterScale);
      plan.clusterRects.reserve(cluster.indices.size());
      for (auto index : cluster.indices) {
        plan.clusterRects.push_back(levelRects[index]);
      }
      auto planIndex = static_cast<int>(plans->size());
      for (auto offset : cluster.indices) {
        (*taskPlan)[levelEntry.second[offset]] = planIndex;
      }
      plans->push_back(std::move(plan));
    }
  }
}

void DisplayList::prepareTileCapture(CapturePlan* plan) const {
  plan->prepared = true;
  // A background style only samples the area under its own bounds plus the blur reach, so cull the
  // capture against those footprints instead of the whole tile. Layers that overlap the tile but
  // sit under no background style are then not drawn. The captured area stays the cluster union,
  // so the surfaces and the way sibling styles share their output do not change.
  auto& viewMatrix = plan->viewMatrix;
  plan->cullRects = plan->clusterRects;
  Matrix inverse = Matrix::I();
  if (!viewMatrix.invert(&inverse)) {
    return;
  }
  auto worldRect = Rect::MakeEmpty();
  for (const auto& rect : plan->clusterRects) {
    worldRect.join(inverse.mapRect(rect));
  }
  TILE_PROBE(auto probeFootprintStart = ProbeClock::now();)
  auto footprints = collectBackgroundFootprints(worldRect, viewMatrix);
  TILE_PROBE(plan->probeFootprintMs = ProbeMs(probeFootprintStart, ProbeClock::now());
             plan->probeFootprints = static_cast<int>(footprints.size());
             plan->probeFootprintBox = Rect::MakeEmpty();
             for (const auto& footprint : footprints) { plan->probeFootprintBox.join(footprint); })
  plan->footprintCount = footprints.size();
  if (footprints.empty()) {
    return;
  }
  // Keep the raw rects while the per-layer cull loop can afford them: separate rects keep the
  // capture multi-pass, which is what enables style output sharing.
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
    plan->cullRects = std::move(rects);
  }
}

void DisplayList::runTileCapture(Surface* surface, CapturePlan* plan) const {
  DEBUG_ASSERT(plan->prepared);
  plan->captured = true;
  TILE_PROBE(auto probeManager = surface->getContext()->drawingManager();
             auto probeMark = probeManager->drawMark(); auto probeTasks = probeManager->taskProbe();
             CaptureProbe() = {}; auto probeCaptureStart = ProbeClock::now();
             CurrentProbePhase() = ProbePhase::Capture;)
  plan->map = captureBackgrounds(surface, plan->clusterRects, plan->viewMatrix, plan->cullRects);
  TILE_PROBE(auto probeCaptureMs = ProbeMs(probeCaptureStart, ProbeClock::now());
             tileProbe.captureMs += probeCaptureMs; tileProbe.runMs += probeCaptureMs;
             CurrentProbePhase() = ProbePhase::Tiles;
             ProbeCapture(plan->level, _tileSize, plan->clusterRects, plan->map.get(),
                          plan->cullRects.size(), plan->probeFootprints, plan->probeFootprintBox,
                          plan->probeFootprintMs, probeCaptureMs,
                          probeManager->drawMark() - probeMark, probeTasks,
                          probeManager->taskProbe());)
#if TILE_PROBE_DISCARD_CAPTURE
  plan->map = nullptr;
#endif
}

std::vector<Rect> DisplayList::drawTileTasks(std::vector<DrawTask>& tileTasks,
                                             std::vector<CapturePlan>* plans,
                                             const std::vector<int>& taskPlan, Surface* surface,
                                             float zoomScale, const TileBudgetClock& budget) {
  std::vector<Rect> dirtyRects = {};
  auto surfaceRect = Rect::MakeWH(surface->width(), surface->height());
  // Must tiles go first so that they are never starved by optional ones. Among the optional tiles,
  // the coarser tree level goes first, so the base tiles come before the refinements and the
  // budget sharpens the screen evenly.
  std::vector<size_t> order = {};
  order.reserve(tileTasks.size());
  std::vector<size_t> optionalOrder = {};
  for (size_t i = 0; i < tileTasks.size(); ++i) {
    (tileTasks[i].optional() ? optionalOrder : order).push_back(i);
  }
  std::stable_sort(optionalOrder.begin(), optionalOrder.end(), [&](size_t a, size_t b) {
    auto levelOf = [&](size_t index) {
      auto level = tileTasks[index].treeLevel();
      return level == kExactLevel ? std::numeric_limits<int>::min() : level;
    };
    return levelOf(a) > levelOf(b);
  });
  const auto mustCount = order.size();
  order.insert(order.end(), optionalOrder.begin(), optionalOrder.end());

  struct ClassStats {
    int picked = 0;
    int drawn = 0;
    int dropped = 0;
  };
  ClassStats stats[4] = {};
  auto statsOf = [&](TaskClass taskClass) -> ClassStats& {
    return stats[static_cast<size_t>(taskClass) - 1];
  };
  // Footprints of the base tiles that were drawn / dropped, for the log.
  size_t baseDrawnFootprints = 0;
  size_t baseDroppedFootprints = 0;
  int otherDrawn = 0;
  int otherPicked = 0;
  int baseDrawn = 0;
  bool forced = false;
  const char* stopReason = nullptr;
  const bool starving = budgetStarveFrames >= BUDGET_STARVE_FRAMES;
  auto ensurePrepared = [&](CapturePlan* plan) {
    if (plan->prepared) {
      return;
    }
    TILE_PROBE(auto probePrepareStart = ProbeClock::now();
               CurrentProbePhase() = ProbePhase::Capture;)
    prepareTileCapture(plan);
    TILE_PROBE(auto probePrepareMs = ProbeMs(probePrepareStart, ProbeClock::now());
               tileProbe.captureMs += probePrepareMs; tileProbe.runMs += probePrepareMs;
               CurrentProbePhase() = ProbePhase::Tiles;)
  };
  // Returns true when the estimated cost of a base tile fits what is left of the frame.
  auto baseFits = [&](int level, const CapturePlan* plan) {
    if (!budget.enabled) {
      return true;
    }
    auto footprints = plan != nullptr ? static_cast<double>(plan->footprintCount) : 0.0;
    // The measured cost only describes tiles of its own level: a level covers a different amount
    // of the document, so a tile of another level says nothing about this one.
    auto measured = baseCosts.find(level);
    auto measuredMs = measured != baseCosts.end() ? measured->second.first : 0.0;
    auto measuredRT = measured != baseCosts.end() ? measured->second.second : 0.0;
    auto estimateMs = std::max(footprints * BUDGET_MS_PER_FOOTPRINT, measuredMs);
    auto estimateRT = std::max(footprints * BUDGET_RT_PER_FOOTPRINT, measuredRT);
    if (budget.timeMs > 0 &&
        budget.elapsedMs() + estimateMs > static_cast<double>(budget.timeMs)) {
      return false;
    }
    return !(budget.rtSwitches > 0 && static_cast<double>(budget.usedSwitches()) + estimateRT >
                                          static_cast<double>(budget.rtSwitches));
  };
  for (size_t position = 0; position < order.size(); ++position) {
    auto taskIndex = order[position];
    auto& task = tileTasks[taskIndex];
    auto planIndex = taskPlan[taskIndex];
    auto* plan = planIndex >= 0 ? &(*plans)[static_cast<size_t>(planIndex)] : nullptr;
    if (position >= mustCount) {
      auto& taskStats = statsOf(task.taskClass());
      taskStats.picked++;
      bool drop = false;
      if (task.taskClass() == TaskClass::Base) {
        if (plan != nullptr) {
          ensurePrepared(plan);
        }
        auto fits = baseFits(task.treeLevel(), plan);
        if (!fits) {
          if (stopReason == nullptr) {
            stopReason = "base";
          }
        }
        auto footprints = plan != nullptr ? plan->footprintCount : 0;
        (fits ? baseDrawnFootprints : baseDroppedFootprints) += footprints;
        drop = !fits;
      } else {
        otherPicked++;
        if (auto reason = budget.exceeded()) {
          if (stopReason == nullptr) {
            stopReason = reason;
          }
          if (otherDrawn > 0 || !starving || forced) {
            drop = true;
          } else {
            forced = true;
          }
        }
      }
      if (drop) {
        taskStats.dropped++;
        recycleTileTask(task);
        hasZoomBlurTiles = true;
        continue;
      }
      taskStats.drawn++;
      (task.taskClass() == TaskClass::Base ? baseDrawn : otherDrawn)++;
    }
    const auto baseStartMs = budget.elapsedMs();
    const auto baseStartSwitches = budget.usedSwitches();
    if (plan != nullptr && !plan->captured) {
      ensurePrepared(plan);
      runTileCapture(surface, plan);
    }
    auto isTreeTask = task.treeLevel() != kExactLevel;
    auto* snapshots = plan != nullptr ? plan->map.get() : nullptr;
    TILE_PROBE(auto probeContext = surfaceCaches[task.sourceIndex()]->getContext();
               auto probeManager = probeContext->drawingManager();
               auto probeMark = probeManager->drawMark();
               auto probeSwitchesBefore = probeManager->taskProbe().switches;
               ConsumeProbe() = {}; auto probeStart = ProbeClock::now();)
    TILE_PROBE(TileContent() = {};)
    auto hasContent = drawTileTask(task, snapshots);
    if (task.taskClass() == TaskClass::Base) {
      baseCosts[task.treeLevel()] = {
          budget.elapsedMs() - baseStartMs,
          static_cast<double>(budget.usedSwitches() - baseStartSwitches)};
    }
    TILE_PROBE(ProbeRecordTile(task, ProbeMs(probeStart, ProbeClock::now()),
                               probeManager->drawMark() - probeMark, hasContent, ConsumeProbe(),
                               probeManager->taskProbe().switches - probeSwitchesBefore,
                               TileContent());)
    if (!hasContent && task.coversTiles()) {
      recycleBlankTiles(task);
    }
    if (isTreeTask && task.coversTiles()) {
      for (auto& tile : task.getTiles()) {
        releaseStaleTile(task.treeLevel(), tile->tileX, tile->tileY);
      }
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
  budgetStarveFrames = otherPicked > 0 && otherDrawn == 0 ? budgetStarveFrames + 1 : 0;
  TILE_PROBE(
      if (stats[0].picked + otherPicked > 0) {
        LOGI("[TileBudget] f=%llu time=%.1f/%.1fms rt=%u/%d | base pick=%d drawn=%d drop=%d "
             "fp=%zu/%zu | grid pick=%d drawn=%d drop=%d | refine pick=%d "
             "drawn=%d drop=%d | chain pick=%d drawn=%d drop=%d | forced=%d starve=%d | stop=%s",
             static_cast<unsigned long long>(tileProbe.index), budget.elapsedMs(),
             static_cast<double>(budget.timeMs), budget.usedSwitches(), budget.rtSwitches,
             stats[0].picked, stats[0].drawn, stats[0].dropped, baseDrawnFootprints,
             baseDroppedFootprints, stats[1].picked, stats[1].drawn, stats[1].dropped, stats[2].picked, stats[2].drawn, stats[2].dropped,
             stats[3].picked, stats[3].drawn, stats[3].dropped, forced ? 1 : 0, budgetStarveFrames,
             stopReason != nullptr ? stopReason : "none");
      })
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

std::vector<DrawTask> DisplayList::invalidateTileCaches(
    const std::vector<Rect>& dirtyRegions, const std::vector<Rect>& refinementRegions,
    const Rect& viewportRect) {
  if (dirtyRegions.empty() && refinementRegions.empty()) {
    return {};
  }
  auto allRegions = dirtyRegions;
  allRegions.insert(allRegions.end(), refinementRegions.begin(), refinementRegions.end());
  std::vector<DrawTask> tileTasks = {};
  invalidateCurrentGridTiles(allRegions, viewportRect, &tileTasks);
  invalidateTreeTiles(dirtyRegions, refinementRegions, viewportRect, &tileTasks);
  invalidateBlankTiles(allRegions);
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
                                      const std::vector<Rect>& refinementRegions,
                                      const Rect& viewportRect, std::vector<DrawTask>* tileTasks) {
  if (tilePool->size(TileType::Tree) == 0) {
    return;
  }
  auto zoomScale = ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision);
  auto baseLevel = GetTileLevel(zoomScale);
  struct LevelDirtyRects {
    std::vector<Rect> dirtyRects = {};
    std::vector<Rect> refineRects = {};
    Rect viewportRect = {};
  };
  std::unordered_map<int, LevelDirtyRects> rectsByLevel = {};
  std::vector<TileKey> dirtyKeys = {};
  std::vector<TileKey> staleKeys = {};
  for (auto& key : tilePool->keysOf(TileType::Tree)) {
    auto result = rectsByLevel.find(key.level);
    if (result == rectsByLevel.end()) {
      auto rasterScale = GetTileRasterScale(key.level);
      LevelDirtyRects rects = {};
      rects.dirtyRects = MapDirtyRegions(dirtyRegions, Matrix::MakeScale(rasterScale));
      rects.refineRects = MapDirtyRegions(refinementRegions, Matrix::MakeScale(rasterScale));
      rects.viewportRect = viewportRect;
      rects.viewportRect.scale(rasterScale / zoomScale, rasterScale / zoomScale);
      result = rectsByLevel.emplace(key.level, std::move(rects)).first;
    }
    auto tileRect =
        Rect::MakeXYWH(key.tileX * _tileSize, key.tileY * _tileSize, _tileSize, _tileSize);
    auto& rects = result->second;
    auto hits = [&tileRect](const std::vector<Rect>& list) {
      return std::any_of(list.begin(), list.end(), [&tileRect](const Rect& rect) {
        return Rect::Intersects(tileRect, rect);
      });
    };
    const bool editHit = hits(rects.dirtyRects);
    if (!editHit && !hits(rects.refineRects)) {
      continue;
    }
    // Only the coarsest level is patched, because the finer fallbacks are not drawn while the
    // grid covers every cell: patching them would replay the whole scene for pixels nobody sees.
    // They are dropped here instead and rebuilt by the chain top-up once the grid is settled.
    const auto coarsestLevel = baseLevel + TREE_LEVEL_COUNT - 1;
    if (key.level != coarsestLevel || !Rect::Intersects(tileRect, rects.viewportRect)) {
      // An edit drops the tile at once, so that no old content stays on screen. A refinement only
      // makes the same picture sharper, so a tile on screen keeps showing its old content, scaled,
      // until its replacement is drawn.
      if (!editHit && Rect::Intersects(tileRect, rects.viewportRect)) {
        staleKeys.push_back(key);
      } else {
        dirtyKeys.push_back(key);
      }
      continue;
    }
    auto tile = tilePool->find(key);
    for (auto* list : {&rects.dirtyRects, &rects.refineRects}) {
      for (auto& dirtyRect : *list) {
        auto drawRect = tileRect;
        if (drawRect.intersect(dirtyRect)) {
          DrawTask task(tile, _tileSize, drawRect);
          task.setTreeLevel(key.level);
          tileTasks->push_back(std::move(task));
        }
      }
    }
  }
  for (auto& key : dirtyKeys) {
    emptyTiles.push_back(tilePool->erase(key));
  }
  for (auto& key : staleKeys) {
    auto tile = tilePool->erase(key);
    auto& slot = staleTreeTiles[std::make_tuple(key.level, key.tileY, key.tileX)];
    if (slot != nullptr) {
      emptyTiles.push_back(std::move(slot));
    }
    slot = std::move(tile);
  }
}

void DisplayList::releaseStaleTile(int level, int tileX, int tileY) {
  auto result = staleTreeTiles.find(std::make_tuple(level, tileY, tileX));
  if (result == staleTreeTiles.end()) {
    return;
  }
  emptyTiles.push_back(std::move(result->second));
  staleTreeTiles.erase(result);
}

void DisplayList::pruneStaleTiles(const Rect& viewportRect) {
  if (staleTreeTiles.empty()) {
    return;
  }
  auto zoomScale = ToZoomScaleFloat(_zoomScaleInt, _zoomScalePrecision);
  for (auto iterator = staleTreeTiles.begin(); iterator != staleTreeTiles.end();) {
    auto level = std::get<0>(iterator->first);
    auto tileY = std::get<1>(iterator->first);
    auto tileX = std::get<2>(iterator->first);
    auto tileRect =
        Rect::MakeXYWH(tileX * _tileSize, tileY * _tileSize, _tileSize, _tileSize);
    auto displayScale = zoomScale / GetTileRasterScale(level);
    tileRect.scale(displayScale, displayScale);
    if (!Rect::Intersects(tileRect, viewportRect) || staleTreeTiles.size() > MAX_STALE_TILES) {
      emptyTiles.push_back(std::move(iterator->second));
      iterator = staleTreeTiles.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

void DisplayList::recycleCurrentTileTasks(const std::vector<DrawTask>& tileTasks) {
  for (auto& task : tileTasks) {
    recycleTileTask(task);
  }
}

void DisplayList::recycleTileTask(const DrawTask& task) {
  for (auto& tile : task.getTiles()) {
    auto key = task.treeLevel() == kExactLevel
                   ? TileKey::MakeGrid(tile->tileX, tile->tileY, _zoomScaleInt)
                   : TileKey::MakeTree(tile->tileX, tile->tileY, task.treeLevel());
    if (auto recycled = tilePool->erase(key)) {
      emptyTiles.push_back(std::move(recycled));
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

bool DisplayList::selectTileTasks(const Surface* surface, std::vector<DrawTask>* tileTasks) {
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
    return false;
  }
  TILE_PROBE(tileProbe.level = GetTileLevel(zoomScale);
             tileProbe.viewportChanged = viewportChanged;)
  const auto tileSizeFloat = static_cast<float>(_tileSize);
  const auto startX = FloatFloorToInt(renderRect.left / tileSizeFloat);
  const auto startY = FloatFloorToInt(renderRect.top / tileSizeFloat);
  const auto endX = FloatCeilToInt(renderRect.right / tileSizeFloat);
  const auto endY = FloatCeilToInt(renderRect.bottom / tileSizeFloat);

  if (startX >= endX || startY >= endY) {
    return false;
  }

  auto tileCount = static_cast<size_t>(endX - startX) * static_cast<size_t>(endY - startY);
  TILE_PROBE(tileProbe.cells = static_cast<int>(tileCount);)
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
      TILE_PROBE(tileProbe.hit++;)
    } else if (blankTiles->contains(key)) {
      TILE_PROBE(tileProbe.blank++;)
    } else {
      missingGrids.push_back(grid);
    }
  }
  TILE_PROBE(tileProbe.missing = static_cast<int>(missingGrids.size());)
  // A cell nothing covers shows the background color, which is worse than a blurry fallback, so
  // those cells are drawn first. The sort is stable, so the mouse distance still orders each group.
  if (useFallback && !coverages.empty()) {
    std::stable_partition(missingGrids.begin(), missingGrids.end(),
                          [&](const std::pair<int, int>& grid) {
                            return FindFinestCoverage(&coverages, getGridRect(grid), _tileSize) ==
                                   nullptr;
                          });
  }

  std::vector<std::shared_ptr<Tile>> freeTiles = {};
  std::vector<std::pair<int, int>> dirtyGrids = {};
  bool continuous = false;
  // A frame without any atlas has nothing to fall back on, so it goes straight to full resolution.
  if (missingGrids.size() == tileCount && (!useFallback || surfaceCaches.empty())) {
    freeTiles = createContinuousTiles(surface, endX - startX, endY - startY);
    continuous = !freeTiles.empty();
  }
  TILE_PROBE(tileProbe.continuous = continuous;)
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
                                  const std::vector<std::pair<int, int>>& cells,
                                  TaskClass taskClass) -> size_t {
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
        task.setTaskClass(taskClass);
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
      // Pass 1: cells without any fallback get the coarsest level, which needs the fewest tiles:
      // the cost of a tile is dominated by the layers it intersects rather than by its area, and a
      // large layer is drawn again for every tile it reaches. The tiles are base tasks, so a frame
      // whose budget cannot pay for them shows the background color in those cells. Fast mode
      // never rasterizes them.
      if (!uncoveredRects.empty() && _tileUpdateMode != TileUpdateMode::Fast) {
        auto& coarsest = coverages.back();
        rasterizeTreeTiles(&coarsest, CollectMissingTreeTiles(coarsest, uncoveredRects, _tileSize),
                           TaskClass::Base);
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
          auto refined = rasterizeTreeTiles(&coverages[i], cellsByLevel[i], TaskClass::Refine);
          refineBudget -= static_cast<int>(refined);
        }
      }
    }
    // Every viewport cell has its exact tile, so the screen is sharp and the budget can top up the
    // coarser fallback levels. A zoom-in remaps them onto the next frame's L+3..L+1; without them
    // that zoom has to rasterize a fallback level on the spot. The frame budget decides how many of
    // these tiles are actually drawn, and what is left keeps the loop running until the levels are
    // full.
    if (useFallback && !coverages.empty()) {
      const bool gridComplete = missingGrids.empty();
      // Top up the levels above L from L+2 towards L, only once the grid is complete.
      auto chainBudget = gridComplete ? std::max(_maxTilesRefinedPerFrame, 0) : 0;
      const auto lowestLevel = gridComplete ? size_t{1} : size_t{2};
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
        auto refined = rasterizeTreeTiles(&coverage, cells, TaskClass::Chain);
        chainBudget -= static_cast<int>(refined);
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
        LOGE("DisplayList::selectTileTasks() Not enough free tiles available.");
        return false;
      }
      // The next frame selects the remaining grids again, so they only need something to show now.
      hasZoomBlurTiles = true;
      dirtyGrids.resize(freeTiles.size());
    }
  }
  TILE_PROBE(tileProbe.paintGrids = static_cast<int>(dirtyGrids.size());)
  size_t tileIndex = 0;
  std::vector<std::shared_ptr<Tile>> taskTiles = {};
  taskTiles.reserve(dirtyGrids.size());
  for (auto& grid : dirtyGrids) {
    auto& tile = freeTiles[tileIndex++];
    tile->tileX = grid.first;
    tile->tileY = grid.second;
    taskTiles.push_back(tile);
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
      // A dropped grid tile leaves its cell on the fallback below it, or on the background color
      // when nothing covers it.
      if (useFallback) {
        task.setTaskClass(TaskClass::Grid);
      }
      tileTasks->push_back(std::move(task));
    }
  }
  return true;
}

std::vector<DrawTask> DisplayList::assembleScreenTasks(const Surface* surface,
                                                       std::vector<Rect>* skippedRects,
                                                       std::vector<Rect>* underlayRects) {
  const auto useFallback = _tileUpdateMode != TileUpdateMode::Immediate;
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
  screenTasks.reserve(static_cast<size_t>(endX - startX) * static_cast<size_t>(endY - startY));
  std::vector<TreeLevelCoverage> coverages = {};
  if (useFallback) {
    coverages =
        CollectTreeCoverages(tilePool.get(), blankTiles.get(), zoomScale, renderRect, _tileSize);
  }
  // Window levels plus the levels around it, finest first. Built when the first cell needs it.
  std::vector<TreeLevelCoverage> extension = {};
  std::vector<TreeLevelCoverage*> mosaicLevelList = {};
  bool mosaicBuilt = false;
  auto mosaicLevels = [&]() -> const std::vector<TreeLevelCoverage*>& {
    if (mosaicBuilt) {
      return mosaicLevelList;
    }
    mosaicBuilt = true;
    auto baseLevel = GetTileLevel(zoomScale);
    std::vector<int> extraLevels = {};
    for (int offset = -MOSAIC_FINER_LEVELS; offset < 0; ++offset) {
      extraLevels.push_back(baseLevel + offset);
    }
    for (int offset = TREE_LEVEL_COUNT; offset < TREE_LEVEL_COUNT + MOSAIC_COARSER_LEVELS;
         ++offset) {
      extraLevels.push_back(baseLevel + offset);
    }
    extension.reserve(extraLevels.size());
    for (auto level : extraLevels) {
      auto coverage = MakeTreeCoverage(tilePool.get(), blankTiles.get(), zoomScale, renderRect,
                                       _tileSize, level, false, MOSAIC_MAX_LEVEL_CELLS);
      if (!coverage.tiles.empty()) {
        extension.push_back(std::move(coverage));
      }
    }
    for (auto& coverage : extension) {
      if (coverage.level < baseLevel) {
        mosaicLevelList.push_back(&coverage);
      }
    }
    for (auto& coverage : coverages) {
      mosaicLevelList.push_back(&coverage);
    }
    for (auto& coverage : extension) {
      if (coverage.level > baseLevel) {
        mosaicLevelList.push_back(&coverage);
      }
    }
    return mosaicLevelList;
  };
  std::vector<StaleTileView> staleViewList = {};
  bool staleBuilt = false;
  auto staleViews = [&]() -> const std::vector<StaleTileView>& {
    if (staleBuilt) {
      return staleViewList;
    }
    staleBuilt = true;
    // Finer levels first, so the sharper old content wins where several overlap.
    for (auto& entry : staleTreeTiles) {
      StaleTileView view = {};
      view.tile = entry.second;
      view.level = std::get<0>(entry.first);
      view.zoomToRaster = GetTileRasterScale(view.level) / zoomScale;
      staleViewList.push_back(std::move(view));
    }
    std::sort(staleViewList.begin(), staleViewList.end(),
              [](const StaleTileView& a, const StaleTileView& b) { return a.level < b.level; });
    return staleViewList;
  };
  TILE_PROBE(static std::vector<ProbePrev> probePreviousCovered = {};
             std::vector<ProbePrev> probeCovered = {}; ProbeExtension probeExtension = {};)
  auto sortedTiles = GetSortedTiles(startX, endX, startY, endY, _tileSize, mousePosition);
  for (const auto& grid : sortedTiles) {
    auto gridRect =
        Rect::MakeXYWH(grid.first * _tileSize, grid.second * _tileSize, _tileSize, _tileSize);
    gridRect.intersect(renderRect);
    auto key = TileKey::MakeGrid(grid.first, grid.second, _zoomScaleInt);
    if (auto tile = tilePool->find(key)) {
      tilePool->touch(key);
      screenTasks.emplace_back(tile, _tileSize, tile->getTileRect(_tileSize, &renderRect));
      TILE_PROBE(probeCovered.push_back({ProbeWorldRect(gridRect, zoomScale), ProbeSource::Grid, 0});)
    } else if (blankTiles->contains(key)) {
      skippedRects->push_back(gridRect);
      TILE_PROBE(probeCovered.push_back({ProbeWorldRect(gridRect, zoomScale), ProbeSource::Blank, 0});)
    } else if (auto coverage = FindFinestCoverage(&coverages, gridRect, _tileSize)) {
      AppendFallbackScreenTasks(coverage, gridRect, _tileSize, &screenTasks, skippedRects);
      TILE_PROBE(ProbeFallback(static_cast<size_t>(coverage - coverages.data()));
                 probeCovered.push_back({ProbeWorldRect(gridRect, zoomScale), ProbeSource::Tree,
                                         static_cast<int>(coverage - coverages.data())});)
    } else if (useFallback && AppendMosaicScreenTasks(mosaicLevels(), staleViews(), tilePool.get(),
                                                      gridRect, _tileSize, &screenTasks) > 0) {
      underlayRects->push_back(gridRect);
      TILE_PROBE(tileProbe.mosaic++;
                 probeCovered.push_back({ProbeWorldRect(gridRect, zoomScale), ProbeSource::Tree, 0});)
    } else {
      skippedRects->push_back(gridRect);
      TILE_PROBE(tileProbe.uncovered++;
                 ProbeUncovered(tilePool.get(), blankTiles.get(), &probeExtension,
                                probePreviousCovered, gridRect, zoomScale, renderRect, _tileSize);)
    }
  }
  TILE_PROBE(probePreviousCovered = std::move(probeCovered);)
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
                                  std::vector<Rect> underlayRects, Surface* surface,
                                  bool autoClear) const {
  // Tiles are drawn layer by layer, so a finer tile lands on top of a coarser one. Within a layer,
  // sort by surface index to ensure they are drawn in batches.
  std::sort(screenTasks.begin(), screenTasks.end(), [](const DrawTask& a, const DrawTask& b) {
    if (a.layer() != b.layer()) {
      return a.layer() < b.layer();
    }
    return a.sourceIndex() < b.sourceIndex();
  });
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
  // The cells composed from several levels start from the background color, which stays where no
  // level has a tile.
  Paint underlayPaint = paint;
  underlayPaint.setColor(_backgroundColor);
  for (auto& rect : underlayRects) {
    canvas->drawRect(rect, underlayPaint);
    tileRect.join(rect);
  }
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
  baseCosts.clear();
  staleTreeTiles.clear();
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
  TILE_PROBE(std::vector<std::tuple<Rect, Rect, float>> probeStyles = {};
             std::vector<float> probeRadii = {};)
  auto collect = [](const Layer* layer, const Rect& bounds, float deviceScale, bool& safe,
                    std::vector<Rect>& out, const auto& self
                    TILE_PROBE(, std::vector<std::tuple<Rect, Rect, float>>& probeStyles)
                        TILE_PROBE(, std::vector<float>& probeRadii)) -> void {
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
           static_cast<uint32_t>(LayerStyleExtraSourceType::Background)) == 0) {
        continue;
      }
      TILE_PROBE(auto radius = BackgroundHandler::BackgroundBlurRadius(style.get(), layerScale);
                 if (radius > 0) { probeRadii.push_back(radius); })
      if (BackgroundHandler::IsNegligibleBackgroundStyle(style.get(), layerScale)) {
        continue;
      }
      // The style samples the backdrop under its own bounds plus the blur reach.
      auto footprint = layer->renderBounds;
      footprint.outset(layer->maxBackgroundOutset, layer->maxBackgroundOutset);
      out.push_back(footprint);
      TILE_PROBE(probeStyles.emplace_back(layer->renderBounds, footprint,
                                         std::max(layer->renderBounds.width(),
                                                  layer->renderBounds.height()) *
                                             layerScale);)
      break;
    }
    for (const auto& child : layer->_children) {
      self(child.get(), bounds, deviceScale, safe, out, self TILE_PROBE(, probeStyles)
                                                           TILE_PROBE(, probeRadii));
    }
  };
  collect(_root.get(), worldBounds, deviceScale, safe, footprints,
          collect TILE_PROBE(, probeStyles) TILE_PROBE(, probeRadii));
  TILE_PROBE(ProbeFootprintShape(probeStyles, deviceScale);
             ProbeBlurSigma(probeRadii);)
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

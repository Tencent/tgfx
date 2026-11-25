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

#pragma once

#include <deque>
#include <tuple>
#include "tgfx/core/Surface.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {
class RootLayer;
class Tile;
class TilePool;
class BlankTileSet;
class DrawTask;
class StyleOutputCache;
enum class TileType;
struct BackgroundSnapshotMap;
struct TreeLevelCoverage;

/**
 * RenderMode defines the different modes of rendering a DisplayList.
 */
enum class RenderMode {
  /**
   * Direct rendering mode. In this mode, the display list is rendered directly to the target
   * surface without any caching or optimization.
   */
  Direct,

  /**
   * Partial rendering mode. In this mode, only the dirty regions of the display list are rendered
   * to the target surface. This can improve performance when only a small part of the display list
   * changes. However, enabling partial rendering may cause some blending issues, since all layers
   * are first drawn onto a cached surface before being composited onto the target surface. Partial
   * rendering also requires extra memory (equal to the size of the target surface) to cache the
   * previous frame. This is the default rendering mode.
   */
  Partial,

  /**
   * Tiled rendering mode. In this mode, the display list is split into tiles, and each tile is
   * cached separately. This improves performance by redrawing only the parts of the display list
   * that have changed, and also enables efficient scrolling and zooming using contentOffset and
   * zoomScale. Only the portions of tiles that overlap dirty regions are redrawn, not the entire
   * tile. Tiles reached only by dirty regions outside the viewport are invalidated instead of
   * redrawn; they are rasterized again once the viewport reaches them. Whenever possible, adjacent
   * tiles are combined into a single draw call to reduce the total number of draw calls. Performance is similar to partial rendering when the viewport isn’t
   * zoomed or scrolled, though tiled rendering uses a bit more memory. However, for zooming and
   * scrolling, tiled rendering is much more efficient than partial rendering.
   */
  Tiled
};

/**
 * TileUpdateMode defines how missing tiles are produced in tiled rendering mode when the
 * zoomScale changes or when the viewport scrolls into uncached regions. A missing tile either
 * shows a fallback, which is a coarser rendering of the same content prepared ahead of time and
 * therefore still usable after the zoom changes, or is rasterized at the current zoomScale. A
 * frame with nothing left to rasterize prepares fallbacks for later zoom instead.
 */
enum class TileUpdateMode {
  /**
   * Missing tiles are rasterized at the current zoomScale within the same frame, and fallbacks are
   * not used. This produces the highest visual fidelity but may cause frame drops during heavy zoom
   * or pan. This is the default mode.
   */
  Immediate,

  /**
   * A missing tile shows the fallback covering it as a temporary stand-in, which produces brief
   * zoom blur, and the tiles at the current zoomScale fill in over the following frames at most
   * setMaxTilesRefinedPerFrame() per frame to keep the frame rate stable. An area no fallback
   * covers yet is rasterized within the same frame, so the screen always shows content.
   */
  Smooth,

  /**
   * Same as Smooth, except that an area no fallback covers yet keeps the background color instead
   * of being rasterized in the same frame. Combined with the setMaxTilesRefinedPerFrame() cap this
   * keeps the frame rate stable during heavy zoom or pan at the cost of longer-lasting blur or
   * background-colored regions.
   */
  Fast
};

/**
 * DisplayList represents a collection of layers can be drawn to a Surface. Note: All layers in the
 * display list are not thread-safe and should only be accessed from a single thread.
 */
class DisplayList {
 public:
  DisplayList();

  virtual ~DisplayList();

  /**
   * Returns the root layer of the display list. Note: The root layer cannot be added to another
   * layer. Therefore, properties like alpha, blendMode, position, matrix, visibility, scrollRect,
   * and mask have no effect on the root layer since it will never have a parent.
   */
  Layer* root() const;

  /**
   * Returns the current scale factor applied to the layer tree. This factor determines how much the
   * layer tree is scaled during rendering. Adjusting the zoomScale to scale the layer tree is more
   * efficient than applying a matrix directly, as it avoids invalidating the layer tree's internal
   * caches. The default value is 1.0f.
   */
  float zoomScale() const;

  /**
   * Sets the scale factor for the layer tree. This factor determines how much the layer tree is
   * scaled during rendering. Adjusting the zoomScale to scale the layer tree is more efficient than
   * applying a matrix directly, as it avoids invalidating the layer tree's internal caches. The
   * default value is 1.0f.
   */
  void setZoomScale(float zoomScale);

  /**
   * Returns the integer multiplier used for zoom scale precision. This value determines the
   * smallest step by which the zoom scale can change. For example, a precision of 100 means the
   * zoom scale can be adjusted in increments of 0.01 (1/100). Internally, the zoom scale is stored
   * as an integer. The precision is used to convert the zoom scale to an integer as follows:
   * - When zooming in (scale >= 1.0), the scale is multiplied by the precision.
   * - When zooming out (scale < 1.0), the reciprocal of the scale is multiplied by the precision.
   * The default precision is 1000, allowing zoom scale adjustments in steps of 0.001f.
   */
  int zoomScalePrecision() const {
    return _zoomScalePrecision;
  }

  /**
   * Sets the integer multiplier used for zoom scale precision.
   */
  void setZoomScalePrecision(int precision);

  /**
   * Returns the current content offset of the layer tree after applying the zoomScale. This offset
   * determines how far the origin of the layer tree is shifted relative to the surface's origin.
   * Adjusting the contentOffset to move the layer tree is more efficient than applying a matrix
   * directly, as it avoids invalidating the layer tree's internal caches. The default value is
   * (0, 0).
   */
  const Point& contentOffset() const {
    return _contentOffset;
  }

  /**
   * Sets the content offset of the layer tree after applying the zoomScale. This offset determines
   * how far the origin of the layer tree is shifted relative to the surface's origin. Adjusting the
   * contentOffset to move the layer tree is more efficient than applying a matrix directly, as it
   * avoids invalidating the layer tree's internal caches. The default value is (0, 0).
   * Note: The provided values are rounded to the nearest integer pixels internally to ensure
   * pixel-perfect alignment across different rendering modes.
   */
  void setContentOffset(float offsetX, float offsetY);

  /**
   * Returns the current render mode of the display list. The render mode determines how the display
   * list is rendered to the target surface. The default render mode is RenderMode::Partial.
   */
  RenderMode renderMode() const {
    return _renderMode;
  }

  /**
   * Sets the render mode of the display list. The render mode determines how the display list is
   * rendered to the target surface. The default render mode is RenderMode::Partial.
   */
  void setRenderMode(RenderMode renderMode);

  /**
   * Returns the tile size used in tiled rendering mode. This setting is ignored in other render
   * modes. It specifies the width and height of each tile when rendering the display list in tiled
   * mode. The tile size must be between 16 and 2048 pixels and should be a power of two. The
   * default is 256 pixels.
   */
  int tileSize() const {
    return _tileSize;
  }

  /**
   * Sets the size of the tiles used in tiled rendering mode.
   */
  void setTileSize(int tileSize);

  /**
   * Compatibility for callers built against the scale-keyed tile cache. The layered pool manages
   * its own budget, so the value is remembered but has no effect on rendering.
   */
  int maxTileCount() const {
    return _maxTileCount;
  }

  /**
   * Compatibility for callers built against the scale-keyed tile cache. It has no effect.
   */
  void setMaxTileCount(int count) {
    _maxTileCount = count;
  }

  /**
   * Returns the current tile update mode. This setting only applies in tiled rendering mode and
   * controls how missing tiles are produced when the zoomScale changes. The default is
   * TileUpdateMode::Immediate.
   */
  TileUpdateMode tileUpdateMode() const {
    return _tileUpdateMode;
  }

  /**
   * Sets the tile update mode used in tiled rendering mode.
   */
  void setTileUpdateMode(TileUpdateMode mode) {
    _tileUpdateMode = mode;
  }

  /**
   * Returns the per-frame budget for rasterizing missing tiles. This setting only applies in
   * tiled rendering mode and is ignored when tileUpdateMode() is TileUpdateMode::Immediate. While
   * the viewport stays still, the budget rasterizes the missing tiles and prepares fallbacks for
   * later zoom. While it zooms or pans, the budget instead sharpens the displayed fallbacks one
   * step at a time, coarse first. Fallbacks rasterized because nothing covers a missing tile do not
   * count against the budget in TileUpdateMode::Smooth. Higher values fill the screen faster during
   * zoom and pan at the cost of per-frame GPU load; lower values prioritize stable frame rate.
   */
  int maxTilesRefinedPerFrame() const {
    return _maxTilesRefinedPerFrame;
  }

  /**
   * Sets the maximum number of tiles processed per frame in tiled rendering mode. A value of 0
   * disables refinement, so a tile stays at the fallback it was drawn with and the tiled loop stops
   * asking for frames once the viewport is covered.
   */
  void setMaxTilesRefinedPerFrame(int count) {
    _maxTilesRefinedPerFrame = count;
  }

  /**
   * Returns the smallest scale fallback tiles are rasterized at in tiled rendering mode. Zero means
   * no limit. The default value is 0.
   */
  float minFallbackScale() const {
    return _minFallbackScale;
  }

  /**
   * Sets the smallest scale fallback tiles are rasterized at in tiled rendering mode. Fallback tiles
   * are rasterized at power-of-two scales up to eight times smaller than the zoom scale, and this
   * drops the ones smaller than the given scale: a coarse tile covers a large area of the layer
   * tree, so rasterizing it costs nearly as much as the whole tree. Below this scale, missing tiles
   * are rasterized at the zoom scale without any fallback, as in TileUpdateMode::Immediate. Zero or
   * a negative value removes the limit.
   */
  void setMinFallbackScale(float scale) {
    _minFallbackScale = scale > 0.0f ? scale : 0.0f;
  }

  /**
   * Returns the per-frame time budget, in seconds, for rasterizing tiles in tiled rendering mode,
   * including the background captures they need. Zero means no limit. The default value is 0.
   */
  float tileTimeBudget() const {
    return _tileTimeBudget;
  }

  /**
   * Sets the per-frame time budget, in seconds, for rasterizing tiles in tiled rendering mode. Once
   * a frame has spent the budget, its remaining tiles are left to later frames: a new tile shows
   * the finest cached fallback of another scale in the meantime, or the background color when
   * there is none, and a tile whose content changed keeps showing its outdated content. Neighboring
   * tiles share a background capture and are rasterized together, at most a 2x2 block of one scale,
   * and the first such block of a frame is always rasterized, so every frame makes progress. Zero or
   * a negative value removes the limit.
   */
  void setTileTimeBudget(float seconds) {
    _tileTimeBudget = seconds > 0.0f ? seconds : 0.0f;
  }

  /**
   * Returns the background color of the display list. The background is an infinite rectangle that
   * covers the entire display area and is drawn using the SrcOver blend mode during rendering.
   * It is only drawn in the DisplayList::render() path, not when calling Layer::draw() directly.
   * The default value is transparent.
   */
  Color backgroundColor() const {
    return _backgroundColor;
  }

  /**
   * Sets the background color of the display list.
   */
  void setBackgroundColor(const Color& color);

  /**
   * Returns the maximum cache size (longest edge in pixels) for subtree caching. Default is 0
   * (disabled).
   *
   * When enabled, starting from the root layer, any static subtree (where neither the layer nor
   * its descendants have modified properties) whose rendered longest edge is smaller than maxSize
   * will be cached as a texture. The cache uses a mipmap-style scaling strategy, snapping to
   * power-of-2 fractions of maxSize (e.g., maxSize, maxSize/2, maxSize/4, ...) to minimize memory
   * waste. Once a subtree is cached, subsequent renders skip child traversal entirely, greatly
   * improving rendering performance. This optimization is particularly effective in zoom-out
   * scenarios where dense graphics are packed into a smaller screen area.
   */
  int subtreeCacheMaxSize() const {
    return _subtreeCacheMaxSize;
  }

  /**
   * Sets the maximum cache size (longest edge in pixels) for subtree caching. Default is 0 (disabled).
   */
  void setSubtreeCacheMaxSize(int maxSize);

  /**
   * Sets whether to show dirty regions during rendering. When enabled, the dirty regions will be
   * highlighted in the rendered output. This is useful for debugging to visualize which parts of
   * the display list are being updated. The default value is false.
   */
  void showDirtyRegions(bool show);

  /**
   * Returns true if the content of the display list has changed since the last rendering. This can
   * be used to determine if the display list needs to be re-rendered.
   */
  bool hasContentChanged() const;

  /**
   * Renders the display list onto the given surface.
   * @param surface The surface to render the display list on.
   * @param autoClear If true, the surface will be cleared before rendering the display list.
   * Otherwise, the display list will be rendered over the existing content.
   */
  void render(Surface* surface, bool autoClear = true);

 private:
  std::shared_ptr<RootLayer> _root = nullptr;
  Color _backgroundColor = Color::Transparent();
  int64_t _zoomScaleInt = 1000;
  int _zoomScalePrecision = 1000;
  Point _contentOffset = {};
  RenderMode _renderMode = RenderMode::Partial;
  int _tileSize = 256;
  TileUpdateMode _tileUpdateMode = TileUpdateMode::Immediate;
  int _maxTilesRefinedPerFrame = 5;
  float _tileTimeBudget = 0.0f;
  float _minFallbackScale = 0.0f;
  int _subtreeCacheMaxSize = 0;
  bool _showDirtyRegions = false;
  bool _hasContentChanged = false;
  bool hasZoomBlurTiles = false;
  int64_t lastZoomScaleInt = 1000;
  Point lastContentOffset = {};
  Point mousePosition = {};
  int totalTileCount = 0;
  int _maxTileCount = 0;
  int lastSurfaceWidth = 0;
  int lastSurfaceHeight = 0;
  size_t minTreeTileCount = 0;
  std::vector<std::shared_ptr<Surface>> surfaceCaches = {};
  std::unique_ptr<TilePool> tilePool;
  std::unique_ptr<BlankTileSet> blankTiles;
  std::vector<std::shared_ptr<Tile>> emptyTiles = {};
  std::deque<std::vector<Rect>> lastDirtyRegions = {};
  // Outputs of background-sourced styles kept across frames at the current zoom scale.
  std::unique_ptr<StyleOutputCache> styleOutputCache;
  // Tiles, as (tree level, tileX, tileY) at forceCaptureZoomScaleInt, whose last draw skipped a
  // background-sourced style. They are captured until a draw completes without skipping.
  std::vector<std::tuple<int, int, int>> forceCaptureTiles = {};
  int64_t forceCaptureZoomScaleInt = 0;
  // Redraws of cached tiles whose content changed, left over by the time budget of an earlier
  // frame. Their tiles keep the outdated content until these are drawn.
  std::vector<DrawTask> deferredPatches = {};

  std::vector<Rect> renderDirect(Surface* surface, bool autoClear) const;

  std::vector<Rect> renderPartial(Surface* surface, bool autoClear,
                                  const std::vector<Rect>& dirtyRegions);

  std::vector<Rect> renderTiled(Surface* surface, bool autoClear,
                                const std::vector<Rect>& dirtyRegions);

  void checkTileCount(Surface* renderSurface);

  std::vector<DrawTask> invalidateTileCaches(const std::vector<Rect>& dirtyRegions,
                                             const Rect& viewportRect);

  void invalidateCurrentGridTiles(const std::vector<Rect>& dirtyRegions, const Rect& viewportRect,
                                  std::vector<DrawTask>* tileTasks);

  void invalidateTreeTiles(const std::vector<Rect>& dirtyRegions, const Rect& viewportRect,
                           std::vector<DrawTask>* tileTasks);

  void invalidateBlankTiles(const std::vector<Rect>& dirtyRegions);

  void recycleCurrentTileTasks(const std::vector<DrawTask>& tileTasks);

  void recycleGridTiles();

  void recycleBlankTiles(const DrawTask& task);

  std::vector<DrawTask> collectScreenTasks(const Surface* surface, std::vector<DrawTask>* tileTasks,
                                           std::vector<Rect>* skippedRects);

  /**
   * Returns the part of the viewport covered by the root layer, in the zoomed grid, or an empty
   * rect when nothing is visible.
   */
  Rect getVisibleRenderRect(const Surface* surface) const;

  /**
   * Returns the coarsest tree level fallback tiles may use, following minFallbackScale().
   */
  int maxTreeLevel() const;

  /**
   * Returns the screen tasks that display the tiles currently cached for the viewport: the exact
   * tile of each cell, or else the finest complete fallback. Cells nothing covers are appended to
   * skippedRects.
   */
  std::vector<DrawTask> collectDisplayTasks(const Surface* surface,
                                            std::vector<Rect>* skippedRects);

  /**
   * Appends the deferred patches whose tiles are still cached under the same keys.
   */
  void takeDeferredPatches(std::vector<DrawTask>* tileTasks);

  /**
   * Leaves the tasks that were not drawn to later frames: new tiles are returned to the free list,
   * and redraws of cached tiles are kept as deferred patches.
   */
  void deferTileTasks(const std::vector<DrawTask>& tileTasks, const std::vector<bool>& drawn);

  size_t rasterizeTreeTiles(const Surface* surface, const std::vector<std::pair<int, int>>& cells,
                            TreeLevelCoverage* coverage, std::vector<DrawTask>* tileTasks);

  std::vector<std::shared_ptr<Tile>> getFreeTiles(const Surface* renderSurface, TileType type,
                                                  size_t tileCount);

  std::vector<std::shared_ptr<Tile>> createContinuousTiles(const Surface* renderSurface,
                                                           int requestCountX, int requestCountY);

  bool createEmptyTiles(const Surface* renderSurface);

  int nextSurfaceTileCount(Context* context) const;

  int getMaxTileCountPerAtlas(Context* context) const;

  /**
   * Draws the task into its atlas. Returns whether anything was drawn. missingBackdrops receives the
   * number of background-sourced styles skipped because no backdrop was available for them.
   */
  bool drawTileTask(const DrawTask& task, BackgroundSnapshotMap* snapshots, bool snapshotsCover,
                    int* missingBackdrops = nullptr) const;

  /**
   * Captures the backgrounds for the tasks listed by taskIndices, which all rasterize the given
   * tree level. Marks in taskSnapshotCover the tasks the capture covers. Returns nullptr when the
   * tasks need no map.
   */
  std::unique_ptr<BackgroundSnapshotMap> captureTaskBackgrounds(
      Surface* surface, const std::vector<DrawTask>& tileTasks,
      const std::vector<size_t>& taskIndices, int level, float zoomScale,
      std::vector<bool>* taskSnapshotCover) const;

  /**
   * Returns the raster scale, the matrix and the clip rect a tile task is rasterized with into its
   * atlas.
   */
  float getTileDrawGeometry(const DrawTask& task, Matrix* viewMatrix, Rect* clipRect) const;

  /**
   * Collects, in global coordinates, the areas of background-sourced styles in the task that have
   * no cached output and cannot read their backdrop from the tile. Returns false when there are
   * none, in which case the task needs no captured snapshot.
   */
  bool collectMissingBackdrops(const DrawTask& task, std::vector<Rect>* missingRects) const;

  void collectMissingBackdrops(Layer* layer, const Rect& globalRect, float parentAlpha, float scale,
                               bool offscreen, std::vector<Rect>* drawnBounds,
                               std::vector<Rect>* missingRects) const;

  bool isForceCaptured(const DrawTask& task) const;

  /**
   * Records whether the task's last draw skipped background-sourced styles. A tile whose draw
   * skipped some is captured on every later draw until one completes without skipping.
   */
  void updateForceCapture(const DrawTask& task, bool skippedStyles);

  /**
   * Captures the backgrounds and draws the tasks one group at a time, a group being the tasks of
   * one tree level within a 2x2 block of tiles. Stops before a group once budgetSeconds has passed,
   * if it is positive, leaving the rest undrawn; the first group is always drawn. drawn receives
   * which tasks were drawn. Returns the dirty rects on the surface.
   */
  std::vector<Rect> drawTileTasks(const std::vector<DrawTask>& tileTasks, Surface* surface,
                                  float zoomScale, double budgetSeconds, std::vector<bool>* drawn);

  void drawScreenTasks(std::vector<DrawTask> screenTasks, std::vector<Rect> skippedRects,
                       Surface* surface, bool autoClear) const;

  void renderDirtyRegions(Canvas* canvas, std::vector<Rect> dirtyRegions);

  Matrix getViewMatrix() const;

  void resetCaches();

  bool drawRootLayer(Surface* surface, const Rect& drawRect, const Matrix& viewMatrix,
                     bool autoClear, BackgroundSnapshotMap* snapshots,
                     bool readTileBackdrop = false, bool snapshotsCover = true,
                     int* missingBackdrops = nullptr) const;

  std::vector<Rect> collectBackgroundFootprints(const Rect& worldBounds) const;

  static void collectBackgroundFootprints(const Layer* layer, const Rect& bounds, bool clipped,
                                          bool covered, std::vector<Rect>* footprints, float alpha);

  static std::vector<Rect> narrowCullRects(const std::vector<Rect>& footprints,
                                           const std::vector<Rect>& captureRects);

  static void mergeCullRects(std::vector<Rect>* rects);

  static std::vector<Rect> buildCullRects(const std::vector<Rect>& footprints,
                                          const std::vector<Rect>& captureRects);

  std::vector<Rect> computeCaptureCullRects(const std::vector<Rect>& worldRects,
                                            const Matrix& pixelToWorld) const;

  static bool HasVisibleBackgroundStyle(const Layer* layer, const Rect& unionRect,
                                        const std::vector<Rect>& rects, float scale);

  /**
   * Captures the backdrop the background styles sample. `renderRects` are in the surface pixels of
   * `viewMatrix`, which maps the layer space to those pixels; a tile passes its own raster scale so
   * the snapshot density matches the tile it is drawn into. The layers drawn are culled to the
   * footprints those styles sample inside it. `styleSourceRects`, in the same pixels, cover every
   * pass drawing with the returned map; the layer style sources cached for those passes are
   * recorded against them. Pass none to record against `renderRects`.
   */
  std::unique_ptr<BackgroundSnapshotMap> captureBackgrounds(
      Surface* surface, const std::vector<Rect>& renderRects, const Matrix& viewMatrix,
      const std::vector<Rect>& styleSourceRects = {}) const;

  /**
   * Maps render rects in surface pixels to the world-space rects layer style sources are recorded
   * against, widened by the root's maxBackgroundOutset.
   */
  std::vector<Rect> makeStyleSourceRects(const std::vector<Rect>& renderRects,
                                         const Matrix& viewMatrix) const;

  void updateMousePosition();
};
}  // namespace tgfx

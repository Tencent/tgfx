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

#pragma once

#include <deque>
#include <unordered_map>
#include "tgfx/core/Surface.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {
class RootLayer;
class Tile;
class TileCache;
class SlidingWindowTracker;
struct DrawTask;
struct FallbackTile;

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
   * Tiled rendering mode. In this mode, the display list is divided into tiles, and each tile is
   * rendered separately. This can improve performance by rendering only the parts of the display
   * list that have changed, and also enables efficient scrolling and zooming (using contentOffset
   * and zoomScale). However, tiled rendering may use more memory for tile caches than partial
   * rendering.
   */
  Tiled
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
  float zoomScale() const {
    return _zoomScale;
  }

  /**
   * Sets the scale factor for the layer tree. This factor determines how much the layer tree is
   * scaled during rendering. Adjusting the zoomScale to scale the layer tree is more efficient than
   * applying a matrix directly, as it avoids invalidating the layer tree's internal caches. The
   * default value is 1.0f.
   */
  void setZoomScale(float zoomScale);

  /**
   * Returns the integer multiplier used for zoom scale precision. This value determines the
   * smallest increment by which the zoom scale can change. For example, a precision of 100 means
   * the zoom scale can be adjusted in steps of 0.01 (1/100). Internally, the zoom scale is first
   * rounded to the nearest integer and then converted back to a float for rendering. The precision
   * is used to convert the zoom scale to an integer as follows:
   * - When zooming in (scale >= 1.0), the scale is multiplied by the precision.
   * - When zooming out (scale < 1.0), the reciprocal of the scale is multiplied by the precision.
   * The default precision is 500, allowing zoom scale adjustments in steps of 0.002f.
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
   * Returns the maximum number of tiles that can be created in tiled rendering mode. This setting is
   * ignored in other render modes. If the specified count is less than the minimum required for tiled
   * rendering, it will be automatically increased to meet the minimum. The minimum value is calculated
   * based on the tile size and viewport size, ensuring the visible area is always fully covered, even
   * if the viewport is offset by half a tile. For example, with a tile size of 256 pixels and a
   * viewport of 512x512 pixels, the minimum tile count is 9 (2 tiles in each direction plus 1 for
   * offset). The default is 0, which means the minimum tile count will be used based on the viewport
   * size and tile size.
   */
  int maxTileCount() const {
    return _maxTileCount;
  }

  /**
   * Sets the maximum number of tiles that can be created in tiled rendering mode.
   */
  void setMaxTileCount(int count);

  /**
   * Returns true if zoom blur is allowed in tiled rendering mode. This setting is ignored in other
   * render modes. When enabled, if the zoomScale changes and cached images at other zoom levels are
   * available, the display list will first use those caches to render when the frame rate is low.
   * It will then gradually update to the current zoomScale in later frames. This can improve
   * zooming performance, but may cause temporary zoom blur artifacts. The default is true.
   */
  bool allowZoomBlur() const {
    return _allowZoomBlur;
  }

  /**
   * Sets whether to allow zoom blur in tiled rendering mode.
   */
  void setAllowZoomBlur(bool allow) {
    _allowZoomBlur = allow;
  }

  /**
   * Returns the render time budget in microseconds. This value limits how long rendering can take
   * for each frame, helping the application maintain a consistent frame rate. If rendering goes
   * over this budget, some parts may be skipped or rendered at lower quality to stay within the
   * limit. You can adjust this value each frame based on your app’s performance needs. Rendering
   * may still take longer than the budget, especially if there is no content that can be skipped or
   * rendered at lower quality. The default is 40,000 microseconds (25 fps).
   */
  int64_t renderTimeBudget() const {
    return _renderTimeBudget;
  }

  /**
   * Sets the render time budget in microseconds.
   */
  void setRenderTimeBudget(int64_t budget) {
    _renderTimeBudget = budget;
  }

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
  float _zoomScale = 1.0f;
  int _zoomScalePrecision = 500;
  Point _contentOffset = {};
  RenderMode _renderMode = RenderMode::Partial;
  int _tileSize = 256;
  int _maxTileCount = 0;
  bool _allowZoomBlur = true;
  int64_t _renderTimeBudget = 40000;  // 40 milliseconds (25 fps)
  SlidingWindowTracker* tileTimeTracker = nullptr;
  SlidingWindowTracker* screenTimeTracker = nullptr;
  bool _showDirtyRegions = false;
  bool _hasContentChanged = false;
  float lastZoomScale = 1.0f;
  Point lastContentOffset = {};
  int totalTileCount = 0;
  std::vector<std::shared_ptr<Surface>> surfaceCaches = {};
  std::unordered_map<float, TileCache*> tileCaches = {};
  std::vector<std::shared_ptr<Tile>> emptyTiles = {};
  std::deque<std::vector<Rect>> lastDirtyRegions = {};

  std::vector<Rect> renderDirect(Surface* surface, bool autoClear) const;

  std::vector<Rect> renderPartial(Surface* surface, bool autoClear,
                                  const std::vector<Rect>& dirtyRegions);

  std::vector<Rect> renderTiled(Surface* surface, bool autoClear,
                                const std::vector<Rect>& dirtyRegions, int64_t renderTimeBudget);

  void checkTileCount(Surface* renderSurface);

  std::vector<DrawTask> invalidateTileCaches(const std::vector<Rect>& dirtyRegions);

  void invalidateCurrentTileCache(const TileCache* tileCache, const std::vector<Rect>& dirtyRegions,
                                  std::vector<DrawTask>* tileTasks) const;

  std::vector<DrawTask> collectScreenTasks(const Surface* surface, std::vector<DrawTask>* tileTasks,
                                           std::vector<FallbackTile>* fallbackTiles);

  std::vector<std::pair<float, TileCache*>> getSortedTileCaches() const;

  std::vector<DrawTask> getFallbackDrawTasks(
      int tileX, int tileY, const std::vector<std::pair<float, TileCache*>>& sortedCaches) const;

  std::vector<std::shared_ptr<Tile>> getFreeTiles(
      const Surface* renderSurface, size_t tileCount,
      const std::vector<std::pair<float, TileCache*>>& sortedCaches);

  std::vector<std::shared_ptr<Tile>> createContinuousTiles(const Surface* renderSurface,
                                                           int requestCountX, int requestCountY);

  bool createEmptyTiles(const Surface* renderSurface);

  int nextSurfaceTileCount(Context* context) const;

  int getMaxTileCountPerAtlas(Context* context) const;

  std::vector<Rect> drawFallbackTiles(const std::vector<FallbackTile>& fallbackTiles,
                                      std::vector<DrawTask>* screenTasks, int64_t renderTimeBudget);

  Rect drawTileTask(const DrawTask& task, bool requireFlush = false) const;

  void drawScreenTasks(std::vector<DrawTask> screenTasks, Surface* surface, bool autoClear,
                       bool recordScreenTime) const;

  void renderDirtyRegions(Canvas* canvas, std::vector<Rect> dirtyRegions);

  Matrix getViewMatrix() const;

  void resetCaches();
};
}  // namespace tgfx

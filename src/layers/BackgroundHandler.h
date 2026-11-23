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

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>
#include "layers/BackgroundSnapshotMap.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

class BackgroundCapturer;
class BackgroundSource;
class Canvas;
class DrawArgs;
class Layer;
class LayerStyle;
class Surface;
struct LayerStyleSource;

struct LayerChildRange {
  // Index of the last child to visit, or -1 to visit none.
  int lastIndex = -1;
  // When false, the parent skips its Above styles and foreground, and every child in the range,
  // including the last one, is drawn through BackgroundHandler::drawChild.
  bool drawTrailingContent = true;
};

/**
 * Strategy for rendering Background-sourced LayerStyles.
 *   - BackgroundCapturer: samples the bg surface into a snapshot map during the capture pass.
 *   - BackgroundConsumer: blits matched snapshots back during the consume pass.
 *   - NoOp: skips Background-sourced styles (intermediate artifacts, 3D subtrees).
 * The active handler lives on DrawArgs::backgroundHandler.
 */
class BackgroundHandler {
 public:
  virtual ~BackgroundHandler() = default;

  static BackgroundHandler* NoOp();

  // Dispatches a background-sourced style through args.backgroundHandler, or NoOp if null.
  static void DispatchOrSkip(const DrawArgs& args, Canvas* canvas, Layer* layer, float alpha,
                             LayerStyle* style, const LayerStyleSource* source);

  virtual void drawBackgroundStyle(const DrawArgs& args, Canvas* canvas, Layer* layer, float alpha,
                                   LayerStyle* style, const LayerStyleSource* source) = 0;

  // Returns this handler as a BackgroundCapturer pointer, or nullptr if it is not one.
  // Replaces isCapturer() + static_cast with a single type-safe query that the compiler enforces.
  virtual BackgroundCapturer* asCapturer() {
    return nullptr;
  }

  // Returns true when descendants of `layer` should render onto a real Surface so a sub
  // background source can sample them back. Capturer returns true only when `layer` actually has
  // descendant background-sourced styles AND a GPU context is available. Consumer returns true
  // only when it mirrors a capture pass that selects surfaces from the live layer tree; NoOp and
  // other passes always return false.
  virtual bool needsSurface(Layer* /*layer*/) const {
    return false;
  }

  // Builds a sub-handler bound to a Surface-backed sub bg source. `localToWorld` maps the
  // layer's local space to root device; `localToSurface` maps it to the sub surface's pixels.
  // Returns nullptr when the parent handler does not need rebinding for this offscreen subtree
  // (e.g. Consumer, which is canvas-matrix independent), letting the caller keep the parent.
  virtual std::unique_ptr<BackgroundHandler> createSubHandler(
      Surface* /*surface*/, const DrawArgs& /*parentArgs*/, const Rect& /*localBounds*/,
      const Matrix& /*localToWorld*/, const Matrix& /*localToSurface*/) const {
    return nullptr;
  }

  // Returns the narrowed renderRects this sub-handler should use, or nullptr when the caller
  // should keep the parent's renderRects. Only meaningful on handlers produced by
  // createSubHandler; the base class default returns nullptr.
  virtual const std::vector<Rect>* renderRects() const {
    return nullptr;
  }

  // Returns the rects the layer style sources are recorded against during this pass, or nullptr
  // when they follow renderRects. Only a capture pass narrows its draw, but the sources it caches
  // are reused outside those rects, so it records them against the whole captured area instead.
  // Belongs to the pass rather than to DrawArgs so that a recording which deliberately clears
  // renderRects to stay complete cannot inherit it by accident. Base class returns nullptr.
  virtual const std::vector<Rect>* styleSourceRects() const {
    return nullptr;
  }

  // Selects the children to visit and whether to draw the parent's content after them.
  virtual LayerChildRange childRange(const Layer* parent, float alpha) const;

  // Draws a child whose descendants must be visited or whose parent has a truncated range.
  // Descendants of siblings before a background style are part of the backdrop source, so they
  // must be drawn completely even when they fall outside the tile's clip. The final child of a
  // full range uses the layer's normal draw path directly, so this is only called when
  // forceDescendants is true or the range is truncated. A subclass that intercepts a specific
  // child must return drawTrailingContent = false from childRange for its parent.
  virtual void drawChild(const DrawArgs& args, Canvas* canvas, Layer* parent, Layer* child,
                         float alpha, bool forceDescendants);

  // Returns a previously cached LayerStyleSource for the given layer, or nullptr if none exists.
  // The consume pass reuses sources from capture to avoid redundant image recordings. The default
  // returns nullptr, so NoOp handlers don't engage caching.
  virtual const LayerStyleSource* getCachedLayerStyleSource(Layer* /*layer*/) const {
    return nullptr;
  }

  // Determines whether to record an uncached source for this layer's styles in this pass.
  virtual bool needsLayerStyleSource(Layer* /*layer*/) const {
    return true;
  }

  // Whether this handler will cache a LayerStyleSource for `layer`. Used by Layer::drawDirectly
  // to decide if `cacheLayerStyleSource` should be called.
  virtual bool canCacheLayerStyleSource(Layer* /*layer*/) const {
    return false;
  }

  // Takes ownership of `source` and caches it under `layer`, returning the raw pointer to the
  // cached entry. Precondition: canCacheLayerStyleSource(layer) returned true.
  virtual const LayerStyleSource* cacheLayerStyleSource(
      Layer* /*layer*/, std::unique_ptr<LayerStyleSource> /*source*/) {
    return nullptr;
  }
};

class BackgroundCapturer : public BackgroundHandler {
 public:
  BackgroundCapturer(BackgroundSnapshotMap* snapshots, std::shared_ptr<BackgroundSource> source,
                     const std::vector<Rect>* styleSourceRects)
      : snapshots(snapshots), bgSource(std::move(source)), _styleSourceRects(styleSourceRects) {
  }

  void drawBackgroundStyle(const DrawArgs& args, Canvas* canvas, Layer* layer, float alpha,
                           LayerStyle* style, const LayerStyleSource* source) override;

  BackgroundCapturer* asCapturer() override {
    return this;
  }

  bool needsSurface(Layer* layer) const override;

  std::unique_ptr<BackgroundHandler> createSubHandler(Surface* surface, const DrawArgs& parentArgs,
                                                      const Rect& localBounds,
                                                      const Matrix& localToWorld,
                                                      const Matrix& localToSurface) const override;

  const std::vector<Rect>* renderRects() const override {
    return _renderRects.empty() ? nullptr : &_renderRects;
  }

  const std::vector<Rect>* styleSourceRects() const override {
    return _styleSourceRects;
  }

  LayerChildRange childRange(const Layer* parent, float alpha) const override;

  void drawChild(const DrawArgs& args, Canvas* canvas, Layer* parent, Layer* child, float alpha,
                 bool forceDescendants) override;

  const LayerStyleSource* getCachedLayerStyleSource(Layer* layer) const override;

  bool canCacheLayerStyleSource(Layer* layer) const override;

  const LayerStyleSource* cacheLayerStyleSource(Layer* layer,
                                                std::unique_ptr<LayerStyleSource> source) override;

  // Returns true when `layer` has a visible descendant whose Background-sourced style draws. This
  // is the live-tree selection both the export capturer and its matching consumer use, so they
  // agree on which offscreen groups need a real Surface.
  static bool HasDrawableBackdropDescendant(Layer* layer, float alpha);

  // Runs a capture pass: replays captureRoot onto bgSource's canvas, populating snapshots. The
  // replay walks the tree exactly once with renderRects set to the rects the capture actually
  // samples, so drawLayer culls each layer whose renderBounds does not intersect any of them.
  // styleSourceRects covers the whole captured area: the layer style sources it caches are reused
  // outside those cull rects, so they are recorded against this wider set instead.
  // alpha is the draw alpha of the consume pass: both sides push and read the same snapshots only
  // when a style is visible at the same alpha, so the replay must see the same value.
  static void Run(Layer* captureRoot, const DrawArgs& baseArgs,
                  std::shared_ptr<BackgroundSource> bgSource, BackgroundSnapshotMap* snapshots,
                  const std::vector<Rect>& renderRects, const std::vector<Rect>& styleSourceRects,
                  float alpha);

  // Exposes the snapshot map this capturer writes into, so per-fragment Sub3DCapturers spawned
  // by 3D subtrees can append their composed-backdrop entries to the same map under the same
  // (Layer, LayerStyle) keys that the consume pass will read back.
  BackgroundSnapshotMap* snapshotMap() const {
    return snapshots;
  }

  // Records the matrix that maps layer-local coordinates into the capture canvas space: world
  // space on the main surface, or the downsampled background-source pixel space during capture.
  // The offscreen content path records the layer into a canvas whose matrix starts at the
  // layer-local origin, so drawBackgroundStyle() cannot recover the layer's world placement
  // from canvas->getMatrix() there and uses this matrix instead. Set by the offscreen content
  // path right before the layer subtree is recorded.
  void setCaptureWorldMatrix(std::optional<Matrix> worldMatrix) {
    _captureWorldMatrix = std::move(worldMatrix);
  }

  const std::optional<Matrix>& captureWorldMatrix() const {
    return _captureWorldMatrix;
  }

 protected:
  // Returns true when `layer` itself draws a Background-sourced style or contains one that does.
  static bool HasDrawableBackdrop(Layer* layer, float alpha);

  virtual std::unique_ptr<BackgroundCapturer> makeSubCapturer(
      BackgroundSnapshotMap* snapshots, std::shared_ptr<BackgroundSource> source,
      const std::vector<Rect>* styleSourceRects) const;

  // A pass that shares style inputs with a later pass may need to keep the parent cull rects.
  virtual bool narrowSubRenderRects() const {
    return true;
  }

  void forceDrawDescendants() {
    ++_forcedCaptureDepth;
  }

  bool isForcedCapture() const {
    return _forcedCaptureDepth > 0;
  }

 private:
  std::unique_ptr<BackgroundCapturer> buildSubCapturer(
      const DrawArgs& parentArgs, std::shared_ptr<BackgroundSource> subSource) const;

  BackgroundSnapshotMap* snapshots = nullptr;
  std::shared_ptr<BackgroundSource> bgSource = nullptr;
  std::vector<Rect> _renderRects;
  const std::vector<Rect>* _styleSourceRects = nullptr;
  int _forcedCaptureDepth = 0;
  std::optional<Matrix> _captureWorldMatrix = std::nullopt;
};

class BackgroundConsumer : public BackgroundHandler {
 public:
  // A null snapshots pointer means the capture pass was skipped (picture-canvas path, no GPU
  // context). In that case the consumer synthesizes backdrops on the fly via
  // Layer::synthesizeBackgroundImage. When snapshots is non-null (surface path) the map is
  // authoritative — a miss there indicates a capture-side coverage bug, so we silently skip
  // rather than masking it with synthesis.
  // shareStyleOutput: the display list enables this only when the background color is fully
  // opaque (which is what makes SrcOver compositing of the cached output match the style's own
  // Src draw), the frame renders through multiple passes, and those passes cover compact render
  // rects, since that is when caching pays for its rasterization.
  // matchLiveBackdrop mirrors the capture pass that produced `snapshots` when it selects
  // surfaces from the live layer tree (the export target path). It makes this consumer take the
  // same offscreen route as the capturer for groups that contain drawable backdrop styles.
  BackgroundConsumer(BackgroundSnapshotMap* snapshots, bool shareStyleOutput,
                     bool matchLiveBackdrop = false)
      : snapshots(snapshots), shareStyleOutput(shareStyleOutput),
        matchLiveBackdrop(matchLiveBackdrop) {
  }

  void drawBackgroundStyle(const DrawArgs& args, Canvas* canvas, Layer* layer, float alpha,
                           LayerStyle* style, const LayerStyleSource* source) override;

  bool needsSurface(Layer* layer) const override;

  const LayerStyleSource* getCachedLayerStyleSource(Layer* layer) const override;

 private:
  BackgroundSnapshotMap* snapshots = nullptr;
  bool shareStyleOutput = false;
  bool matchLiveBackdrop = false;
  // Per-consumer read cursors: each (Layer, LayerStyle) entry list is consumed in dispatch order
  // through this map, so a shared snapshot map can be consumed by multiple consumers (e.g. one
  // per tile in tiled rendering) without cursors interfering.
  std::unordered_map<BackgroundSnapshotKey, std::size_t, BackgroundSnapshotKeyHash> readCursors;
};

}  // namespace tgfx

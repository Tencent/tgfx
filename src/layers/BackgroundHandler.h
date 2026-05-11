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
#include <vector>
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

class BackgroundSource;
class Canvas;
class DrawArgs;
class Layer;
class LayerStyle;
class Surface;
struct BackgroundSnapshotMap;
struct LayerStyleSource;

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

  // Returns true when descendants of `layer` should render onto a real Surface so a sub
  // background source can sample them back. Capturer returns true only when `layer` actually has
  // descendant background-sourced styles AND a GPU context is available; Consumer / NoOp always
  // return false.
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

  // Returns the index of the last child in parent's children list that should be traversed
  // during the capture pass, or -1 if no children need traversal. BackgroundCapturer overrides
  // this to skip children after the last one with background styles.
  virtual int lastCaptureChildIndex(const Layer* /*parent*/) const;

  // Returns a previously cached LayerStyleSource for the given layer, or nullptr if none. Capture
  // and consume passes walk the same tree, so the source built in capture is reused in consume —
  // avoiding redundant content/contour image renders. Default implementation returns nullptr so
  // NoOp handlers don't engage caching.
  virtual const LayerStyleSource* getCachedLayerStyleSource(Layer* /*layer*/) const {
    return nullptr;
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

  // During the capture pass, a subtree that sits before a later bg-style sibling must draw all
  // descendants so their content appears on the bg source surface. drawChildren wraps each such
  // child draw in begin/endForceDrawChildren. Non-capturing handlers default to no-ops, so
  // callers can invoke these unconditionally.
  virtual void beginForceDrawChildren() {
  }
  virtual void endForceDrawChildren() {
  }
};

class BackgroundCapturer : public BackgroundHandler {
 public:
  BackgroundCapturer(BackgroundSnapshotMap* snapshots, std::shared_ptr<BackgroundSource> source)
      : snapshots(snapshots), bgSource(std::move(source)) {
  }

  void drawBackgroundStyle(const DrawArgs& args, Canvas* canvas, Layer* layer, float alpha,
                           LayerStyle* style, const LayerStyleSource* source) override;

  bool needsSurface(Layer* layer) const override;

  std::unique_ptr<BackgroundHandler> createSubHandler(Surface* surface, const DrawArgs& parentArgs,
                                                      const Rect& localBounds,
                                                      const Matrix& localToWorld,
                                                      const Matrix& localToSurface) const override;

  const std::vector<Rect>* renderRects() const override {
    return _renderRects.empty() ? nullptr : &_renderRects;
  }

  int lastCaptureChildIndex(const Layer* parent) const override;

  const LayerStyleSource* getCachedLayerStyleSource(Layer* layer) const override;

  bool canCacheLayerStyleSource(Layer* layer) const override;

  const LayerStyleSource* cacheLayerStyleSource(Layer* layer,
                                                std::unique_ptr<LayerStyleSource> source) override;

  void beginForceDrawChildren() override {
    ++_forcedCaptureDepth;
  }
  void endForceDrawChildren() override {
    --_forcedCaptureDepth;
  }

  // Runs a capture pass: replays captureRoot onto bgSource's canvas, populating snapshots. The
  // replay walks the tree exactly once with renderRects set to the pre-outset world-space dirty
  // rects. drawLayer culls each layer whose renderBounds does not intersect any of these rects.
  static void Run(Layer* captureRoot, const DrawArgs& baseArgs,
                  std::shared_ptr<BackgroundSource> bgSource, BackgroundSnapshotMap* snapshots,
                  const std::vector<Rect>& renderRects);

 private:
  bool isForcedCapture() const {
    return _forcedCaptureDepth > 0;
  }

  std::unique_ptr<BackgroundCapturer> buildSubCapturer(
      const DrawArgs& parentArgs, std::shared_ptr<BackgroundSource> subSource) const;

  BackgroundSnapshotMap* snapshots = nullptr;
  std::shared_ptr<BackgroundSource> bgSource = nullptr;
  std::vector<Rect> _renderRects;
  int _forcedCaptureDepth = 0;
};

class BackgroundConsumer : public BackgroundHandler {
 public:
  explicit BackgroundConsumer(const BackgroundSnapshotMap* snapshots) : snapshots(snapshots) {
  }

  void drawBackgroundStyle(const DrawArgs& args, Canvas* canvas, Layer* layer, float alpha,
                           LayerStyle* style, const LayerStyleSource* source) override;

  const LayerStyleSource* getCachedLayerStyleSource(Layer* layer) const override;

 private:
  const BackgroundSnapshotMap* snapshots = nullptr;
};

}  // namespace tgfx

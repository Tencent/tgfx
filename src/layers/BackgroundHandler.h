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
#include "tgfx/core/Rect.h"

namespace tgfx {

class BackgroundSource;
class Canvas;
class DrawArgs;
class Layer;
class LayerStyle;
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

  // Returns the source this handler samples from (BackgroundCapturer), or nullptr otherwise.
  virtual BackgroundSource* source() const {
    return nullptr;
  }

  // Returns the index of the last child in parent's children list that should be traversed
  // during the capture pass, or -1 if no children need traversal. BackgroundCapturer overrides
  // this to skip children after the last one with background styles; when isForcedCapture() is
  // true it returns the last index so all children are drawn.
  virtual int lastCaptureChildIndex(const Layer* /*parent*/) const;

  // Returns a handler of the same type sampling from newSource, or nullptr for non-sampling
  // handlers so the caller keeps the current handler. Used by OffscreenRenderer to stack a child
  // handler when descending into an offscreen subtree.
  virtual std::unique_ptr<BackgroundHandler> cloneWithSource(
      std::shared_ptr<BackgroundSource> /*newSource*/) const {
    return nullptr;
  }

  // Returns a previously cached LayerStyleSource for the given layer, or nullptr if none. Capture
  // and consume passes walk the same tree, so the source built in capture is reused in consume —
  // avoiding redundant content/contour image renders. Default implementation returns nullptr so
  // NoOp handlers don't engage caching.
  virtual const LayerStyleSource* getCachedLayerStyleSource(Layer* /*layer*/) const {
    return nullptr;
  }

  // Tries to take ownership of `source` and cache it under `layer`. On success, returns the raw
  // pointer to the cached source and leaves `source` empty. On failure (e.g. NoOp handler or
  // capturer with non-1 surfaceScale where capture/consume densities diverge), returns nullptr
  // and leaves `source` untouched so the caller keeps managing its lifetime.
  virtual const LayerStyleSource* tryCacheLayerStyleSource(
      Layer* /*layer*/, std::unique_ptr<LayerStyleSource>& /*source*/) const {
    return nullptr;
  }

  // During the capture pass, a subtree that sits before a later bg-style sibling must draw
  // all descendants so their content appears on the bg source surface. beginForcedCapture() /
  // endForcedCapture() manage a nesting depth counter so recursive drawChildren calls don't
  // clobber the forced state.
  bool isForcedCapture() const {
    return _forcedCaptureDepth > 0;
  }
  void beginForcedCapture() const {
    ++_forcedCaptureDepth;
  }
  void endForcedCapture() const {
    --_forcedCaptureDepth;
  }

 private:
  mutable int _forcedCaptureDepth = 0;
};

class BackgroundCapturer : public BackgroundHandler {
 public:
  BackgroundCapturer(BackgroundSnapshotMap* snapshots, std::shared_ptr<BackgroundSource> source)
      : snapshots(snapshots), bgSource(std::move(source)) {
  }

  void drawBackgroundStyle(const DrawArgs& args, Canvas* canvas, Layer* layer, float alpha,
                           LayerStyle* style, const LayerStyleSource* source) override;

  BackgroundSource* source() const override {
    return bgSource.get();
  }

  int lastCaptureChildIndex(const Layer* parent) const override;

  std::unique_ptr<BackgroundHandler> cloneWithSource(
      std::shared_ptr<BackgroundSource> newSource) const override;

  const LayerStyleSource* getCachedLayerStyleSource(Layer* layer) const override;

  const LayerStyleSource* tryCacheLayerStyleSource(
      Layer* layer, std::unique_ptr<LayerStyleSource>& source) const override;

  // Runs a capture pass: replays captureRoot onto bgSource's canvas, populating snapshots. The
  // replay walks the tree exactly once with renderRects set to the pre-outset world-space dirty
  // rects. drawLayer culls each layer whose renderBounds does not intersect any of these rects.
  static void Run(Layer* captureRoot, const DrawArgs& baseArgs,
                  std::shared_ptr<BackgroundSource> bgSource, BackgroundSnapshotMap* snapshots,
                  const std::vector<Rect>& renderRects);

 private:
  BackgroundSnapshotMap* snapshots = nullptr;
  std::shared_ptr<BackgroundSource> bgSource = nullptr;
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

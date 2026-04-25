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
#include "tgfx/core/Matrix.h"
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
 * BackgroundHandler owns the policy for rendering Background-sourced LayerStyles during a single
 * render pass. Three concrete handlers exist:
 *   - BackgroundCapturer: holds a BackgroundSource, samples it into a snapshot map at each
 *     in-scope style so the consumer can replay stable pixels later.
 *   - BackgroundConsumer: holds the snapshot map and blits matched snapshots through the layer
 *     style.
 *   - NoOp: silently skips Background-sourced styles (intermediate artifacts / 3D subtrees).
 *
 * The active handler lives on DrawArgs::background::handler. OffscreenRenderer may stack a child
 * handler for an offscreen subtree by calling source() to read the parent's BackgroundSource,
 * deriving a sub source off the carrier's backing store, and asking the parent to
 * cloneWithSource(sub).
 */
class BackgroundHandler {
 public:
  virtual ~BackgroundHandler() = default;

  static BackgroundHandler* NoOp();

  /**
   * Dispatches a Background-sourced layer style through the current handler in `args.background`.
   * If no handler is installed, falls back to NoOp (silently skips). Lets callers render
   * Background-sourced styles without knowing anything about handler types or NoOp semantics.
   */
  static void DispatchOrSkip(const DrawArgs& args, Canvas* canvas, Layer* layer, float alpha,
                             LayerStyle* style, const LayerStyleSource* source);

  virtual void drawBackgroundStyle(const DrawArgs& args, Canvas* canvas, Layer* layer, float alpha,
                                   LayerStyle* style, const LayerStyleSource* source) = 0;

  /**
   * Returns the BackgroundSource this handler currently samples from, or nullptr if the handler
   * has no sampling need (BackgroundConsumer / NoOp). OffscreenRenderer reads this to decide
   * whether to bother deriving a sub source.
   */
  virtual BackgroundSource* source() const {
    return nullptr;
  }

  /**
   * Produces a new handler of the same concrete type, but sampling from `newSource` instead.
   * OffscreenRenderer calls this when descending into an offscreen subtree, after deriving a
   * sub BackgroundSource off the carrier's backing store. Handlers that don't sample
   * (BackgroundConsumer / NoOp) return nullptr so the caller keeps the current handler.
   */
  virtual std::unique_ptr<BackgroundHandler> cloneWithSource(
      std::shared_ptr<BackgroundSource> /*newSource*/) const {
    return nullptr;
  }
};

class BackgroundCapturer : public BackgroundHandler {
 public:
  BackgroundCapturer(BackgroundSnapshotMap* snapshots, float baseCaptureScale,
                     std::shared_ptr<BackgroundSource> source)
      : snapshots(snapshots), baseCaptureScale(baseCaptureScale), bgSource(std::move(source)) {
  }

  void drawBackgroundStyle(const DrawArgs& args, Canvas* canvas, Layer* layer, float alpha,
                           LayerStyle* style, const LayerStyleSource* source) override;

  BackgroundSource* source() const override {
    return bgSource.get();
  }

  std::unique_ptr<BackgroundHandler> cloneWithSource(
      std::shared_ptr<BackgroundSource> newSource) const override;

  /**
   * Runs a single capture pass: replays `captureRoot` onto `bgSource`'s canvas so every in-scope
   * Background-sourced LayerStyle samples the live background into `snapshots`.
   */
  static void Run(Layer* captureRoot, const DrawArgs& baseArgs,
                  std::shared_ptr<BackgroundSource> bgSource, float captureScale,
                  BackgroundSnapshotMap* snapshots);

 private:
  BackgroundSnapshotMap* snapshots = nullptr;
  float baseCaptureScale = 1.0f;
  std::shared_ptr<BackgroundSource> bgSource = nullptr;
};

class BackgroundConsumer : public BackgroundHandler {
 public:
  explicit BackgroundConsumer(const BackgroundSnapshotMap* snapshots) : snapshots(snapshots) {
  }

  void drawBackgroundStyle(const DrawArgs& args, Canvas* canvas, Layer* layer, float alpha,
                           LayerStyle* style, const LayerStyleSource* source) override;

 private:
  const BackgroundSnapshotMap* snapshots = nullptr;
};

}  // namespace tgfx

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

  // Returns a handler of the same type sampling from newSource, or nullptr for non-sampling
  // handlers so the caller keeps the current handler. Used by OffscreenRenderer to stack a child
  // handler when descending into an offscreen subtree.
  virtual std::unique_ptr<BackgroundHandler> cloneWithSource(
      std::shared_ptr<BackgroundSource> /*newSource*/) const {
    return nullptr;
  }
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

  std::unique_ptr<BackgroundHandler> cloneWithSource(
      std::shared_ptr<BackgroundSource> newSource) const override;

  // Runs a capture pass: replays captureRoot onto bgSource's canvas, populating snapshots. When
  // drawRects contains more than one entry, the replay is invoked once per rect with that rect
  // as the renderRect — that avoids replaying Layers that fall in the bounding gaps between
  // dirty regions. The bg source itself is still a single rectangle (sized to the rects' union)
  // because it is backed by a real Surface or Picture; only the cull window varies per pass.
  static void Run(Layer* captureRoot, const DrawArgs& baseArgs,
                  std::shared_ptr<BackgroundSource> bgSource, BackgroundSnapshotMap* snapshots,
                  const std::vector<Rect>& drawRects);

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

 private:
  const BackgroundSnapshotMap* snapshots = nullptr;
};

}  // namespace tgfx

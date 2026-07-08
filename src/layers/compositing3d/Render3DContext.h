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
#include <unordered_map>
#include <vector>
#include "Layer3DContext.h"
#include "tgfx/core/Image.h"

namespace tgfx {

struct BackgroundSnapshotMap;
class BackgroundSource;
class Context3DCompositor;
class DrawArgs;

/**
 * Render3DContext composites layers using BSP-based depth sorting. addLayer recursively expands
 * the preserve3D subtree, collecting one node per registered layer. finishAndDrawTo builds a BSP
 * tree, rasterizes each layer (per-fragment when the subtree contains BackgroundBlur, otherwise
 * once per layer), then composites the fragments back-to-front into the compositor target. When
 * BackgroundBlur is present, the target is primed with the outer canvas snapshot so any
 * dispatch sees both "behind 3D" and "BSP-accumulated" pixels as its backdrop.
 */
class Render3DContext : public Layer3DContext {
 public:
  Render3DContext(std::shared_ptr<Context3DCompositor> compositor, const Rect& renderRect,
                  float contentScale, std::shared_ptr<ColorSpace> colorSpace);

  void finishAndDrawTo(const DrawArgs& args, Canvas* canvas) override;

 private:
  struct PendingNode {
    Layer* layer = nullptr;
    Matrix3D transform = {};
    Rect localBounds = {};
    int depth = 0;
    float alpha = 1.0f;
    bool antialiasing = true;
    bool hasBackgroundStyle = false;
  };

  void emitNode(Layer* layer, const Rect& localBounds, const Matrix3D& transform, float alpha,
                int depth, bool hasBackgroundStyle) override;
  // Rasters the layer onto a fresh leaf surface and returns the snapshot. When `compositorSource`
  // is non-null, builds a sub BackgroundSource on the leaf surface (parented to compositorSource)
  // and installs a fresh BackgroundCapturer on `leafArgs.backgroundHandler` so in-fragment
  // dispatches and any nested offscreen handlers walk the standard BackgroundCapturer pipeline.
  // `localToWorld` is the layer's matrix in this context's world space (== outer canvas-local at
  // the top level; == enclosing leaf's local for nested 3D contexts).
  std::shared_ptr<Image> rasterLayer(Layer* layer, const Rect& localBounds, float alpha,
                                     BlendMode blendMode, DrawArgs& leafArgs,
                                     const std::shared_ptr<BackgroundSource>& compositorSource,
                                     BackgroundSnapshotMap* snapshots, const Matrix& localToWorld);
  // Snapshot the outer canvas and prime the compositor target with it so in-subtree
  // BackgroundBlur dispatches can sample "outside the 3D subtree" content as part of their
  // backdrop. Returns true on success; returns false (with a LOGW for the no-surface case)
  // when the outer canvas has no surface or the snapshot can't be remapped into compositor
  // pixel space — the caller must skip compositorSource construction so blur falls back
  // cleanly rather than consuming an un-primed compositor.
  bool primeCompositorFromOuterCanvas(Canvas* outerCanvas);

  std::shared_ptr<Context3DCompositor> _compositor = nullptr;
  std::vector<PendingNode> _pendingNodes = {};
  // OR of hasBackgroundStyle across every collected node; gates outer-canvas priming and
  // capturer setup in finishAndDrawTo.
  bool _subtreeNeedsBackdrop = false;
};

}  // namespace tgfx

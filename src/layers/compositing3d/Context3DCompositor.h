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

#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>
#include "BspTree.h"
#include "DrawPolygon3D.h"
#include "core/utils/PlacementPtr.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/core/Image.h"

namespace tgfx {

class Layer;

/**
 * Context3DCompositor handles compositing of 3D transformed images using BSP tree for correct
 * depth sorting. It splits intersecting regions to ensure correct occlusion and blending order.
 *
 * Usage: addPolygon for each registered node, then prepareTraversal to build the BSP tree and
 * obtain fragments in back-to-front order. Caller rasterizes images on demand and feeds them via
 * drawPolygon. Finally flushToImage produces the composited image.
 */
class Context3DCompositor {
 public:
  Context3DCompositor(const Context& context, int width, int height);

  int width() const {
    return _width;
  }

  int height() const {
    return _height;
  }

  /**
   * Adds a polygon for compositing. Geometry only — the layer's image is not produced here.
   * @param layer The layer this polygon represents (used as image cache key during traversal).
   * @param localBounds Layer-local bounds used to derive the polygon's corners.
   * @param matrix The 3D transformation applied to the local-space corners.
   * @param depth The depth level in the layer tree (used for sorting coplanar polygons).
   * @param alpha The layer alpha for transparency.
   * @param antiAlias Whether to enable edge antialiasing when MSAA is unavailable.
   */
  void addPolygon(Layer* layer, const Rect& localBounds, const Matrix3D& matrix, int depth,
                  float alpha, bool antiAlias);

  /**
   * Builds the BSP tree from added polygons and returns fragments in back-to-front paint order.
   * The returned vector is owned by the compositor; pointers stay valid until flushToImage.
   * Each fragment carries a Layer* identifying which layer's raster image should be sampled when
   * passed back via drawPolygon.
   */
  const std::vector<DrawPolygon3D*>& prepareTraversal();

  /**
   * Draws a single fragment (returned by prepareTraversal) with its supplied raster image.
   */
  void drawPolygon(const DrawPolygon3D* polygon, const std::shared_ptr<Image>& image);

  /**
   * Returns an immutable snapshot of the compositor's render target with all currently queued
   * draw ops applied. Used by 3D-subtree BackgroundBlur to feed the in-progress BSP-accumulated
   * pixels as part of the per-fragment backdrop. The returned Image is independent of subsequent
   * draws (a copy task is enqueued).
   */
  std::shared_ptr<Image> snapshotTarget();

  /**
   * Lays down a target-sized image as the very first draw on the compositor target so that
   * subsequent BSP fragments composite on top of it. Used by Render3DContext to inject the
   * outer-canvas snapshot before BSP raster, giving in-subtree BackgroundBlur dispatches a
   * backdrop that includes "behind 3D subtree" content.
   *
   * @param image A target-sized image in compositor pixel space.
   */
  void primeWithImage(const std::shared_ptr<Image>& image);

  /**
   * Flushes accumulated draw ops to the offscreen target and returns the composited image.
   */
  std::shared_ptr<Image> flushToImage();

 private:
  void drawQuads(const DrawPolygon3D* polygon, const std::shared_ptr<Image>& image,
                 const std::vector<Quad>& subQuads);

  int _width = 0;
  int _height = 0;
  std::shared_ptr<RenderTargetProxy> _targetColorProxy = nullptr;
  std::deque<std::unique_ptr<DrawPolygon3D>> _polygons = {};
  std::unique_ptr<BspTree> _bspTree = nullptr;
  std::vector<DrawPolygon3D*> _orderedFragments = {};
  std::vector<PlacementPtr<DrawOp>> _drawOps = {};
  std::unordered_map<int, int> _depthSequenceCounters = {};
  // Tracks whether at least one OpsRenderTask has been submitted to _targetColorProxy. The first
  // submission must clear the target; subsequent submissions append on top.
  bool _targetCleared = false;
};

}  // namespace tgfx

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
#include "gpu/AOTEffectDecomposer.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/tasks/RenderTask.h"

namespace tgfx {
class Context;
class RenderTargetProxy;

class AOTPlanExecutor {
 public:
  /** Returns whether the current production executor can preserve this plan's semantics. */
  static bool CanExecute(const AOTEffectGraph& graph, const AOTEffectPlan& plan);

  /**
   * Flattens a single-pass PointwiseChain plan into the fused chain processor, or returns nullptr
   * if the pass shape exceeds what the chain kernel can represent. The returned processor carries
   * the whole DAG in uniforms, so a draw whose color processors are replaced by it evaluates the
   * same pixels through the precompiled PointwiseChainShader. Used both by the offscreen executor
   * and by the direct-draw decomposition route in DrawOp::prepare.
   *
   * coverageFP, when non-null, folds the draw's coverage into the chain: a bare AARectEffect
   * becomes an AARectCoverage slot, an alpha-only DeviceSpaceTextureEffect becomes the mask child,
   * and Compose(mask, rect) becomes both. Any other coverage form returns nullptr.
   */
  static PlacementPtr<FragmentProcessor> BuildChainProcessor(
      BlockAllocator* allocator, const AOTEffectGraph& graph, const AOTPassDescriptor& pass,
      const FragmentProcessor* coverageFP = nullptr);

  /**
   * Builds an atomic render task for a supported linear AOT plan without enqueueing it. On failure,
   * originalDraw remains unchanged. On success, ownership of originalDraw moves into the task.
   *
   * deviceBounds is the area the original draw covers on the destination, in destination device
   * coordinates. fpCoordOffset is the translation the draw's geometry applies between destination
   * device coordinates and the coordinate space the fragment processors evaluate in: zero for an
   * OpsCompositor draw (FP local == device), or the caller's coordOffset for an offscreen fill
   * (DrawingManager::fillRTWithFP), whose draw always lands at (0,0,w,h) while its processors see
   * coords shifted by that offset. Intermediate passes add it to their fill offset so the rebuilt
   * chain evaluates in the same space the original processors would have seen; the terminal
   * sampling matrix is independent of it.
   */
  static PlacementPtr<RenderTask> Make(Context* context, uint32_t renderFlags,
                                       const AOTEffectGraph& graph, const AOTEffectPlan& plan,
                                       const Rect& deviceBounds,
                                       std::shared_ptr<RenderTargetProxy> destination,
                                       PlacementPtr<DrawOp>* originalDraw,
                                       const Point& fpCoordOffset);
};
}  // namespace tgfx

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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <vector>
#include "gpu/AOTMaterializationPolicy.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {

/**
 * The coordinate space a materialized edge is captured in, which decides how its consumer samples
 * it back.
 */
enum class AOTEdgeSpace {
  /// Captured over the destination's device bounds. The consumer reads it at its own device pixel
  /// (DeviceSpaceTextureEffect), so producer and consumer stay pixel-aligned and no apron is
  /// needed. Used by the passes a multi-pass linear plan splits off.
  Device,
  /// Captured in the local coordinate space of the subtree it replaces. The consumer reads it with a
  /// TextureEffect at the subtree's local coordinates, which can land just outside the captured
  /// area once it is snapped to integers, so these edges carry an apron. Used by blend-child
  /// materialization.
  Local,
};

/**
 * Why an edge exists, which decides how the draw stats count it: the multi-pass linear planner's
 * device-space splits (planMaterializedEdges) versus a materialized blend operand
 * (fpFlattenEdges, the same counter the retry's local-space producers feed).
 */
enum class AOTEdgeReason {
  /// A pass the multi-pass linear planner split off.
  Split,
  /// A blend operand subtree the planner materialized (budget-driven).
  OperandMaterialize,
};

/**
 * The contract of one materialized edge: where its texels live and how much area it captures. Every
 * edge is an RGBA8, top-left-origin, exact-fit target allocated by AOTMaterializationPolicy, so
 * crossing an edge rounds the producer's output to the 1/255 grid.
 */
struct AOTMaterializedEdge {
  AOTEdgeSpace space = AOTEdgeSpace::Device;
  AOTEdgeReason reason = AOTEdgeReason::Split;
  /// The captured area and the coordinate offset the producer renders with.
  MaterializedGeometry geometry = {};
  /// The outset already applied to geometry.bounds (see MaterializationDecision::apronRadius).
  float apronRadius = 0.0f;
};

/**
 * One producer pass of an AOTRenderPlan: a fill draw that renders a materialized edge into its own
 * offscreen target. The target is cleared to transparent before the draw. A pass may sample the
 * targets of any earlier pass in the same plan, never a later one.
 */
struct AOTRenderPass {
  std::shared_ptr<RenderTargetProxy> target = nullptr;
  /// The fill draw (DrawingManager::makeFillDrawOp) that renders the edge into target.
  PlacementPtr<DrawOp> drawOp = nullptr;
  AOTMaterializedEdge edge = {};
};

/**
 * The executable form of an AOT route for one draw: the producer passes, ordered so that every pass
 * comes after the passes whose targets it samples, then the terminal pass, which replays the original draw onto its destination with terminalColors in
 * place of its color processors. The terminal pass may sample any producer. AOTPlanRenderTask runs
 * the whole plan as one task, so a producer and the passes that read it can never be split across
 * tasks.
 */
struct AOTRenderPlan {
  std::vector<AOTRenderPass> passes = {};
  DrawOp::ColorProcessorList terminalColors = {};

  /**
   * Whether every pass has a target and a draw, and the terminal pass has color processors. The
   * pass order is the builder's responsibility and is not checked here.
   */
  bool isWellFormed() const {
    for (const auto& pass : passes) {
      if (pass.target == nullptr || pass.drawOp == nullptr) {
        return false;
      }
    }
    return !terminalColors.empty();
  }
};
}  // namespace tgfx

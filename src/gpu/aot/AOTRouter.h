/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <optional>
#include "core/utils/PlacementArray.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

class RenderTask;
class StandardDrawOp;
struct AOTCoverageTerm;
struct DrawIR;

/**
 * What the router is asked to route: an on-screen draw (whose device bounds gate the plan route)
 * or an offscreen fill (whose whole color expression and fresh-target clear it carries).
 */
struct AOTRouteRequest {
  enum class Kind : uint8_t { Draw, Fill };
  Kind kind = Kind::Draw;
  std::shared_ptr<RenderTargetProxy> target = nullptr;
  /// Draw only: the plan route's offscreen targets need a size, so a draw without bounds keeps
  /// the plain route. A fill leaves it empty — its bounds are the target's logical size.
  std::optional<Rect> deviceBounds = std::nullopt;
  /// Fill only: the offset the fill's processors evaluate their coordinates at.
  Point coordOffset = Point::Zero();
  /// Draw only: the draw's blend mode, which the task-coverage gate reads.
  BlendMode blend = BlendMode::SrcOver;
  uint32_t renderFlags = 0;
  /// Fill only: an offscreen fill owns a fresh target whose whole backing must start transparent.
  bool clearDestination = false;
  /// Fill only: the fill's whole color expression, read before the caller moves its owner onto
  /// the draw op.
  const FragmentProcessor* processor = nullptr;
};

/**
 * The record-time AOT route's one entry: it lowers the op once, plans it and dispatches it, in
 * one place instead of one hand-written copy per entry point (OpsCompositor's draw recording and
 * DrawingManager::fillRTWithFP). Returns the plan task — which now owns the op — when the plan
 * needs several passes; the caller submits it and regenerates the target's mipmaps. Returns null
 * otherwise: the op is then either untouched or annotated in place (its record-time IR, or the
 * single in-place pass's descriptor) for the prepare-time serving. A fill's IR is unconditional —
 * the plain matcher reads it for bare single-source fills too — so a fill op may leave annotated
 * even when no plan was made.
 */
class AOTRouter {
 public:
  explicit AOTRouter(Context* context);

  PlacementPtr<RenderTask> route(PlacementPtr<DrawOp>* op, const AOTRouteRequest& request);

  /**
   * The batch-submission backstop: lowers ops that reach an ops task without passing through the
   * compositor's draw recording (a clip's mask layer, a layer's own render), so their IR exists
   * before the task prepares them. Idempotent — ops that already lowered on the record route cost
   * one branch. Stencil covers draw no program and stay unlowered.
   */
  void lowerOps(PlacementArray<DrawOp>& ops);

 private:
  PlacementPtr<RenderTask> routeDraw(PlacementPtr<DrawOp>* op, const AOTRouteRequest& request);
  PlacementPtr<RenderTask> routeFill(PlacementPtr<DrawOp>* op, const AOTRouteRequest& request);

  /// The decision tail both routes share: one fused pass wins no plan, otherwise the planners'
  /// verdict decides whether a task is built. `blend` and `terminalCoverage` are the two inputs
  /// the routes disagree on; `localRemapSource` is the draw whose device-to-local map a multi-pass
  /// fill needs (null for an offscreen fill, whose local coordinates are device plus coordOffset).
  /// `planMissed`, when given, reports that the planners produced nothing — the draw side records
  /// it as an unsupported shape; a fused pass is not a miss.
  PlacementPtr<RenderTask> routePlan(const DrawIR* ir, const AOTRouteRequest& request,
                                     BlendMode blend,
                                     const std::vector<AOTCoverageTerm>* terminalCoverage,
                                     StandardDrawOp* localRemapSource, PlacementPtr<DrawOp>* op,
                                     bool* planMissed = nullptr);

  PlacementPtr<RenderTask> buildPlanTask(const AOTEffectGraph& graph, const Plan& plan,
                                         const AOTRouteRequest& request,
                                         const std::optional<Matrix>& deviceToLocal,
                                         const std::vector<AOTCoverageTerm>* terminalCoverage,
                                         PlacementPtr<DrawOp>* op);

  Context* _context = nullptr;
};

}  // namespace tgfx

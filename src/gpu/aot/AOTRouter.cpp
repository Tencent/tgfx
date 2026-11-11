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

#include "AOTRouter.h"
#include <optional>
#include <utility>
#include <vector>
#include "gpu/ContextInternal.h"
#include "gpu/DrawingManager.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/plan/AOTPlanner.h"
#include "gpu/ops/StandardDrawOp.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

AOTRouter::AOTRouter(Context* context) : _context(context) {
}

PlacementPtr<RenderTask> AOTRouter::route(PlacementPtr<DrawOp>* op,
                                          const AOTRouteRequest& request) {
  if (request.kind == AOTRouteRequest::Kind::Fill) {
    return routeFill(op, request);
  }
  return routeDraw(op, request);
}

void AOTRouter::lowerOps(PlacementArray<DrawOp>& ops) {
  auto cache = _context != nullptr ? ContextCacheAccess::Cache(_context) : nullptr;
  if (cache == nullptr || !cache->isLoaded()) {
    return;
  }
  for (auto& op : ops) {
    if (op != nullptr) {
      op->tryLowerIR();
    }
  }
}

PlacementPtr<RenderTask> AOTRouter::routePlan(const DrawIR* ir, const AOTRouteRequest& request,
                                              BlendMode blend,
                                              const std::vector<AOTCoverageTerm>* terminalCoverage,
                                              StandardDrawOp* localRemapSource,
                                              PlacementPtr<DrawOp>* op, bool* planMissed) {
  // A graph one fused pass can carry needs no plan at all: prepare compiles that pass from the
  // graph itself, which is the same decision the DAG planner would make.
  PassIR fused = {};
  if (AOTChainBuilder::PlanFusedPass(ir->color, &fused)) {
    return nullptr;
  }
  auto plan = AOTPlanner::MakePlan(*ir);
  if (!plan.has_value()) {
    if (planMissed != nullptr) {
      *planMissed = true;
    }
    return nullptr;
  }
  // A fill pass carrying local-coordinate leaves (a plain texture, a gradient, a noise source)
  // evaluates them at top-left device positions and needs one affine device-to-local map; the
  // uniform-geometry-color requirement is enforced inside AOTPlanner::Build. An offscreen fill
  // needs none: its local coordinates are exactly device plus coordOffset, which Build expresses
  // as an absent map.
  std::optional<Matrix> deviceToLocal = std::nullopt;
  if (localRemapSource != nullptr && plan->passes.size() > 1 &&
      AOTPlanner::NeedsLocalRemap(ir->color, *plan)) {
    deviceToLocal = localRemapSource->deviceToLocalMatrix();
    if (!deviceToLocal.has_value()) {
      return nullptr;
    }
  }
  const std::vector<AOTCoverageTerm> noCoverage = {};
  const auto& coverage = terminalCoverage != nullptr ? *terminalCoverage : noCoverage;
  if (AOTPlanner::RouteRecordPlan(*plan, coverage, blend) != AOTRecordRoute::Task) {
    // Anything else keeps the plain route: the matcher serves it at prepare time.
    return nullptr;
  }
  return buildPlanTask(ir->color, *plan, request, deviceToLocal, terminalCoverage, op);
}

PlacementPtr<RenderTask> AOTRouter::routeDraw(PlacementPtr<DrawOp>* op,
                                              const AOTRouteRequest& request) {
  auto* drawOp = op->get();
  auto cache = ContextCacheAccess::Cache(_context);
  if (cache == nullptr || !cache->isLoaded()) {
    return nullptr;
  }
  // The single record-time lowering: the op's whole expression (colors, coverages, xfer, blend
  // mode) lands on a DrawIR every later stage converges on, so the prepare-time serving never
  // lowers a second time. Device bounds are a plan-task input only (its offscreen targets need a
  // size), so a draw without bounds keeps its record-time IR on the plain route — coverage never
  // folds into the color graph: a multi-pass plan carries the classified terms on its terminal
  // pass, and a single-pass plan stays on the plain route for the prepare-time walk.
  drawOp->tryLowerIR();
  const bool coverageFree = !drawOp->hasCoverageProcessors();
  if (!cache->aotPolicy().decomposition || !request.deviceBounds.has_value() ||
      request.deviceBounds->isEmpty()) {
    return nullptr;
  }
  auto* ir = drawOp->loweredIR();
  if (ir == nullptr) {
    // The color side failed to lower: the same category the FP-walk route recorded, without a
    // second analysis pass over the processor tree. Atlas-text ops never attempt the lowering
    // (their glyph leaf needs the geometry processor), so they record nothing here — as they
    // never did. A draw still carrying coverage processors records nothing either: its route was
    // never a color-only one.
    if (coverageFree && drawOp->type() != DrawOp::Type::AtlasTextOp &&
        cache->diagnosticRecordingEnabled()) {
      cache->recordDecomposeRejection(AOTDecomposeOutcome::BlockedByLowering);
    }
    return nullptr;
  }
  bool planMissed = false;
  auto task = routePlan(ir, request, request.blend, &ir->coverage,
                        static_cast<StandardDrawOp*>(drawOp), op, &planMissed);
  if (planMissed && coverageFree && cache->diagnosticRecordingEnabled()) {
    // The color side lowered but no planner covers it — the same fact the color-only attempt
    // recorded. A draw still carrying coverage processors records nothing: its route was never a
    // color-only one, and classifying it would require the analysis the production path does not
    // run.
    cache->recordDecomposeRejection(AOTDecomposeOutcome::UnsupportedShape);
  }
  return task;
}

PlacementPtr<RenderTask> AOTRouter::routeFill(PlacementPtr<DrawOp>* op,
                                              const AOTRouteRequest& request) {
  auto cache = ContextCacheAccess::Cache(_context);
  const bool precompiledActive = cache != nullptr && cache->isLoaded();
  if (precompiledActive && request.processor != nullptr) {
    // The fill's single record-time lowering: its whole color expression lands on a DrawIR the op
    // carries, and the route below plans from it — no second analysis over the FP tree. A fill
    // has no coverage and no xfer, and its blend is Src. The IR is unconditional — the
    // executor-table matching reads it for bare single-source fills too (the direct matchers no
    // longer walk the tree) — while the plan route below stays gated to composite trees: a bare
    // fill's single pass serves in place, exactly as before.
    auto allocator = _context->drawingManager()->drawingAllocator();
    if (auto lowered = LowerDraw({request.processor}, {}, BlendMode::Src, allocator)) {
      op->get()->adoptLoweredIR(allocator->make<DrawIR>(std::move(*lowered)));
    }
  }
  // The plan route only pays off for chains the plain route cannot match directly: a composite
  // processor tree (Compose/Xfermode children). A bare single-source fill is already served by
  // the direct matchers, so it must skip the route and keep its single program lookup.
  const bool planCandidate = precompiledActive && cache->aotPolicy().decomposition &&
                             request.processor != nullptr &&
                             request.processor->numChildProcessors() > 0;
  auto* ir = planCandidate ? op->get()->loweredIR() : nullptr;
  if (ir == nullptr) {
    return nullptr;
  }
  return routePlan(ir, request, BlendMode::Src, nullptr, nullptr, op);
}

PlacementPtr<RenderTask> AOTRouter::buildPlanTask(
    const AOTEffectGraph& graph, const Plan& plan, const AOTRouteRequest& request,
    const std::optional<Matrix>& deviceToLocal,
    const std::vector<AOTCoverageTerm>* terminalCoverage, PlacementPtr<DrawOp>* op) {
  // The fill covers its target's logical bounds, and its processors see coordinates shifted by
  // coordOffset; the split-off passes of a multi-pass plan reproduce both. A draw reaches here
  // with its own device bounds instead.
  auto deviceBounds = request.deviceBounds.value_or(Rect::MakeWH(
      static_cast<float>(request.target->width()), static_cast<float>(request.target->height())));
  return AOTPlanner::Build(_context, request.renderFlags, graph, plan, deviceBounds, request.target,
                           op, request.coordOffset, request.clearDestination, deviceToLocal,
                           terminalCoverage);
}

}  // namespace tgfx

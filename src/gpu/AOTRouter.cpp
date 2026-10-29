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

#include "AOTRouter.h"
#include "gpu/AOTEffectDecomposer.h"

namespace tgfx {

// The kernels the prepare-time fused serving can express: one fused chain/graph or perlin
// processor standing in for a draw's whole color expression. Every other kernel needs a plan
// task.
static bool IsInlineRewritableKernel(AOTKernelKind kernel) {
  return IsAOTPointwiseDAGKernel(kernel) || kernel == AOTKernelKind::PerlinNoiseFill;
}

AOTRouteResult AOTRouter::Route(const std::vector<const FragmentProcessor*>& processors,
                                const SubmitPlan& submit) {
  AOTEffectGraph graph = {};
  AOTEffectPlan plan = {};
  if (!AOTEffectDecomposer::Lower(processors, &graph) ||
      !AOTEffectDecomposer::Decompose(graph, &plan)) {
    return AOTRouteResult::NoPlan;
  }
  if (!PlanRequiresTask(plan)) {
    // No task is needed: the draw stays on the plain route with the deferral flag, and the
    // prepare-time plain lookup's fused walk serves it through the committed kernel.
    return AOTRouteResult::Deferred;
  }
  return submit(graph, plan) ? AOTRouteResult::Submitted : AOTRouteResult::TaskRefused;
}

bool AOTRouter::PlanInlineRewrite(const std::vector<const FragmentProcessor*>& processors,
                                  AOTEffectGraph* graph, AOTEffectPlan* plan) {
  if (!AOTEffectDecomposer::Lower(processors, graph) ||
      !AOTEffectDecomposer::Decompose(*graph, plan)) {
    return false;
  }
  return !PlanRequiresTask(*plan);
}

bool AOTRouter::PlanRequiresTask(const AOTEffectPlan& plan) {
  if (plan.passes.size() != 1) {
    return true;
  }
  return !IsInlineRewritableKernel(plan.passes[0].kernel);
}
}  // namespace tgfx

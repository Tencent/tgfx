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

#include "gpu/aot/AOTRouter.h"
#include "gpu/aot/AOTEffectDecomposer.h"

namespace tgfx {

// The kernels the prepare-time fused serving can express: one fused chain/graph or perlin
// processor standing in for a draw's whole color expression. Every other kernel needs a plan
// task.
static bool IsFusedServingKernel(AOTKernelKind kernel) {
  return IsAOTPointwiseDAGKernel(kernel) || kernel == AOTKernelKind::PerlinNoiseFill;
}

std::optional<AOTRoutePlan> AOTRouter::Route(
    const std::vector<const FragmentProcessor*>& processors, AOTDecomposeOutcome* failureOutcome) {
  AOTRoutePlan routed = {};
  std::string blocker;
  if (!AOTEffectDecomposer::Lower(processors, &routed.graph, &blocker)) {
    if (failureOutcome != nullptr) {
      *failureOutcome = processors.empty()
                            ? AOTDecomposeOutcome::Trivial
                            : (blocker.empty() ? AOTDecomposeOutcome::UnsupportedShape
                                               : AOTDecomposeOutcome::BlockedByLowering);
    }
    return std::nullopt;
  }
  if (!AOTEffectDecomposer::Decompose(routed.graph, &routed.plan)) {
    if (failureOutcome != nullptr) {
      *failureOutcome = AOTDecomposeOutcome::UnsupportedShape;
    }
    return std::nullopt;
  }
  if (failureOutcome != nullptr) {
    *failureOutcome = AOTDecomposeOutcome::FusablePointwise;
  }
  return routed;
}

bool AOTRouter::PlanSinglePass(const std::vector<const FragmentProcessor*>& processors,
                               AOTEffectGraph* graph, AOTEffectPlan* plan) {
  if (!AOTEffectDecomposer::Lower(processors, graph) ||
      !AOTEffectDecomposer::Decompose(*graph, plan, true)) {
    return false;
  }
  return !PlanRequiresTask(*plan);
}

bool AOTRouter::PlanRequiresTask(const AOTEffectPlan& plan) {
  if (plan.passes.size() != 1) {
    return true;
  }
  return !IsFusedServingKernel(plan.passes[0].kernel);
}
}  // namespace tgfx

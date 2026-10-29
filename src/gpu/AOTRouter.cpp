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
#include "gpu/AOTMaterializationPolicy.h"
#include "gpu/AOTMaterializationRecorder.h"

namespace tgfx {

// The kernels the prepare-time inline rewrite can express: one fused chain/graph or perlin
// processor replacing a draw's color list in place. Every other kernel needs a plan task.
static bool IsInlineRewritableKernel(AOTKernelKind kernel) {
  return IsAOTPointwiseDAGKernel(kernel) || kernel == AOTKernelKind::PerlinNoiseFill;
}

AOTRouteResult AOTRouter::Route(const std::vector<const FragmentProcessor*>& processors,
                                const SubmitPlan& submit, AOTMaterializationRecorder* producers) {
  AOTEffectGraph graph = {};
  AOTEffectPlan plan = {};
  if (!AOTEffectDecomposer::Lower(processors, &graph) ||
      !AOTEffectDecomposer::Decompose(graph, &plan)) {
    return AOTRouteResult::NoPlan;
  }
  return submit(graph, plan, producers) ? AOTRouteResult::Submitted : AOTRouteResult::TaskRefused;
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

bool AOTRouter::RetryWarranted(const FragmentProcessor* leading, uint32_t renderFlags) {
  if (leading == nullptr || leading->numChildProcessors() != 2 ||
      leading->name() != "XfermodeFragmentProcessor - two") {
    return false;
  }
  bool nested = (renderFlags & InternalRenderFlags::NestedRasterization) != 0;
  for (size_t childIndex = 0; childIndex < 2; ++childIndex) {
    auto decision = AOTMaterializationPolicy::Evaluate(
        leading->childProcessor(childIndex), MaterializationConsumer::PointwiseBlend, childIndex);
    if (decision.requiredForCorrectness || (!nested && decision.shouldFlatten)) {
      return true;
    }
  }
  return false;
}

AOTRouteResult AOTRouter::RouteWithMaterializedLeading(
    Context* context, const ColorChainRebuild& rebuildLeading,
    const std::vector<const FragmentProcessor*>& trailing, const SubmitPlan& submit) {
  if (context == nullptr || rebuildLeading == nullptr) {
    return AOTRouteResult::NoPlan;
  }
  // Producers recorded during the rebuild stay pending until Route hands them to the entry's plan
  // task; whatever is still pending when the recorder goes out of scope was refused and is dropped.
  AOTMaterializationRecorder recorder(context);
  auto leading = rebuildLeading();
  if (leading == nullptr) {
    return AOTRouteResult::NoPlan;
  }
  std::vector<const FragmentProcessor*> processors = {leading.get()};
  processors.insert(processors.end(), trailing.begin(), trailing.end());
  // The plan task rebuilds its processors from the lowered graph, so the rebuilt leading
  // processor is not needed once Route returns.
  return Route(processors, submit, &recorder);
}
}  // namespace tgfx

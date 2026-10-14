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

namespace tgfx {

AOTRouteResult AOTRouter::Route(const std::vector<const FragmentProcessor*>& processors,
                                const SubmitPlan& submit) {
  AOTEffectGraph graph = {};
  AOTEffectPlan plan = {};
  if (!AOTEffectDecomposer::Lower(processors, &graph) ||
      !AOTEffectDecomposer::Decompose(graph, &plan)) {
    return AOTRouteResult::NoPlan;
  }
  return submit(graph, plan) ? AOTRouteResult::Submitted : AOTRouteResult::TaskRefused;
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
    const ColorChainRebuild& rebuildLeading, const std::vector<const FragmentProcessor*>& trailing,
    const SubmitPlan& submit) {
  if (rebuildLeading == nullptr) {
    return AOTRouteResult::NoPlan;
  }
  auto leading = rebuildLeading();
  if (leading == nullptr) {
    return AOTRouteResult::NoPlan;
  }
  std::vector<const FragmentProcessor*> processors = {leading.get()};
  processors.insert(processors.end(), trailing.begin(), trailing.end());
  // The plan task rebuilds its processors from the lowered graph, so the rebuilt leading
  // processor is not needed once Route returns.
  return Route(processors, submit);
}
}  // namespace tgfx

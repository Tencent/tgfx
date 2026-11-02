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
#include "gpu/BlendFormula.h"
#include "gpu/aot/AOTEffectDecomposer.h"

namespace tgfx {

std::optional<AOTEffectPlan> AOTRouter::Plan(const DrawIR& ir,
                                             AOTDecomposeOutcome* failureOutcome) {
  AOTEffectPlan plan = {};
  if (!AOTEffectDecomposer::Decompose(ir.color, &plan)) {
    if (failureOutcome != nullptr) {
      *failureOutcome = AOTDecomposeOutcome::UnsupportedShape;
    }
    return std::nullopt;
  }
  if (failureOutcome != nullptr) {
    *failureOutcome = AOTDecomposeOutcome::FusablePointwise;
  }
  return plan;
}

bool AOTRouter::ServesInPlacePass(const AOTEffectPlan& plan) {
  if (plan.passes.size() != 1 || !IsAOTPointwiseDAGKernel(plan.passes[0].kernel)) {
    return false;
  }
  const auto& pass = plan.passes[0];
  return !pass.nodes.empty() && pass.output == plan.output && pass.dependencies.empty() &&
         !pass.materializesOutput;
}

bool AOTRouter::TaskCarriesCoverage(const AOTEffectPlan& plan,
                                    const std::vector<AOTCoverageTerm>& coverageTerms,
                                    BlendMode blendMode) {
  if (coverageTerms.empty()) {
    return true;
  }
  if (plan.passes.empty() || !IsAOTPointwiseDAGKernel(plan.passes.back().kernel)) {
    return false;
  }
  return !BlendModeNeedDstTexture(blendMode, true);
}

bool AOTRouter::PlanRequiresTask(const AOTEffectPlan& plan) {
  if (plan.passes.size() != 1) {
    return true;
  }
  return !IsAOTPointwiseDAGKernel(plan.passes[0].kernel);
}
}  // namespace tgfx

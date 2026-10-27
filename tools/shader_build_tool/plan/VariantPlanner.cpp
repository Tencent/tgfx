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
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "VariantPlanner.h"

namespace tgfx {

ShaderPlan VariantPlanner::PlanShader(const PrecompiledShaderInfo& info) {
  ShaderPlan plan;
  plan.variants = EnumerateReachablePermutations(info.name);
  if (!plan.variants) {
    return plan;
  }
  for (const auto& pair : *plan.variants) {
    if (!IsBuildablePermutation(info, pair.first, pair.second)) {
      plan.unbuildable.push_back(pair);
    }
  }
  return plan;
}

}  // namespace tgfx

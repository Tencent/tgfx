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

#include "AuditCommand.h"

#include <iostream>
#include "gpu/shaders/AllShaderFamilies.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "plan/VariantPlanner.h"

namespace tgfx {

// Validates every shader's rule-reachable set: each (vertIndex, fragIndex) pair the Compose
// functions can produce must be a structurally valid permutation (indices within the declared
// domains and mirrored dimensions in agreement), because the compile list is derived directly
// from these sets. An invalid entry would compile a variant with a broken vertex/fragment
// interface, so any violation fails the audit. The audit asks the same planner the build loop
// does, so the two paths cannot disagree.
int RunAuditCommand() {
  const auto& factories = ShaderRegistry::All();
  size_t audited = 0;
  size_t violations = 0;
  for (const auto& factory : factories) {
    auto shader = factory();
    auto info = shader->info();
    auto plan = VariantPlanner::PlanShader(info);
    if (!plan.variants) {
      std::cerr << "[audit] " << info.name << ": ERROR no rule enumerator\n";
      violations++;
      continue;
    }
    audited++;
    for (const auto& pair : plan.unbuildable) {
      violations++;
      std::cout << "[audit] " << info.name << ": INVALID vert=" << pair.first
                << " frag=" << pair.second << " (out of range or mirrored dimensions disagree)\n";
    }
    if (plan.unbuildable.empty()) {
      std::cout << "[audit] " << info.name << ": OK (" << plan.variants->size()
                << " reachable permutations)\n";
    }
  }
  std::cout << "[audit] summary: " << audited << " audited, " << violations << " violations\n";
  return violations == 0 ? 0 : 1;
}

}  // namespace tgfx

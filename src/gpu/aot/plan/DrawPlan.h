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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/Swizzle.h"
#include "gpu/aot/select/FamilyMatchResult.h"
#include "gpu/aot/spec/SpecValues.h"

namespace tgfx {

/**
 * One draw's resolved serving decision: the variant that names its program, the specialization
 * values folded into it, and the module data sources the writers read. The selection produces it
 * once per draw and it stays on the ProgramInfo, so the program lookup, the uniform writers and
 * the texture binder all read one object instead of re-deriving the same facts from the draw.
 *
 * The plan holds only target-independent inputs: the destination texture, the pipeline state and
 * the sample count stay on the ProgramInfo. The output swizzle rides here as the identity of the
 * acceptance that produced the plan — an alpha-only target admits a different set of families, so
 * a plan made against one target must not be reused against another.
 */
struct DrawPlan {
  /// The variant that selects the program's blob pair; also the family/ordinal part of its key.
  Variant variant = {};
  /// The variant's match result: the per-role leaves the declared families' writers read.
  FamilyMatchResult sources = {};
  /// The specialization values folded into the program; empty means the generic form.
  SpecValues spec = {};
  /// The output swizzle the acceptance ran under (see the struct comment).
  Swizzle outputSwizzle = Swizzle::RGBA();
};

}  // namespace tgfx

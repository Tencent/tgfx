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
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/KernelTable.h"
#include "gpu/shaders/AllShaderFamilies.h"
#include "gpu/shaders/ShaderFamily.h"

namespace tgfx {

std::optional<AOTKernelMatch> KernelTableLookup(const DrawShape& facts,
                                                PermutationMatchFailure* failure) {
#define TGFX_TRY_FAMILY(F)                                    \
  if constexpr (FamilyHasExtractFacts<F>::value) {            \
    if (!facts.alphaOnlyTarget || F::ServesAlphaOnly) {       \
      if (auto variant = F::MatchFromFacts(facts, failure)) { \
        return AOTKernelMatch{F::Name, *variant};             \
      }                                                       \
    }                                                         \
  }
  TGFX_FOR_EACH_SHADER_FAMILY(TGFX_TRY_FAMILY)
#undef TGFX_TRY_FAMILY
  return std::nullopt;
}

}  // namespace tgfx

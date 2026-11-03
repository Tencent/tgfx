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
#include <cassert>
#include <type_traits>
#include "gpu/shaders/AllShaderFamilies.h"
#include "gpu/shaders/ShaderFamily.h"

namespace tgfx {
namespace {

/// The one family that is not a table row: its acceptance is the carried IR, not the facts, and
/// the matcher calls its Extract as the walk's last step. Naming it here turns the exemption from
/// a silent SFINAE default into an explicit, checked exception.
template <typename F>
constexpr bool IsInterpreterFamily = std::is_same_v<F, PointwiseChainShader>;

}  // namespace

std::optional<AOTKernelMatch> KernelTableLookup(const DrawShape& facts,
                                                PermutationMatchFailure* failure) {
  // Every family must either join the table (FillKernel) or be the named interpreter: a family
  // that forgets both would otherwise vanish from matching silently.
#define TGFX_TRY_FAMILY(F)                                                                 \
  static_assert(FamilyJoinsKernelTable<F>::value || IsInterpreterFamily<F>,                \
                "a shader family must derive FillKernel (join the kernel table) or "       \
                "be the pointwise-chain interpreter");                                     \
  if constexpr (FamilyJoinsKernelTable<F>::value) {                                        \
    if (!facts.alphaOnlyTarget || F::ServesAlphaOnly) {                                    \
      if (auto variant = F::MatchFromFacts(facts, failure)) {                              \
        assert(F::CachedReachable().count(                                                 \
                   {variant->vertPermutationIndex, variant->fragPermutationIndex}) != 0 && \
               "matched a variant outside the family's reachable set; the bundle "         \
               "lacks it and the draw would be dropped — extend its AllInputs");           \
        return AOTKernelMatch{F::Name, *variant};                                          \
      }                                                                                    \
    }                                                                                      \
  }
  TGFX_FOR_EACH_SHADER_FAMILY(TGFX_TRY_FAMILY)
#undef TGFX_TRY_FAMILY
  return std::nullopt;
}

}  // namespace tgfx

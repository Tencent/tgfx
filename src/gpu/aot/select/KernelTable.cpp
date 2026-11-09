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

#include "gpu/aot/select/KernelTable.h"
#include <cassert>
#include <type_traits>
#include "core/utils/Log.h"
#include "gpu/aot/select/FamilyMatch.h"
#include "gpu/shaders/AllShaderFamilies.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/ShaderFamily.h"

namespace tgfx {
namespace {

/// The one family that is not a table row: its acceptance is the carried IR, not the facts, and
/// the matcher calls its Extract as the walk's last step. Naming it here turns the exemption from
/// a silent SFINAE default into an explicit, checked exception.
template <typename F>
constexpr bool IsInterpreterFamily = std::is_same_v<F, PointwiseChainShader>;

#if DEBUG
/// The offline exclusivity test enumerates the DrawShape field space with a null IR, so the
/// IR-reading families sit outside its coverage; this runtime pre-pass closes that gap over real
/// draws. At most one table family may accept a shape — with two, the table order silently picks
/// the winner and the second family's coverage is dead weight.
void AssertAtMostOneFamilyAccepts(const DrawShape& facts) {
  int accepting = 0;
  const char* first = nullptr;
#define TGFX_COUNT_FAMILY(F)                                           \
  if constexpr (FamilyJoinsKernelTable<F>::value) {                    \
    if (!facts.alphaOnlyTarget || F::ServesAlphaOnly) {                \
      PermutationMatchFailure ignored = PermutationMatchFailure::None; \
      if (F::MatchFromFacts(facts, &ignored)) {                        \
        if (accepting == 0) {                                          \
          first = F::Name;                                             \
        }                                                              \
        ++accepting;                                                   \
      }                                                                \
    }                                                                  \
  }
  TGFX_FOR_EACH_SHADER_FAMILY(TGFX_COUNT_FAMILY)
#undef TGFX_COUNT_FAMILY
  if (accepting > 1) {
    LOGE(
        "KernelTableLookup: %d families accept one shape (%s first); the table order silently "
        "picks the winner — narrow the families' Accept predicates",
        accepting, first);
    DEBUG_ASSERT(false);
  }
}
#endif

}  // namespace

std::optional<AOTKernelMatch> KernelTableLookup(const DrawShape& facts,
                                                PermutationMatchFailure* failure) {
#if DEBUG
  AssertAtMostOneFamilyAccepts(facts);
#endif
  // The legacy table runs first during the migration, and this is the equivalence contract, not
  // a transitional convenience: a draw's old-table home was the first acceptor in file-name
  // order, the legacy table only ever loses families (never reorders or adds), and a family
  // earlier in that order that still claims the draw would have won before the migration too —
  // so consulting the legacy table first reproduces the old outcome exactly, and the declared
  // families only serve what no legacy family claims (the draws of their deleted predecessors).
  // Cross-table overlap is therefore expected here (QuadTextureFillShader still wins the
  // QuadPerEdgeAA tiled draws the declared TiledTextureFill would also accept); it disappears
  // with the legacy table in P3, where the declared order takes over.
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
  // The declaration-driven families' variants ride the same bundle under the ordinal the family
  // table's domains encode.
  if (auto assembled = MatchAssembledFamily(facts)) {
    PermutationVariant variant = {};
    variant.vertPermutationIndex = assembled->variant.ordinal;
    variant.fragPermutationIndex = assembled->variant.ordinal;
    return AOTKernelMatch{Families()[assembled->variant.family].name, variant};
  }
  return std::nullopt;
}

}  // namespace tgfx

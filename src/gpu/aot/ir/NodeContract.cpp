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
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/ir/NodeContract.h"
#include "gpu/aot/ir/ChainContext.h"

namespace tgfx {
namespace {

// One row per AOTEffectKind, in enum order: the kind's traits, its encoder and its resource
// demand. The row count assert pins the table to the enum, so adding a kind without adding a row
// fails the build instead of silently reading a stale row; the second assert pins the directional
// rule that a fusable kind must actually be encodable (the "forgot the encoder" failure would
// otherwise surface at runtime as a silent route fallback).
constexpr AOTNodeContract KindContracts[] = {
    // GeometryColor (graph root, designator): never scheduled.
    {{false, AOTDesignator::GeometryColor}, nullptr, nullptr},
    {{true, AOTDesignator::None, false, AOTRemapKind::TextureMatrix},
     EncodeTextureSourceSlot,
     DemandTextureSource},
    {{true, AOTDesignator::None, true}, EncodeUnaryPointwiseSlot, nullptr},  // ColorMatrix
    {{true, AOTDesignator::None, true}, EncodeUnaryPointwiseSlot, nullptr},  // Luma
    {{true, AOTDesignator::None, true}, EncodeUnaryPointwiseSlot, nullptr},  // AlphaThreshold
    {{true, AOTDesignator::None, true}, EncodeUnaryPointwiseSlot, nullptr},  // ColorSpaceXform
    {{true}, EncodeConstColorSlot, nullptr},
    {{true}, EncodeBlendSlot, nullptr},
    // The noise demand is counted from the layout's noise record instead (the layout owns it
    // after stage three), so the row carries no demand.
    {{true, AOTDesignator::None, false, AOTRemapKind::LocalMatrix}, EncodePerlinNoiseSlot, nullptr},
    {{true}, EncodeRectCoverageSlot, nullptr},
    {{true, AOTDesignator::None, false, AOTRemapKind::LocalMatrix},
     EncodeGradientSlot,
     DemandGradientSource},
    {{true}, EncodeRRectCoverageSlot, nullptr},
    // GeometryCoverage (graph root, designator): never scheduled.
    {{false, AOTDesignator::GeometryCoverage}, nullptr, nullptr},
    {{true, AOTDesignator::OpaqueInput}, nullptr, nullptr},  // GeometryColorOpaqueInput
    {{true, AOTDesignator::WhiteInput}, nullptr, nullptr},   // GeometryWhiteInput
    {{true}, EncodeInputOpaqueSlot, nullptr},
    {{true}, EncodeMulAlphaSlot, nullptr},
    // The opaque-kernel barrier (blur, glass): the planner refuses a graph containing one, so the
    // row is never fusable and carries no encoder — the dedicated family reads the node's facts.
    {{false}, nullptr, nullptr},
};

constexpr size_t KindContractCount = sizeof(KindContracts) / sizeof(AOTNodeContract);
static_assert(KindContractCount == static_cast<size_t>(AOTEffectKind::OpaqueKernel) + 1,
              "every AOTEffectKind needs exactly one contract row in KindContracts");

constexpr bool EveryFusableKindIsEncodable() {
  for (const auto& row : KindContracts) {
    // The designators are fusable (they may appear inside a fused pass as inputs) but never
    // scheduled, so they are the only fusable rows allowed to leave the encoder null.
    if (row.traits.chainFusable && !row.traits.isInputDesignator() && row.encode == nullptr) {
      return false;
    }
  }
  return true;
}
static_assert(EveryFusableKindIsEncodable(),
              "a scheduled kind must have an encoder; the graph roots and the designators are "
              "the only rows allowed to leave it null");

}  // namespace

const AOTNodeContract& NodeContractOf(AOTEffectKind kind) {
  static constexpr AOTNodeContract fallback = {};
  auto index = static_cast<size_t>(kind);
  return index < KindContractCount ? KindContracts[index] : fallback;
}

const AOTKindTraits& TraitsOf(AOTEffectKind kind) {
  return NodeContractOf(kind).traits;
}

}  // namespace tgfx

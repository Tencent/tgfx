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
#include "gpu/aot/bind/Binder.h"
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

// One row per AOTChainOp, in the op ABI value order (Op::* in KernelContract.h): the instruction's
// uniform write. A separate table on purpose — the instruction vocabulary and the node vocabulary
// are not one-to-one (see the header).
constexpr AOTOpContract OpContracts[] = {
    {BindColorMatrixSlot},        // ColorMatrix (0)
    {BindLumaSlot},               // Luma (1)
    {BindAlphaThresholdSlot},     // AlphaThreshold (2)
    {},                           // ColorSpaceXform (3): rides the program-level Chain block
    {},                           // None (4, the inactive-slot marker)
    {},                           // Texture (5): the sampler binding carries its records
    {BindConstColorSlot},         // ConstColor (6)
    {},                           // Blend (7): the chain's operands are registers
    {BindAARectCoverageSlot},     // AARectCoverage (8)
    {},                           // Gradient (9): the program-level gradient block
    {BindLocalRectCoverageSlot},  // LocalRectCoverage (10)
    {BindRRectCoverageSlot},      // RRectCoverage (11)
    {},                           // TexModulate (12, synthesized by the texture encoder)
    {},                           // InputOpaque (13)
    {},                           // MulAlpha (14)
    {},                           // PerlinNoise (15): the program-level noise block
};

constexpr size_t OpContractCount = sizeof(OpContracts) / sizeof(AOTOpContract);
static_assert(OpContractCount == 16,
              "every AOTChainOp needs exactly one contract row in OpContracts");

}  // namespace

const AOTNodeContract& NodeContractOf(AOTEffectKind kind) {
  static constexpr AOTNodeContract fallback = {};
  auto index = static_cast<size_t>(kind);
  return index < KindContractCount ? KindContracts[index] : fallback;
}

const AOTOpContract& OpContractOf(AOTChainOp op) {
  static constexpr AOTOpContract fallback = {};
  auto index = static_cast<size_t>(op);
  return index < OpContractCount ? OpContracts[index] : fallback;
}

bool IsSchedulableChainOp(AOTChainOp op) {
  return static_cast<size_t>(op) < OpContractCount && op != AOTChainOp::None;
}

const AOTKindTraits& TraitsOf(AOTEffectKind kind) {
  return NodeContractOf(kind).traits;
}

}  // namespace tgfx

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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/aot/ir/Program.h"

namespace tgfx {

struct AOTChainContext;
struct AOTResourceDemand;
class UniformData;

/// The chain input designator a kind maps to when it appears as an op input. None marks every
/// scheduled kind; the four designator kinds are never slots, and the two roots are never copied.
enum class AOTDesignator : int {
  None = 0,
  GeometryColor = -1,
  GeometryCoverage = -3,
  WhiteInput = -4,
  OpaqueInput = -5,
};

/// How a copied node's coordinate parameters follow a pass remap. None: the parameters carry no
/// coordinates. LocalMatrix: one coordinate matrix precomposed with the local mapping (a gradient's
/// coordMatrix, a noise source's uvMatrix). TextureMatrix: the uv matrix, precomposed with the
/// device-leaf mapping for device sampling and with the local mapping otherwise.
enum class AOTRemapKind : uint8_t {
  None,
  LocalMatrix,
  TextureMatrix,
};

/**
 * Static facts about one AOTEffectKind. Every call site that used to switch or if-chain over node
 * kinds reads this table instead, so adding a kind means adding one row (a missing row fails the
 * table-size static_assert at compile time) rather than remembering N scattered switch sites.
 */
struct AOTKindTraits {
  /// May appear inside a fused chain pass. The two graph roots (GeometryColor, GeometryCoverage)
  /// are the only kinds excluded; a future non-pointwise kind defaults to false here until its
  /// contract row says otherwise, which keeps it out of fused passes until deliberately admitted.
  bool chainFusable = false;
  /// The op-input designator the kind maps to (see MapChainInput); None for scheduled kinds.
  AOTDesignator designator = AOTDesignator::None;
  /// One of the four unary pointwise operators sharing the OP_* ABI values.
  bool unaryPointwise = false;
  /// How the kind's coordinate parameters follow a pass remap (see AOTRemapKind).
  AOTRemapKind remap = AOTRemapKind::None;

  constexpr bool isInputDesignator() const {
    return designator != AOTDesignator::None;
  }
};

// The contract implementations' signatures. A kind's encoder turns one scheduled node into its
// chain slot(s) (0..2 — a computed-input texture emits the raw sample plus a modulate, so the
// kind-to-op mapping is the encoder's business, never the table's); its demand adds the node's
// resource demand to the tally.
using AOTNodeEncoder = bool (*)(AOTChainContext* ctx, size_t combined, size_t slotIndex);
using AOTNodeDemand = void (*)(const AOTEffectNode* node, AOTResourceDemand* demand);

/**
 * One contract row of the expression vocabulary (indexed by AOTEffectKind): the kind's traits,
 * its encoder and its resource demand. A new kind adds one row here, and the directional asserts
 * in NodeContract.cpp fail the build when a fusable kind has no encoder.
 */
struct AOTNodeContract {
  AOTKindTraits traits = {};
  AOTNodeEncoder encode = nullptr;
  AOTNodeDemand demand = nullptr;
};

/** Returns the contract row of one node kind; the table is the single place its facts live. */
const AOTNodeContract& NodeContractOf(AOTEffectKind kind);

/** Returns the traits row of one kind; the table is the single place a kind's facts live. */
const AOTKindTraits& TraitsOf(AOTEffectKind kind);

}  // namespace tgfx

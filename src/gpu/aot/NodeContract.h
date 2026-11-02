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

#include "gpu/aot/EffectGraph.h"
#include "gpu/aot/Program.h"

namespace tgfx {

struct AOTChainContext;
struct AOTResourceDemand;
class UniformData;

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
  /// An input designator: never a chain slot, excluded from pass node lists. Covers all four
  /// designator kinds; the two root designators can never be copied or scheduled anyway.
  bool inputDesignator = false;
  /// One of the four unary pointwise operators sharing the OP_* ABI values.
  bool unaryPointwise = false;
  /// Carries a local-coordinate matrix that a cut-segment copy must precompose with the fill's
  /// coordToLocal (or deviceLeafRemap for a device-sampled texture).
  bool remapsLocalCoords = false;
};

// The contract implementations' signatures. A kind's encoder turns one scheduled node into its
// chain slot(s) (0..2 — a computed-input texture emits the raw sample plus a modulate, so the
// kind-to-op mapping is the encoder's business, never the table's); its demand adds the node's
// resource demand to the tally.
using AOTNodeEncoder = bool (*)(AOTChainContext* ctx, size_t combined, size_t slotIndex);
using AOTNodeDemand = void (*)(const AOTEffectNode* node, AOTResourceDemand* demand);
/// One chain instruction's per-slot parameter write. The packed word and its selector stay with
/// the caller's dialect (the chain encodes result registers, the YUV tail encodes the op's small
/// value), so the table carries only the parameter arrays: `index` is the slot's index (the
/// per-op arrays), `ordinal` is its ordinal into a per-kind array (the coverage capacities; the
/// slot index for every other op). A null row means the instruction writes no parameter block.
using AOTOpBinder = void (*)(UniformData* uniformData, size_t index, int ordinal,
                             const AOTChainSlot& slot);

/**
 * One contract row of the expression vocabulary (indexed by AOTEffectKind): the kind's traits,
 * its encoder and its resource demand. This table and AOTOpContract are the whole of a kind's
 * AOT contract — a new kind adds one row here, and the directional asserts in NodeContract.cpp
 * fail the build when a fusable kind has no encoder.
 */
struct AOTNodeContract {
  AOTKindTraits traits = {};
  AOTNodeEncoder encode = nullptr;
  AOTNodeDemand demand = nullptr;
};

/**
 * One contract row of the instruction vocabulary (indexed by AOTChainOp). Deliberately a second
 * table: the two vocabularies are not one-to-one — five kinds never emit an instruction (the
 * graph roots and the designators), and two instructions have no node of their own (None is the
 * inactive-slot marker, TexModulate is synthesized by the texture encoder). Sharing one row count
 * would be a false invariant, so each table is pinned to its own enum.
 */
struct AOTOpContract {
  AOTOpBinder bind = nullptr;
};

/** Returns the contract row of one node kind; the table is the single place its facts live. */
const AOTNodeContract& NodeContractOf(AOTEffectKind kind);

/** Returns the contract row of one chain instruction. */
const AOTOpContract& OpContractOf(AOTChainOp op);

/**
 * Whether a value is a schedulable chain instruction: inside the instruction table's bounds and
 * not the inactive-slot marker. The validity of the op vocabulary is the table's own extent, so a
 * new instruction is schedulable the moment it gains a row — no separate whitelist to keep in
 * sync (the kernel's evaluation switch has no default branch, so an out-of-enum opcode must be
 * rejected here rather than silently misread).
 */
bool IsSchedulableChainOp(AOTChainOp op);

/** Returns the traits row of one kind; the table is the single place a kind's facts live. */
const AOTKindTraits& TraitsOf(AOTEffectKind kind);

}  // namespace tgfx

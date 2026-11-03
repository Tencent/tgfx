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

#pragma once

#include <vector>
#include "gpu/aot/AOTResourceProfile.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/aot/ir/Program.h"
#include "gpu/aot/plan/AOTChainRegisterAllocator.h"

namespace tgfx {

struct AOTChainCompile;
struct AOTPassDescriptor;

/**
 * The shared state of the chain-layout pipeline (see AOTChainBuilder's PlanChainLayout): each
 * stage fills part of it and reads what the earlier stages produced, so no shape rule is ever
 * re-derived. Opaque outside the AOT pipeline — only the pipeline stages and the per-kind
 * contract encoders (declared below and registered by the node contract table) touch it.
 */
struct AOTChainContext {
  // Pipeline inputs.
  const AOTEffectGraph* graph = nullptr;
  const AOTPassDescriptor* pass = nullptr;
  // The analytic coverage terms' links, as node IDs in the coverage graph, in chain order; they
  // are encoded as ordinary slots and rewired onto the clip-coverage channel by stage seven.
  const std::vector<AOTNodeID>* clipLinks = nullptr;
  const AOTEffectGraph* coverageGraph = nullptr;
  AOTNodeID coverageRoot = AOTNodeID();
  bool coverageLeafFromUVCoord = false;
  bool checkKernel = false;
  AOTChainCompile* layout = nullptr;
  // Stage results, consumed by the later stages.
  std::vector<const AOTEffectNode*> nodes = {};
  size_t colorCount = 0;
  size_t covCount = 0;
  std::vector<bool> inPass = {};
  std::vector<size_t> slotOf = {};
  std::vector<size_t> rawSlotOf = {};
  std::vector<size_t> ordered = {};
  std::vector<AOTChainSlot> slots = {};
  bool hasGradient = false;
  AOTResourceProfile finalProfile = AOTResourceProfile::Automatic;
  size_t rootIndex = 0;
  int coverageRootSlot = -1;
  int clipCoverageSlot = -1;
  int clipChainInput = -4;
  // Stage-three budget counters carried across the per-kind encoders.
  size_t rectCoverageSlots = 0;
  size_t localRectCoverageSlots = 0;
  AOTChainRegisterAssignment assignment = {};
};

// The per-kind contract implementations the node contract table registers: each encoder turns one
// scheduled node into its chain slot (0..2 slots — a computed-input texture emits the raw sample
// and a modulate), and each demand adds the node's resource demand to the profile tally.
bool EncodeTextureSourceSlot(AOTChainContext* ctx, size_t combined, size_t slotIndex);
bool EncodePerlinNoiseSlot(AOTChainContext* ctx, size_t combined, size_t slotIndex);
bool EncodeConstColorSlot(AOTChainContext* ctx, size_t combined, size_t slotIndex);
// Shared by the four unary pointwise kinds (the single pointwise op ABI value picks the math).
bool EncodeUnaryPointwiseSlot(AOTChainContext* ctx, size_t combined, size_t slotIndex);
bool EncodeInputOpaqueSlot(AOTChainContext* ctx, size_t combined, size_t slotIndex);
bool EncodeMulAlphaSlot(AOTChainContext* ctx, size_t combined, size_t slotIndex);
bool EncodeBlendSlot(AOTChainContext* ctx, size_t combined, size_t slotIndex);
bool EncodeRectCoverageSlot(AOTChainContext* ctx, size_t combined, size_t slotIndex);
bool EncodeRRectCoverageSlot(AOTChainContext* ctx, size_t combined, size_t slotIndex);
bool EncodeGradientSlot(AOTChainContext* ctx, size_t combined, size_t slotIndex);

void DemandTextureSource(const AOTEffectNode* node, AOTResourceDemand* demand);
void DemandGradientSource(const AOTEffectNode* node, AOTResourceDemand* demand);

}  // namespace tgfx

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

#include <cstddef>
#include <vector>
#include "gpu/aot/chain/ChainContext.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/NodeContract.h"

namespace tgfx {

/**
 * The compiled result of one chain pass: the data-only program plus the build-time leaf plan the
 * processor construction reads (graph node pointers for the texture leaves, the LUT gradient and
 * noise parameter records, and the device-space leaf indices). The shell-free serving path
 * replaces the leaf plan with the program's binding table.
 */
struct AOTChainLayout {
  AOTProgram program = {};
  // Texture leaves in sampler order: leafNodes[k] pairs with slot k and TextureSampler_k.
  std::vector<const AOTEffectNode*> leafNodes = {};
  const AOTGradientParameters* lutGradient = nullptr;
  const AOTPerlinNoiseParameters* noise = nullptr;
  // Indices into leafNodes of the device-space leaves, in leaf order.
  std::vector<int> deviceLeafIndices = {};
};

// The chain kernel's own capacities, as the fixed-size GLSL arrays declare them: the instruction
// storage across the shapes (the noise shape: 32; the gradient shape shares the plain chain's 16),
// the chainResults register file the allocator recycles by last use, and the chain-wide parameter
// blocks (one gradient, one color-space transform, two device-space and two local rect coverage
// sets, four rrect array elements). ValidateSlots is the only place that enforces them; the
// planner and the compile both run it on the same instruction array, so the two cannot disagree.
inline constexpr size_t MaxChainSlots = NoiseGraphResourceCapacity.instructions;
inline constexpr size_t MaxChainGradientSlots = TiledGraphResourceCapacity.gradients;
inline constexpr size_t MaxChainColorSpaceXformSlots = 1;

// The chain-layout pipeline, four stages over one merged node space. PlanChainLayout is the only
// caller that runs them in order; every other entry point (AOTChainBuilder::Compile, FitsChainPlan)
// goes through it, so a stage never re-derives what an earlier stage produced. A stage's steps are
// file-static in its translation unit: only these four names cross a file boundary.

// Turns one scheduled node into its pointwise op slot. Shared by the encoder (EncodeUnaryPointwise)
// and the texture/YUV tail collectors in AOTChainBuilder.cpp.
bool BuildPointwiseSlot(const AOTEffectGraph& graph, const AOTEffectNode* node, AOTChainSlot* slot);

// Stage one — Schedule: merges the color and coverage node spaces, admits the pass, then orders
// the texture leaves ahead of the remaining nodes in topological order.
bool ScheduleSlots(AOTChainContext* ctx);

// Stage two — Encode: encodes every scheduled node into its chain slot, resolves the pass's
// resource shape, roots the coverage subtree, and routes the analytic coverage links.
bool EncodeSlots(AOTChainContext* ctx);

// Stage three — Registers: rewrites instruction ordinals into the kernel's recycled result
// registers, so the instruction budget and the live-result budget stay independent.
bool AllocateRegisters(AOTChainContext* ctx);

// Stage four — Bindings: the slot rules and the final shape's capacities, checked on the exact
// instruction array the compile hands to ValidateSlots, then published onto the program.
bool BindLayout(AOTChainContext* ctx);

/// Describes a graph that is its own pass: every non-designator node in index order, rooted at the
/// graph's root, with the resource profile the chain plan resolves. A cut segment graph and a
/// fused draw graph are both exactly that, so one derivation keeps them from disagreeing. Returns
/// false when the graph cannot fit one fused pass.
bool DeriveWholeGraphPass(const AOTEffectGraph& graph, PassIR* pass);

// The pipeline entry: stages one through eight over one merged node space.
bool PlanChainLayout(const AOTEffectGraph& graph, const PassIR& pass,
                     const AOTEffectGraph* coverageGraph, AOTNodeID coverageRoot,
                     bool coverageLeafFromUVCoord, const std::vector<AOTNodeID>& clipLinks,
                     AOTChainLayout* layout);

// The one compile entry: the planner plus the sampler binding table.
bool CompileChainLayout(const AOTEffectGraph& graph, const PassIR& pass,
                        const std::vector<AOTCoverageTerm>* coverageTerms, AOTChainLayout* out);

// The program-only view of the same entry.
bool CompileChainProgramLayout(const AOTEffectGraph& graph, const PassIR& pass,
                               const std::vector<AOTCoverageTerm>* coverageTerms, AOTProgram* out);

}  // namespace tgfx

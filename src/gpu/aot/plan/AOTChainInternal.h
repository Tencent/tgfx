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
#include "gpu/aot/ir/ChainContext.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/NodeContract.h"

namespace tgfx {

// The chain kernel's own capacities, as the fixed-size GLSL arrays declare them: the instruction
// storage across the shapes (the noise shape: 32; the gradient shape shares the plain chain's 16),
// the chainResults register file the allocator recycles by last use, and the chain-wide parameter
// blocks (one gradient, one color-space transform, two device-space and two local rect coverage
// sets, four rrect array elements). ValidateSlots is the only place that enforces them; the
// planner and the compile both run it on the same instruction array, so the two cannot disagree.
inline constexpr size_t MaxChainSlots = NoiseGraphResourceCapacity.instructions;
inline constexpr size_t MaxChainRegisters = 16;
inline constexpr size_t MaxChainGradientSlots = TiledGraphResourceCapacity.gradients;
inline constexpr size_t MaxChainColorSpaceXformSlots = 1;
inline constexpr size_t MaxChainDeviceRectCoverageSlots = 2;
inline constexpr size_t MaxChainLocalRectCoverageSlots = 2;
inline constexpr size_t MaxChainRRectCoverageSlots = 4;

// The chain-layout pipeline, split by stage across AOTChainSchedule/Encode/Allocate/Bindings.cpp.
// PlanChainLayout is the only caller that runs the stages in order; every other entry point
// (AOTChainBuilder::Compile, FitsChainPlan) goes through it, so a stage never re-derives what an
// earlier stage produced.

// Turns one scheduled node into its pointwise op slot. Shared by the encoder (EncodeUnaryPointwise)
// and the texture/YUV tail collectors in AOTChainBuilder.cpp.
bool BuildPointwiseSlot(const AOTEffectGraph& graph, const AOTEffectNode* node,
                        AOTPointwiseSlot* slot);

// Stage one: merges the color and coverage node spaces and admits the pass.
bool MergeNodeSpaces(AOTChainContext* ctx);

// Stage two: texture leaves first, then the remaining nodes in topological order.
void ScheduleChainSlots(AOTChainContext* ctx);

// Stage three: encodes every scheduled node into its chain slot.
bool EncodeChainSlots(AOTChainContext* ctx);

// Stage four: the pass's resource demand picks the final profile.
bool ResolveFinalChainProfile(AOTChainContext* ctx);

// Stage five: resolves the coverage subtree's root register and validates its shape.
bool ResolveCoverageRoot(AOTChainContext* ctx);

// Stage six: routes the analytic coverage links onto the clip-coverage channel.
void RouteAnalyticChains(AOTChainContext* ctx);

// Stage seven: register allocation.
bool AssignChainRegisters(AOTChainContext* ctx);

// Stage eight: slot rules and the final profile's capacities.
bool FinalizeChainLayout(AOTChainContext* ctx);

// The pipeline entry: stages one through eight over one merged node space.
bool PlanChainLayout(const AOTEffectGraph& graph, const PassIR& pass,
                     const AOTEffectGraph* coverageGraph, AOTNodeID coverageRoot,
                     bool coverageLeafFromUVCoord, const std::vector<AOTNodeID>& clipLinks,
                     AOTChainCompile* layout);

// The one compile entry: the planner plus the sampler binding table.
bool CompileChainLayout(const AOTEffectGraph& graph, const PassIR& pass,
                        const std::vector<AOTCoverageTerm>* coverageTerms, AOTChainCompile* out);

// The program-only view of the same entry.
bool CompileChainProgramLayout(const AOTEffectGraph& graph, const PassIR& pass,
                               const std::vector<AOTCoverageTerm>* coverageTerms, AOTProgram* out);

}  // namespace tgfx

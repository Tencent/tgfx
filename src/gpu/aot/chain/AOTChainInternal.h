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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstddef>
#include <vector>
#include "gpu/CoordTransform.h"
#include "gpu/aot/AOTResourceProfile.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/NodeContract.h"
#include "gpu/aot/plan/AOTPassIR.h"

namespace tgfx {

struct AOTProgram;

// The chain kernel's own capacities, as the fixed-size GLSL arrays declare them: the instruction
// storage across the shapes (the noise shape: 32; the gradient shape shares the plain chain's 16),
// the chainResults register file the allocator recycles by last use, and the chain-wide parameter
// blocks (one gradient, one color-space transform, two device-space and two local rect coverage
// sets, four rrect array elements).
inline constexpr size_t MaxChainSlots = NoiseGraphResourceCapacity.instructions;
/// A chain pass carries at most one LUT gradient: the gradient profiles declare one leaf and
/// the walk rejects a second.
inline constexpr size_t MaxChainGradientSlots = ChainProfileGradientLeaves[3];
inline constexpr size_t MaxChainColorSpaceXformSlots = 1;

/// One planned call site as the graph layout consumes it: the operator, its two resolved inputs (a
/// designator or a producing slot index — the register assignment rewrites these after the layout),
/// the result register slot, the operator's small selector values, and the graph node the site was
/// encoded from (the upload-time readers address their parameter blocks through it).
struct AOTChainCallSite {
  int op = 0;
  int in0 = -2;
  int in1 = -2;
  int outRegister = -1;
  /// Blend's mode and selector bit; ConstColor's input mode; Texture's low selector bits;
  /// TexModulate's raw-sampling leaf and alpha-only flag.
  int blendMode = 0;
  bool multiplyInputAlpha = false;
  int inputMode = 0;
  int textureFlags = 0;
  int texModulateSourceSlot = -1;
  bool texModulateAlphaOnly = false;
  const AOTEffectNode* node = nullptr;
};

// The merged coverage graph of a draw's coverage terms: one graph over one geometry-coverage
// unit, the analytic terms' links chained onto it, and — when a subtree or glyph term exists —
// its nodes copied over with the term's own unit replaced by the combined one.
struct ChainCoverageGraph {
  AOTEffectGraph graph = {};
  AOTNodeID coverageRoot = AOTNodeID::Invalid();
  std::vector<AOTNodeID> clipLinks = {};
  const AOTTextureParameters* mask = nullptr;
  bool coverageLeafFromUVCoord = false;
};

// Merges the draw's coverage terms into ONE coverage graph for the layout: one
// geometry-coverage unit, the analytic terms' links chained onto it (each term's innermost link
// continues the previous term's outermost — a pure product), and — when a subtree or glyph term
// exists — its nodes copied over with the term's own unit replaced by the combined one. The
// analytic links' node IDs are reported in chain order for the clip channel's rewiring; the
// DeviceMask term's parameters are reported for the mask child. Returns false when the terms
// cannot combine (a subtree and a glyph together never lower).
bool MergeCoverageTerms(const std::vector<AOTCoverageTerm>& terms, ChainCoverageGraph* merged);

/**
 * The graph layout of one chain pass: everything the two consumers of a laid-out chain share —
 * the assembled plan's walk and the program binding table's builder — derived once so the two
 * cannot disagree. It carries the merged node space, the texture-first instruction order, the
 * encoded call sites (with their nodes), the leaf records the sampler table binds, and the
 * resolved coverage facts (the merged coverage graph, the coverage/clip root slots).
 */
struct AOTChainGraphLayout {
  /// The merged node space: the color graph's nodes, then the merged coverage graph's.
  std::vector<const AOTEffectNode*> nodes = {};
  size_t colorCount = 0;
  size_t covCount = 0;
  /// slotOf maps a node to the instruction slot its consumers read; rawSlotOf maps a
  /// computed-input texture to its leading raw-sampling slot. ordered lists the nodes in
  /// instruction order (texture leaves first, then the remaining nodes in index order).
  std::vector<size_t> slotOf = {};
  std::vector<size_t> rawSlotOf = {};
  std::vector<size_t> ordered = {};
  /// The encoded call sites in instruction order; in0/in1 hold slot indices until the register
  /// assignment runs (the binding builder consumes them as laid out).
  std::vector<AOTChainCallSite> sites = {};
  /// The leaves the body samples, in sampler order (a multi-plane source occupies three), plus
  /// the resource records the binding table and the profile derivation read.
  std::vector<const AOTEffectNode*> leafNodes = {};
  std::vector<int> deviceLeafIndices = {};
  const AOTPerlinNoiseParameters* noise = nullptr;
  const AOTGradientParameters* lutGradient = nullptr;
  /// Bit k is set when leaf k samples in destination device space; the coord-source mask for the
  /// chain vertex stage (bit k sources leaf k from the uvCoord attribute, bit 4 the procedural
  /// sources).
  uint32_t coordSourceMask = ~0u;
  /// The instruction slot of the color pass's output.
  size_t rootIndex = 0;
  int coverageRootSlot = -1;
  int clipCoverageSlot = -1;
  /// The merged coverage graph backing the coverage nodes above (empty when the draw carries no
  /// coverage terms); the mask term's parameters ride the binding table's mask slot.
  ChainCoverageGraph coverage = {};
  const AOTEffectGraph* coverageGraph = nullptr;
  /// The instruction count the demand statistics ran over (the pass nodes plus the coverage
  /// subtree), reported for the profile resolution.
  size_t instructionCount = 0;
  /// The shader-side tiling recipes the leaves carry (tiled leaves the kernel's single-tap path
  /// serves through the interpreter's runtime loops, which the assembled text does not spell).
  size_t tiledRecipeCount = 0;
};

// The shared kind-to-op mapping and parameter admission for the unary pointwise operators; only
// the op and the checks matter — the values ride the upload-time readers off the node itself.
bool BuildPointwiseSlot(const AOTEffectGraph& graph, const AOTEffectNode* node, int* op);

// Whether a texture node's parameters describe a leaf the chain kernel samples itself: a resolved
// plain leaf (RGBAAA included), a device-space leaf, or a tiled leaf whose wrap modes the kernel's
// single-tap path implements. YUV planes need their own operator.
bool IsChainTextureLeaf(const AOTTextureParameters& parameters);

// Whether the texture's sampling needs shader-side wrap/border emulation (the tiled recipe carries
// a non-None shader mode). Shared by the binding builder and the graph walk, so the two cannot
// disagree about which leaves tile.
bool HasShaderTiling(const AOTTextureParameters& parameters);

/**
 * Lays one draw's expression out as a chain pass: derives the pass node set (the given pass when
 * not null, else the whole-graph derivation the assembled entry uses), merges the coverage terms,
 * orders the texture leaves ahead of the remaining nodes, encodes every node into its call site
 * and collects the leaf, noise and LUT-gradient records. Every acceptance rule the assembled
 * slice runs lives here — the walk and the binding builder both decline what this declines.
 * @param ir    The draw's lowered expression (color graph and coverage terms).
 * @param pass  The pass node set when a caller already planned one; null derives the whole-graph
 *              fused pass (the graph's own root and reachable set).
 * @param out   Receives the layout.
 * @return      False when the expression is outside the chain slice.
 */
bool LayoutChainGraph(const DrawIR& ir, const PassIR* pass, AOTChainGraphLayout* out);

/**
 * Builds the program's sampler binding table from the laid-out graph: one binding per leaf (a
 * multi-plane source owns three), the LUT gradient texture, the noise lookup tables, the phantom
 * padding the four-sampler artifacts need and the mask slot child, plus the leaf facts the writer
 * and the variant derivation read (the sampler child count, the mask and noise flags, the resolved
 * resource profile, the device-space leaves and the coord-source mask). This is the graph
 * replacement of the slot pipeline's binding stage: admission and the build run the same
 * derivation, so an admitted pass is by construction a buildable one.
 * @param ir    The draw's lowered expression (color graph and coverage terms).
 * @param pass  The pass node set when a caller already planned one; null derives the whole-graph
 *              fused pass.
 * @param out   Receives the program.
 * @return      False when the expression is outside the chain slice or a resource cannot be
 *              described.
 */
bool BuildChainProgram(const DrawIR& ir, const PassIR* pass, AOTProgram* out);

/**
 * A fit probe, not an admission: runs the graph layout alone (no binding table), which is what
 * the decomposer needs to decide whether a cut fits before any program exists. outProfile, when
 * not null, receives the resource profile the pass needs.
 */
bool ChainProgramFits(const DrawIR& ir, const PassIR& pass,
                      AOTResourceProfile* outProfile = nullptr);

/**
 * The coordinate transforms the assembled vertex stage consumes, in the order the geometry
 * processor writes them: for an extended profile, the chain-wide gradient/noise matrix first
 * (the source module reads it through the first transform slot — the graph, color or coverage,
 * is scanned for the one source), then one per sampler-binding child that samples through a
 * transform — every texture leaf, the LUT gradient and the padding. The device mask samples in
 * destination space and contributes none.
 */
std::vector<CoordTransform> BuildChainCoordTransforms(const DrawIR& ir, const AOTProgram& program);

/// Describes a graph that is its own pass: every non-designator node in index order, rooted at the
/// graph's root, with the resource profile the chain plan resolves. A cut segment graph and a
/// fused draw graph are both exactly that, so one derivation keeps them from disagreeing. Returns
/// false when the graph cannot fit one fused pass.
bool DeriveWholeGraphPass(const AOTEffectGraph& graph, PassIR* pass);

}  // namespace tgfx

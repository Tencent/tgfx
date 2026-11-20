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
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include "gpu/shaders/Family.h"
#include "gpu/shaders/contract/ChainBindings.h"
#include "gpu/shaders/contract/ChainComposition.h"
#include "gpu/shaders/contract/FragmentLibrary.h"

namespace tgfx {

class PrecompiledShaderCache;
struct DrawIR;

/**
 * Where one declared uniform member's value comes from at upload time. The slot names the chain
 * instruction a per-slot parameter belongs to; the index names the array element, the leaf or the
 * coordinate transform the member addresses.
 */
// ChainBindingSource lives in the contract area: the operator table and the build tool read it
// too.

/// One declared uniform member: its std140 placement and the reader the upload walks.
struct ChainBinding {
  ChainBindingSource source = ChainBindingSource::OutputSwizzle;
  int slot = -1;
  int index = -1;
  uint32_t offset = 0;
  uint32_t size = 0;
};

/// Which sampler-binding child one declared sampler reads.
enum class ChainSamplerSource : uint8_t {
  Leaf,
  Lut,
  NoiseLookup,
  Mask,
  DstTexture,
};

struct ChainSamplerBinding {
  std::string name = {};
  ChainSamplerSource source = ChainSamplerSource::Leaf;
  int index = 0;
};

/**
 * The emit's own description of an assembled chain program: the members the emitted fragment
 * declares (each with the reader that fills it), the stored vertex stage's members, the samplers
 * in declaration order, and the varying contract both stages carry. The upload walks these tables,
 * so the declarations and the writer cannot disagree: nothing exists that is not read.
 */
struct ChainBindings {
  std::vector<ChainBinding> vertex = {};
  std::vector<ChainBinding> fragment = {};
  std::vector<ChainSamplerBinding> samplers = {};
  /// The leaf coordinate varyings the composition reads, and whether the resource coordinates
  /// (gradient/noise) are read.
  std::set<int> leafVaryings = {};
  bool needsSourceCoords = false;
  /// The coordinate transform index offset of leaf 0: a resource profile gives transform 0 to the
  /// gradient/noise source, so its leaves start at 1. The stored vertex stage's declares and the
  /// interpreter's own hook follow the same shift.
  uint8_t leafTransformShift = 0;
  uint32_t fragmentBlockSize = 0;
};

/// How one call argument reaches the operator: a prior register, one of the frame's fixed inputs,
/// a declared uniform member, or a literal the plan baked at walk time.
enum class ChainArgKind : uint8_t {
  Register,
  GeomColor,
  OpaqueColor,
  UnitCoverage,
  White,
  Black,
  LeafTex,
  FragCoord,
  Uniform,
  Literal,
};

struct ChainArg {
  ChainArgKind kind = ChainArgKind::White;
  /// The register written by an earlier call (Register), the leaf slot (LeafTex), the uniform
  /// member's index into ChainProgramPlan::uniforms (Uniform), or the literal's value (Literal).
  int index = 0;
};

/// One instruction of the straight-line body: the operator, its arguments in call order, and the
/// register the result lands in. The inline expressions (InputOpaque, MulAlpha) are calls too, so
/// every backend spells the same data flow.
struct ChainCall {
  int op = 0;
  std::vector<ChainArg> args = {};
  int outRegister = 0;
  /// Blend's selector bit: the result re-multiplies by the geometry color's alpha.
  bool multiplyInputAlpha = false;
};

/// One declared uniform member in declaration order: its spelling, its std140 placement, and the
/// reader the upload walks. The offsets are the single layout every backend reads.
struct ChainUniformMember {
  std::string name = {};
  FragmentType type = FragmentType::Float;
  uint16_t count = 1;
  ChainBindingSource source = ChainBindingSource::OutputSwizzle;
  int slot = -1;
  int index = -1;
  uint32_t offset = 0;
  uint32_t size = 0;
};

/// One declared fragment input in declaration order. The spelling is the GLSL name; the location
/// is the variant's own (DeriveLayout), never renumbered per composition.
struct ChainVaryingDecl {
  std::string name = {};
  FragmentType type = FragmentType::Float;
};

/**
 * The backend-neutral description of one assembled chain program: the call list in topological
 * order, the declared uniform members, varyings and samplers in declaration order, the interface
 * facts the spelling follows, and the writer's binding tables. A printer turns this into GLSL
 * text today; the Vulkan and Metal routes consume the same plan.
 */
struct ChainProgramPlan {
  std::vector<ChainCall> calls = {};
  std::vector<ChainUniformMember> uniforms = {};
  std::vector<ChainVaryingDecl> varyings = {};
  std::vector<ChainSamplerBinding> samplers = {};
  /// Which fixed sources the body reads; each one becomes a source function on the text backends.
  bool geomColorUsed = false;
  bool opaqueColorUsed = false;
  bool unitCoverageUsed = false;
  bool needsSourceCoords = false;
  /// The variant facts the spelling depends on.
  bool hasColorVarying = false;
  bool hasSubset = false;
  bool hasLeafSamplers = false;
  bool hasMaskSampler = false;
  bool gradientLutForm = false;
  int hasXp = 0;
  /// The composition's register roles and the xfer's blend-pass fact.
  int rootRegister = 0;
  int clipCoverageRegister = -1;
  int coverageRootRegister = -1;
  bool xpNeedsBlendPass = false;
  /// The leaves the body samples and the operator set it reaches, both for the pack entries.
  std::set<int> leafSlots = {};
  std::set<int> usedOps = {};
  /// The writer's tables: the fragment bindings in member order, the vertex stage's members, the
  /// samplers, and the varying contract the vertex stage assembles against.
  ChainBindings bindings = {};
};

/**
 * Walks the composition into the neutral plan: one pass registers every uniform, varying and
 * sampler the shape reads (the declaration closure), resolves every argument, and collects the
 * facts a printer or a linker needs. The walk declines the same shapes the GLSL emit declined.
 * @param composition  The shape to plan.
 * @param family       The matched family (the variant walk's input).
 * @param variant      The matched variant (its geometry, texture count and xfer fact).
 * @param cache        The loaded bundle, for the slice's availability facts.
 * @param plan         Receives the plan.
 * @return             False when the shape is outside the assembled slice; the caller keeps its
 *                     other paths.
 */
bool PlanChainProgram(const ChainComposition& composition, const Family& family,
                      const Variant& variant, const PrecompiledShaderCache* cache,
                      ChainProgramPlan* plan);

/**
 * Plans the same program the composition walk plans, directly from the draw's lowered expression:
 * the node order the slot pipeline's schedule derives (texture leaves first, then the remaining
 * nodes in index order, color graph ahead of the merged coverage graph), each node's operator and
 * selector structure the encoders produce, the register assignment the shared allocator returns,
 * and the same declaration closure — none of it through the slot encoding. The graph route is the
 * replacement path for the assembled entry; the composition route stays the serving one until the
 * parity data says otherwise.
 * @param ir               The draw's lowered expression (color graph and coverage terms).
 * @param xpNeedsBlendPass The xfer's advanced-mode fact (the epilogue's blend pass).
 * @param family           The matched family (the variant walk's input).
 * @param variant          The matched variant (its geometry, texture count and xfer fact).
 * @param cache            The loaded bundle, for the slice's availability facts.
 * @param plan             Receives the plan.
 * @return                 False when the expression is outside the assembled slice.
 */
bool PlanChainProgramFromGraph(const DrawIR& ir, bool xpNeedsBlendPass, const Family& family,
                               const Variant& variant, const PrecompiledShaderCache* cache,
                               ChainProgramPlan* plan);

/**
 * The dual-route parity check: plans the same draw through both routes (the composition walk and
 * the graph walk) and compares the plans field by field — the calls in instruction order, every
 * declared member, varying, sampler and binding, and the epilogue registers. A disagreement is
 * logged and counted (a debug build asserts on the first), because the graph route replaces the
 * slot route in the assembled path; silence means the two spell the same program.
 * @param composition      The slot route's shape description of the draw.
 * @param ir               The draw's lowered expression (the graph route's input).
 * @param xpNeedsBlendPass The xfer's advanced-mode fact.
 * @param family           The matched family.
 * @param variant          The matched variant.
 * @param cache            The loaded bundle.
 */
void CheckChainProgramPlanParity(const ChainComposition& composition, const DrawIR& ir,
                                 bool xpNeedsBlendPass, const Family& family,
                                 const Variant& variant, const PrecompiledShaderCache* cache);

/**
 * The plan's per-leaf member-naming rule: a leaf's records carry the base name at leaf 0 and the
 * base name with the leaf index suffix beyond it (Subset, Subset_1, Subset_2, ...). The walk
 * registers the members under these names and every printer spells them the same way.
 * @param base  The record's base name (for example "Subset").
 * @param leaf  The leaf slot the record belongs to.
 * @return      The member's declared name.
 */
std::string ChainLeafMemberName(const char* base, int leaf);

/**
 * Copies one pack entry's text out of the loaded bundle. An empty result means the entry is
 * absent; the walk reads only availability, the printers read the text.
 * @param cache  The loaded bundle.
 * @param id     The pack entry's identifier.
 * @return       The entry's text, or empty when the bundle carries no such entry.
 */
std::string ChainPackText(const PrecompiledShaderCache* cache, uint32_t id);

}  // namespace tgfx

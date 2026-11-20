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
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/shaders/Family.h"
#include "gpu/shaders/contract/ChainBindings.h"
#include "gpu/shaders/contract/FragmentLibrary.h"
#include "tgfx/core/BytesKey.h"

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

struct AOTEffectNode;

/// One declared uniform member: its std140 placement and the reader the upload walks. The node is
/// the graph site the member's operator was encoded from — the parameter readers address their
/// values through it instead of any intermediate instruction encoding.
struct ChainBinding {
  ChainBindingSource source = ChainBindingSource::OutputSwizzle;
  int slot = -1;
  int index = -1;
  uint32_t offset = 0;
  uint32_t size = 0;
  const AOTEffectNode* node = nullptr;
};

/// The per-vertex color varying's suffix: the rect layouts declare it bare (vColor), the
/// per-pixel geometries with their module prefix (EllipseGeometry_vColor, ...). One constant
/// serves every matcher of the name, so the spelling has one authority.
inline constexpr const char* ColorVaryingSuffix = "_vColor";

/// Whether a varying name is the per-vertex color: bare or module-prefixed (see
/// ColorVaryingSuffix).
inline bool IsColorVaryingName(const std::string& name) {
  const std::string suffix = ColorVaryingSuffix;
  return name == "vColor" ||
         (name.size() > suffix.size() &&
          name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0);
}

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
/// reader the upload walks. The offsets are the single layout every backend reads. The node names
/// the graph site the member's operator was encoded from (see ChainBinding).
struct ChainUniformMember {
  std::string name = {};
  FragmentType type = FragmentType::Float;
  uint16_t count = 1;
  ChainBindingSource source = ChainBindingSource::OutputSwizzle;
  int slot = -1;
  int index = -1;
  uint32_t offset = 0;
  uint32_t size = 0;
  const AOTEffectNode* node = nullptr;
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
  /// The leaves whose recipe names a shader-side tiled sample: the printer maps their fetch
  /// through the tiled catalog functions with the recipe's own uniforms.
  std::set<int> tiledLeafSlots = {};
  /// The variant's geometry index: a per-pixel-coverage geometry (anything but the rect layouts)
  /// makes the walk declare the geometry's own varyings and ChainColorParams switches, and the
  /// printer spell the edge-coverage call ahead of the body.
  uint8_t geometry = 0;
  /// The writer's tables: the fragment bindings in member order, the vertex stage's members, the
  /// samplers, and the varying contract the vertex stage assembles against.
  ChainBindings bindings = {};
};

/// One argument of a per-pixel geometry coverage call: a varying the plan declares alongside
/// (prefixed fragment name, the stored variant's own declaration) or a ChainColorParams uniform
/// the walk registers (with the reader that fills it).
struct ChainGeometryCoverageArg {
  const char* name = nullptr;
  FragmentType type = FragmentType::Float;
  bool uniform = false;
  ChainBindingSource source = ChainBindingSource::GeometryStroke;
};

/// One per-pixel geometry coverage call: the variant's geometry index, the catalog function and
/// its arguments in call order. One table serves the walk (which declares the members) and the
/// printer (which spells the call), so the two cannot disagree about a geometry's contract.
struct ChainGeometryCoverageCall {
  uint8_t geometry = 0;
  const char* function = nullptr;
  const ChainGeometryCoverageArg* args = nullptr;
  size_t argCount = 0;
};

/** The per-pixel coverage call of a geometry index, or null when the geometry carries its
 * coverage through the vCoverage varying instead (the rect layouts). */
const ChainGeometryCoverageCall* ChainGeometryCoverageOf(uint8_t geometry);

/**
 * Plans the chain program directly from the draw's lowered expression: the node order the layout
 * derives (texture leaves first, then the remaining nodes in index order, color graph ahead of the
 * merged coverage graph), each node's operator and selector structure, the register assignment
 * the shared allocator returns, and the declaration closure. This is the assembled entry's one
 * and only serving path; the uniform members carry the graph nodes their operators were encoded
 * from, so the upload reads every parameter straight off the node.
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

/**
 * Folds the plan's structure into a program key: the calls in instruction order, the epilogue
 * registers and the interface facts the spelling follows — everything the assembled text derives
 * from, and nothing else. The assembled text carries no parameter values (the upload reads those
 * through the binding table), so two draws of one variant share a program exactly when their
 * plans agree; folding the plan here cannot miss a structural fact the way a hand-rolled shape
 * digest could.
 * @param plan  The planned program.
 * @param key   The key to append to.
 */
void AppendChainPlanKey(const ChainProgramPlan& plan, BytesKey* key);

}  // namespace tgfx

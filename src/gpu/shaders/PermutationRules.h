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

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "gpu/AOTResourceProfile.h"
#include "gpu/shaders/ShaderPermutation.h"

namespace tgfx {

// ---------------------------------------------------------------------------------------
// Design charter: what becomes a compile-time permutation dimension
// ---------------------------------------------------------------------------------------
// A permutation dimension is justified ONLY when it changes a resource binding or a varying
// layout. Everything else — value math, operator selection, mode flags, colors — must ride a
// runtime uniform. When reviewing a new dimension, ask what ABI difference it gates; if the
// answer is "none", make it a uniform instead.
//
// The Compose functions below are the single source of truth for the variant semantics: the
// runtime matcher feeds them real inputs (via Extract in PermutationMatcher.cpp) and compiles
// exactly what survives. Rejections that depend on dimension values must therefore live inside
// Compose — the enumeration must see the same rejection the runtime applies — while whole-draw
// rejections (wrong processor kind, unsupported shapes) stay in Extract where they never shape
// the reachable set.
//
// The compiled variant list is enumerated two ways:
//   - Pass-through families (Compose maps every input-axis value onto the vert/frag domains
//     one-to-one) enumerate the mirrored domain product directly via EnumerateMirroredReachable
//     in PermutationRules.cpp; there is no per-family input loop to keep in sync.
//   - Chain/Graph and YUV keep dedicated input-lattice enumerators, because their Compose is not
//     a pass-through (geometry normalization, resource caps, format folding).
//
// ---------------------------------------------------------------------------------------
// Three kinds of precompiled shader, and what each is for
// ---------------------------------------------------------------------------------------
//   1. Dedicated shaders (the level1/*Shader.h families other than Pointwise*): a fixed pattern
//      with its own fast kernel; neighborhood work (blur, glass) lives here too. Add one when
//      hit data or a measurement shows the pattern is worth a kernel of its own. Remove one only
//      after every case it served is served another way — "zero hits in a sample" is not proof.
//   2. Pointwise Chain/Graph: one per-pixel DAG interpreter. Graph is the same kernel with the
//      noise or gradient module compiled in (the extended resource kinds derive from the Chain domains
//      and enumeration; it stays a separate family because its vertex stage also carries the
//      source-coordinate varying). New ops belong here only if they are cheap, per-pixel and
//      general: every op is compiled into every variant. Do not grow the 16/32 slot capacities
//      to fit longer compositions.
//   3. Everything else: split into several passes where the planner supports it (only a linear
//      run of single-input ops over one plain texture today), otherwise it is refused. With a
//      loaded bundle there is no JIT fallback — a refused draw with no precompiled program is
//      dropped, so check coverage after adding or changing anything.
//
// Adding a family:  family header (dims, info(), TGFX_REGISTER_SHADER) + GLSL + the Match
//   function in the family's own .cpp + Inputs, Compose, and the Enumerate declaration in this
//   file/.cpp (a pass-through family only needs the Enumerate one-liner). A new uniform field
//   also needs a writer in the shared onSetData of the processor that owns it.
// Removing a family: delete the same places in one change, and keep the "removed family stays
//   unregistered" assertion. No variant totals to update: PrecompiledBundleLoad ties the reachable
//   sets to the bundle's entry counts.
// Changing dimensions: apply the admission test in ShaderPermutation.h, keep vertex/fragment
//   mirrored dimensions in step, and diff the reachable set before and after. A change to a
//   Chain dimension also reshapes Graph and re-encodes both bundles (bump the
//   toolchain ABI in PrecompiledBundleIdentity.h).
//
// ---------------------------------------------------------------------------------------
// Contract: adding a rule input field
// ---------------------------------------------------------------------------------------
// Adding a field to any *Inputs struct below must be synced in two places:
//   1. Extract (PermutationMatcher.cpp) — assign the field from the ProgramInfo.
//   2. Compose (this file) — read the field where it shapes dimensions or rejects.
// For a pass-through family that is the whole job when the field only shapes dimension values.
// If the field makes Compose REJECT a combination, the rejection must live in one shared
// validity function that both Compose and the family's Enumerate* call (see the *ValuesAllowed
// functions in PermutationRules.cpp) — never as two independently restated expressions. A
// diverging copy silently OVER-compiles variants the runtime never requests; the pair-total
// assertion in ShaderPermutationTest.cpp catches the count change but not which family drifted.
// When adding such a predicate, diff the new reachable set against the previous one.
// Chain/Graph/YUV instead keep the input-lattice
// contract: add a loop layer covering every value the new field can take, or their bundles miss
// variants and the draws silently lose precompiled coverage (with a loaded bundle the draw is
// dropped, not recompiled).
// ---------------------------------------------------------------------------------------

/**
 * Input contract of the RoundStrokeRectFillShader matcher rule. Only predicates that shape
 * dimension values belong here; whole-draw rejections (wrong geometry processor kind,
 * perspective UV) stay in the runtime Extract step and never affect the reachable set.
 */
struct RoundStrokeRectInputs {
  bool hasUVMatrix = false;
  int xpType = XP_UNSUPPORTED;  // no representable XferProcessor
};

/**
 * Input contract of the QuadColorFillShader matcher rule. The coverage FP classification
 * (none, direct AARect, or device mask) happens in Extract: only the resulting mask flag and the
 * transfer type shape dimensions.
 */
struct QuadColorFillInputs {
  int xpType = XP_UNSUPPORTED;  // -1 = no representable XferProcessor.
  bool hasMaskTexture = false;  // device-space mask coverage FP present.
};

/**
 * Input contract of the ComplexEllipseFillShader matcher rule.
 */
struct ComplexEllipseFillInputs {
  bool isStroke = false;
  int xpType = XP_UNSUPPORTED;  // no representable XferProcessor
};

/**
 * Input contract of the NonAARRectFillShader matcher rule. The TiledTextureEffect shape checks
 * (single-tap modes only, no perspective) happen in Extract; the textured variant never
 * combines with stroke, which Compose enforces.
 */
struct NonAARRectFillInputs {
  bool isStroke = false;
  bool textured = false;
  int xpType = XP_UNSUPPORTED;  // no representable XferProcessor
};

/**
 * Input contract of the ComplexNonAARRectFillShader matcher rule.
 */
struct ComplexNonAARRectFillInputs {
  bool isStroke = false;
  int xpType = XP_UNSUPPORTED;  // no representable XferProcessor
};

/**
 * Input contract of the MeshFillShader matcher rule. The color and coverage attributes are
 * unconditional (white baked for meshes without per-vertex colors, a constant 1.0f coverage), so
 * no input fields exist for them.
 */
struct MeshFillInputs {
  bool hasTexCoords = false;
  int xpType = XP_UNSUPPORTED;  // no representable XferProcessor
};

/**
 * Input contract of the YUVTextureFillShader matcher rule. yuvFormat is the plane layout index
 * (0 = I420, 1 = NV12); unsupported formats map to -1 and are rejected in Compose so the
 * enumeration sees the same rejection as the runtime.
 */
struct YUVTextureFillInputs {
  bool hasUVMatrix = false;
  int yuvFormat = -1;           // -1 = unsupported plane layout.
  int xpType = XP_UNSUPPORTED;  // -1 = no representable XferProcessor.
};

/**
 * Input contract of the ShapeInstancedTextureCoverageShader matcher rule. The kernel is
 * EmptyXP-only and the gradient/texture structure checks are whole-draw rejections in Extract.
 * The instance color attribute is unconditional (colorless batches broadcast white), so both
 * forms read the same vColor varying and no color input field exists.
 */
struct ShapeInstancedTextureCoverageInputs {
  bool gradient = false;
};

/**
 * Input contract of the ShapeInstancedFillShader matcher rule. The instance color attribute is
 * unconditional (colorless batches broadcast white), so no color input field exists.
 */
struct ShapeInstancedFillInputs {
  int xpType = XP_UNSUPPORTED;  // no representable XferProcessor
};

/**
 * Input contract of the QuadTextureFillShader matcher rule. The coverage FP classification
 * (device mask = runtime uniform, local mask = dimension, else reject) and the TextureEffect
 * shape checks happen in Extract; alphaOnly / RGBAAA are runtime uniforms.
 */
struct QuadTextureFillInputs {
  bool hasUVCoord = false;
  bool hasSubset = false;
  bool hasLocalMask = false;
  int xpType = XP_UNSUPPORTED;  // no representable XferProcessor
};

/**
 * Input contract of the GlassRefractionShader matcher rule. The GP selects the GP_KIND dimension
 * (0=ellipse common color, 1=quad common color in its uvMatrix form with per-edge coverage)
 * because it changes the vertex attribute layout and the initial-coverage source. The geometry
 * child selects the GEOMETRY_KIND dimension (0=SDF rounded rect, 1=SDF ellipse, 2=UDF, 3=UDF +
 * edge light) because it changes the sampler layout. Dispersion selects DISPERSION_ON because the
 * runtime folds the same predicate into the program key and emits only one tap path; the lighting
 * branch rides a runtime uniform since it cannot move a texture coordinate.
 */
struct GlassRefractionInputs {
  int gpKind = -1;              // -1 = unrepresentable geometry processor form.
  int geometryKind = -1;        // -1 = unrepresentable geometry child.
  bool dispersionOn = false;    // Mirrors GlassRefractionFragmentProcessor::hasDispersion().
  int xpType = XP_UNSUPPORTED;  // -1 = no representable XferProcessor.
};

/**
 * Input contract of the AtlasTextFillShader matcher rule. The optional device-space mask
 * structure checks are whole-draw rejections in Extract; the mask is a runtime uniform.
 */
struct AtlasTextFillInputs {
  bool hasCoverage = false;
  int xpType = XP_UNSUPPORTED;  // no representable XferProcessor
};

/**
 * Shared input contract for the PointwiseChainShader rule (plain chains and the extended
 * resource kinds). gpKind selects
 * (0 = DefaultGP, 1 = QuadPerEdgeAA, 2 = Mesh, 3 = sampler-free AtlasText twin, 4 = Ellipse,
 * 5 = HairlineLine, 6 = ComplexEllipse fill, 7 = ComplexEllipse stroke);
 * each profile fixes which attribute slots its vertex buffers carry, and Compose rejects
 * profile-inconsistent combinations. The chain slot topology (ops, indices) is runtime data;
 * only the leaf-count class, the mask flag, the coverage-subtree flag, and the transfer type
 * shape the shared dimensions. TiledGraph adds a resource-kind value for its single gradient and
 * three tiled recipes; NoiseGraph retains its fixed position-based, common-color AA quad subset.
 */
struct PointwiseChainInputs {
  int gpKind = 0;
  bool hasCoverage = false;         // DefaultGP: AA coverage; quad/mesh: always true.
  bool hasUVCoord = false;          // quad/mesh: texCoord slot; atlas: always true.
  int textureCount = 0;             // domain value: 0 = leaf-free, 1 = four-leaf padded slots.
  bool hasMask = false;             // device-space mask child present.
  bool hasCoverageSubtree = false;  // chain coverage subtree present.
  bool hasSubset = false;           // quad four-leaf chain: per-quad subset of a strict draw.
  bool hasNoise = false;
  bool hasGradient = false;  // one gradient belongs to the extended TiledGraph profile.
  AOTResourceProfile resourceProfile = AOTResourceProfile::Automatic;
  int xpType = XP_UNSUPPORTED;  // no representable XferProcessor
};

/**
 * Pure mapping from rule inputs to dimension values (RuleComposedValues lives in
 * ShaderPermutation.h); unsupported combinations return nullopt.
 * This is the single source of truth for what the rule can produce: the runtime matcher feeds it
 * real inputs, while the build tool feeds it every input combination, so the compiled variant
 * list and the matcher's reachable set can never drift apart.
 */
std::optional<RuleComposedValues> ComposeRoundStrokeRect(const RoundStrokeRectInputs& inputs);

/**
 * Pure mapping for the QuadColorFillShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeQuadColorFill(const QuadColorFillInputs& inputs);

/**
 * Pure mapping for the ComplexEllipseFillShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeComplexEllipseFill(const ComplexEllipseFillInputs& inputs);

/**
 * Pure mapping for the NonAARRectFillShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeNonAARRectFill(const NonAARRectFillInputs& inputs);

/**
 * Pure mapping for the ComplexNonAARRectFillShader rule; see ComposeRoundStrokeRect for the
 * sharing contract.
 */
std::optional<RuleComposedValues> ComposeComplexNonAARRectFill(
    const ComplexNonAARRectFillInputs& inputs);

/**
 * Pure mapping for the MeshFillShader rule; see ComposeRoundStrokeRect for the sharing contract.
 */
std::optional<RuleComposedValues> ComposeMeshFill(const MeshFillInputs& inputs);

/**
 * Pure mapping for the YUVTextureFillShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeYUVTextureFill(const YUVTextureFillInputs& inputs);

/**
 * Pure mapping for the ShapeInstancedTextureCoverageShader rule; see ComposeRoundStrokeRect for
 * the sharing contract.
 */
std::optional<RuleComposedValues> ComposeShapeInstancedTextureCoverage(
    const ShapeInstancedTextureCoverageInputs& inputs);

/**
 * Pure mapping for the ShapeInstancedFillShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeShapeInstancedFill(const ShapeInstancedFillInputs& inputs);

/**
 * Pure mapping for the QuadTextureFillShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeQuadTextureFill(const QuadTextureFillInputs& inputs);

/**
 * Pure mapping for the GlassRefractionShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeGlassRefraction(const GlassRefractionInputs& inputs);

/**
 * Pure mapping for the AtlasTextFillShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeAtlasTextFill(const AtlasTextFillInputs& inputs);

/**
 * Pure mapping for the PointwiseChainShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposePointwiseChain(const PointwiseChainInputs& inputs);

/** Family enumerators bound alongside their factories by TGFX_REGISTER_SHADER. */
std::set<std::pair<uint32_t, uint32_t>> EnumerateRoundStrokeRectReachable();
std::set<std::pair<uint32_t, uint32_t>> EnumerateQuadColorFillReachable();
std::set<std::pair<uint32_t, uint32_t>> EnumerateComplexEllipseFillReachable();
std::set<std::pair<uint32_t, uint32_t>> EnumerateNonAARRectFillReachable();
std::set<std::pair<uint32_t, uint32_t>> EnumerateComplexNonAARRectFillReachable();
std::set<std::pair<uint32_t, uint32_t>> EnumerateMeshFillReachable();
std::set<std::pair<uint32_t, uint32_t>> EnumerateYUVTextureFillReachable();
std::set<std::pair<uint32_t, uint32_t>> EnumerateShapeInstancedTextureCoverageReachable();
std::set<std::pair<uint32_t, uint32_t>> EnumerateShapeInstancedFillReachable();
std::set<std::pair<uint32_t, uint32_t>> EnumerateQuadTextureFillReachable();
std::set<std::pair<uint32_t, uint32_t>> EnumerateGlassRefractionReachable();
std::set<std::pair<uint32_t, uint32_t>> EnumerateAtlasTextFillReachable();

/**
 * Returns every (vertIndex, fragIndex) pair the PointwiseChain rule can ever produce: the plain
 * chains plus the extended resource kinds (the pre-merge Graph family's gradient mirror and noise
 * subset, folded in).
 */
std::set<std::pair<uint32_t, uint32_t>> EnumeratePointwiseChainReachable();

/**
 * Returns the reachable permutation set for a shader whose matcher rule has been migrated to the
 * Compose pattern, or nullopt when the rule has not been migrated yet.
 */
std::optional<std::set<std::pair<uint32_t, uint32_t>>> EnumerateReachablePermutations(
    const std::string& shaderName);

}  // namespace tgfx

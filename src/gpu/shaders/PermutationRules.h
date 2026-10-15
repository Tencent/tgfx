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

namespace tgfx {

// ---------------------------------------------------------------------------------------
// Design charter: what becomes a compile-time permutation dimension
// ---------------------------------------------------------------------------------------
// A permutation dimension is justified ONLY when it changes a resource binding or a varying
// layout. Everything else — value math, operator selection, mode flags, colors — must ride a
// runtime uniform. When reviewing a new dimension, ask what ABI difference it gates; if the
// answer is "none", make it a uniform instead.
//
// The Compose functions below are the single source of truth for the compiled variant list:
// the runtime matcher feeds them real inputs (via Extract in PermutationMatcher.cpp), while the
// build tool feeds them the full input lattice (via the Enumerate functions) and compiles
// exactly what survives. Rejections that depend on dimension values must therefore live inside
// Compose — the enumeration must see the same rejection the runtime applies — while whole-draw
// rejections (wrong processor kind, unsupported shapes) stay in Extract where they never shape
// the reachable set.
//
// ---------------------------------------------------------------------------------------
// Contract: adding a rule input field
// ---------------------------------------------------------------------------------------
// Adding a field to any *Inputs struct below must be synced in three places:
//   1. Extract (PermutationMatcher.cpp) — assign the field from the ProgramInfo.
//   2. Compose (this file) — read the field where it shapes dimensions or rejects.
//   3. Enumerate (this file) — add a loop layer covering every value the field can take.
// Forgetting step 3 leaves the field at its default in the enumeration, so the bundle misses
// those variants and the affected draws fall back to runtime compilation silently. The
// shader_build_tool --audit mode validates structural validity of the reachable set only, NOT
// lattice completeness — the Enumerate loops are the sole declaration of the input space.
// ---------------------------------------------------------------------------------------

/**
 * Input contract of the RoundStrokeRectFillShader matcher rule. Only predicates that shape
 * dimension values belong here; whole-draw rejections (wrong geometry processor kind,
 * perspective UV) stay in the runtime Extract step and never affect the reachable set.
 */
struct RoundStrokeRectInputs {
  bool isCoverageAA = false;
  bool hasCommonColor = false;
  bool hasUVMatrix = false;
  int xpType = -1;  // -1 = no representable XferProcessor.
};

/**
 * Input contract of the MaskFillShader matcher rule. The mask TextureEffect shape checks
 * (alpha-only, no subset, no RGBAAA, no YUV) are whole-draw rejections and stay in Extract.
 */
struct MaskFillInputs {
  int xpType = -1;  // -1 = no representable XferProcessor.
};

/**
 * Input contract of the HairlineLineShader matcher rule. The direct AARect clip coverage check
 * is a whole-draw rejection and stays in Extract.
 */
struct HairlineLineInputs {
  int xpType = -1;  // -1 = no representable XferProcessor.
};

/**
 * Input contract of the HairlineQuadShader matcher rule; same shape as HairlineLineShader.
 */
struct HairlineQuadInputs {
  int xpType = -1;  // -1 = no representable XferProcessor.
};

/**
 * Input contract of the QuadColorFillShader matcher rule. The coverage FP classification
 * (none, direct AARect, or device mask) happens in Extract: only the resulting mask flag and the
 * transfer type shape dimensions.
 */
struct QuadColorFillInputs {
  int xpType = -1;              // -1 = no representable XferProcessor.
  bool hasMaskTexture = false;  // device-space mask coverage FP present.
};

/**
 * Input contract of the SolidColorFillShader matcher rule. The direct AARect clip coverage check
 * is a whole-draw rejection and stays in Extract.
 */
struct SolidColorFillInputs {
  bool isCoverageAA = false;
  int xpType = -1;  // -1 = no representable XferProcessor.
};

/**
 * Input contract of the ComplexEllipseFillShader matcher rule.
 */
struct ComplexEllipseFillInputs {
  bool isStroke = false;
  bool hasCommonColor = false;
  int xpType = -1;  // -1 = no representable XferProcessor.
};

/**
 * Input contract of the EllipseFillShader matcher rule. deviceMask is the shared coverage
 * classification result (0 = none, 1 = device-space mask); the LocalMask/unclassifiable cases are
 * whole-draw rejections and stay in Extract.
 */
struct EllipseFillInputs {
  bool hasCommonColor = false;
  int deviceMask = 0;  // 0 = no mask coverage FP, 1 = device-space mask.
  int xpType = -1;     // -1 = no representable XferProcessor.
};

/**
 * Input contract of the NonAARRectFillShader matcher rule. The TiledTextureEffect shape checks
 * (single-tap modes only, no perspective) happen in Extract; the textured variant never
 * combines with stroke, which Compose enforces.
 */
struct NonAARRectFillInputs {
  bool hasCommonColor = false;
  bool isStroke = false;
  bool textured = false;
  int xpType = -1;  // -1 = no representable XferProcessor.
};

/**
 * Input contract of the ComplexNonAARRectFillShader matcher rule.
 */
struct ComplexNonAARRectFillInputs {
  bool hasCommonColor = false;
  bool isStroke = false;
  int xpType = -1;  // -1 = no representable XferProcessor.
};

/**
 * Input contract of the MeshFillShader matcher rule.
 */
struct MeshFillInputs {
  bool hasTexCoords = false;
  bool hasColors = false;
  bool hasCoverage = false;
  int xpType = -1;  // -1 = no representable XferProcessor.
};

/**
 * Input contract of the PerlinNoiseFillShader matcher rule. The GP kind (Default or QuadAA),
 * the quad attribute constraints, and the Perlin + optional pointwise FP structure checks are
 * whole-draw rejections in Extract: the folded pointwise operator rides a runtime uniform and
 * shapes no dimension, so only coverage and the transfer type remain.
 */
struct PerlinNoiseFillInputs {
  bool hasCoverage = false;
  int xpType = -1;  // -1 = no representable XferProcessor.
};

/**
 * Input contract of the YUVTextureFillShader matcher rule. yuvFormat is the plane layout index
 * (0 = I420, 1 = NV12); unsupported formats map to -1 and are rejected in Compose so the
 * enumeration sees the same rejection as the runtime.
 */
struct YUVTextureFillInputs {
  bool hasUVMatrix = false;
  int yuvFormat = -1;  // -1 = unsupported plane layout.
  int xpType = -1;     // -1 = no representable XferProcessor.
};

/**
 * Input contract of the ShapeInstancedTextureCoverageShader matcher rule. The kernel is
 * EmptyXP-only and the gradient/texture structure checks are whole-draw rejections in Extract;
 * the bare coverage form requires per-instance colors, which Compose enforces.
 */
struct ShapeInstancedTextureCoverageInputs {
  bool gradient = false;
  bool hasColors = false;
};

/**
 * Input contract of the ShapeInstancedFillShader matcher rule.
 */
struct ShapeInstancedFillInputs {
  bool hasColors = false;
  bool isCoverageAA = false;
  int xpType = -1;  // -1 = no representable XferProcessor.
};

/**
 * Input contract of the TiledTextureFillShader matcher rule. The tiled mode / perspective checks
 * are whole-draw rejections in Extract; ALPHA_ONLY / strict are runtime uniforms.
 */
struct TiledTextureFillInputs {
  bool hasCoverage = false;
  int xpType = -1;  // -1 = no representable XferProcessor.
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
  int xpType = -1;  // -1 = no representable XferProcessor.
};

/**
 * Input contract of the GaussianBlur1DShader matcher rule. The blur child structure checks,
 * tiled-mode limits, and sigma range are whole-draw rejections in Extract; sigma and the tiled
 * child are runtime uniforms, so only the transfer type shapes dimensions.
 */
struct GaussianBlur1DInputs {
  int xpType = -1;  // -1 = no representable XferProcessor.
};

/**
 * Input contract of the GlassUDFTentBlurShader matcher rule. The child structure checks, tiled-mode
 * limits, and the max-radius bound are whole-draw rejections in Extract; the radius pair, field
 * selection, packed-input decode, and the tiled child are runtime uniforms, so only the transfer
 * type shapes dimensions.
 */
struct GlassUDFTentBlurInputs {
  int xpType = -1;  // -1 = no representable XferProcessor.
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
  int gpKind = -1;            // -1 = unrepresentable geometry processor form.
  int geometryKind = -1;      // -1 = unrepresentable geometry child.
  bool dispersionOn = false;  // Mirrors GlassRefractionFragmentProcessor::hasDispersion().
  int xpType = -1;            // -1 = no representable XferProcessor.
};

/**
 * Input contract of the TexturedEffectShader matcher rule (the composed texture transform). The
 * Compose(Texture, pointwise) structure checks are whole-draw rejections in Extract: the operator
 * rides the OpType runtime uniform, so only coverage and the transfer type shape dimensions.
 */
struct TexturedEffectInputs {
  bool hasCoverage = false;
  int xpType = -1;  // -1 = no representable XferProcessor.
};

/**
 * Input contract of the AtlasTextFillShader matcher rule. The optional device-space mask
 * structure checks are whole-draw rejections in Extract; the mask is a runtime uniform.
 */
struct AtlasTextFillInputs {
  bool hasCoverage = false;
  bool hasCommonColor = false;
  int xpType = -1;  // -1 = no representable XferProcessor.
};

/**
 * Shared input contract for the PointwiseChainShader/PointwiseGraphShader rules. gpKind selects
 * (0 = DefaultGP, 1 = QuadPerEdgeAA, 2 = Mesh, 3 = sampler-free AtlasText twin, 4 = Ellipse);
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
  bool hasColor = false;            // per-vertex color slot (not DefaultGP).
  int textureCount = 0;             // domain value: 0 = leaf-free, 1 = four-leaf padded slots.
  bool hasMask = false;             // device-space mask child present.
  bool hasCoverageSubtree = false;  // chain coverage subtree present.
  bool hasNoise = false;
  bool hasGradient = false;  // one gradient belongs to the extended TiledGraph profile.
  AOTResourceProfile resourceProfile = AOTResourceProfile::Automatic;
  int xpType = -1;  // -1 = no representable XferProcessor.
};

/**
 * Dimension values a matcher rule produces for both stages, before domain encoding.
 */
struct RuleComposedValues {
  std::vector<int> vertValues;
  std::vector<int> fragValues;
};

/**
 * Pure mapping from rule inputs to dimension values; unsupported combinations return nullopt.
 * This is the single source of truth for what the rule can produce: the runtime matcher feeds it
 * real inputs, while the build tool feeds it every input combination, so the compiled variant
 * list and the matcher's reachable set can never drift apart.
 */
std::optional<RuleComposedValues> ComposeRoundStrokeRect(const RoundStrokeRectInputs& inputs);

/**
 * Pure mapping for the MaskFillShader rule; see ComposeRoundStrokeRect for the sharing contract.
 */
std::optional<RuleComposedValues> ComposeMaskFill(const MaskFillInputs& inputs);

/**
 * Pure mapping for the HairlineLineShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeHairlineLine(const HairlineLineInputs& inputs);

/**
 * Pure mapping for the HairlineQuadShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeHairlineQuad(const HairlineQuadInputs& inputs);

/**
 * Pure mapping for the QuadColorFillShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeQuadColorFill(const QuadColorFillInputs& inputs);

/**
 * Pure mapping for the SolidColorFillShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeSolidColorFill(const SolidColorFillInputs& inputs);

/**
 * Pure mapping for the ComplexEllipseFillShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeComplexEllipseFill(const ComplexEllipseFillInputs& inputs);

/**
 * Pure mapping for the EllipseFillShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeEllipseFill(const EllipseFillInputs& inputs);

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
 * Pure mapping for the PerlinNoiseFillShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposePerlinNoiseFill(const PerlinNoiseFillInputs& inputs);

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
 * Pure mapping for the TiledTextureFillShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeTiledTextureFill(const TiledTextureFillInputs& inputs);

/**
 * Pure mapping for the QuadTextureFillShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeQuadTextureFill(const QuadTextureFillInputs& inputs);

/**
 * Pure mapping for the GaussianBlur1DShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeGaussianBlur1D(const GaussianBlur1DInputs& inputs);

/**
 * Pure mapping for the GlassUDFTentBlurShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeGlassUDFTentBlur(const GlassUDFTentBlurInputs& inputs);

/**
 * Pure mapping for the GlassRefractionShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeGlassRefraction(const GlassRefractionInputs& inputs);

/**
 * Pure mapping for the TexturedEffectShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeTexturedEffect(const TexturedEffectInputs& inputs);

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

/**
 * Maps TiledGraph through the shared chain GP contract and NoiseGraph through its fixed quad
 * subset, adding the fragment-stage resource-kind dimension.
 */
std::optional<RuleComposedValues> ComposePointwiseGraph(const PointwiseChainInputs& inputs);

/**
 * Returns every (vertIndex, fragIndex) pair the PointwiseChain rule can ever produce.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumeratePointwiseChainReachable();

/**
 * Returns the chain-reachable pairs mapped to TiledGraph plus the fixed NoiseGraph quad pairs.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumeratePointwiseGraphReachable();

/**
 * Returns the reachable permutation set for a shader whose matcher rule has been migrated to the
 * Compose pattern, or nullopt when the rule has not been migrated yet.
 */
std::optional<std::set<std::pair<uint32_t, uint32_t>>> EnumerateReachablePermutations(
    const std::string& shaderName);

}  // namespace tgfx

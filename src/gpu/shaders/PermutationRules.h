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

namespace tgfx {

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
 * Input contract of the ConstColorShader matcher rule. The ConstColorProcessor inputMode is a
 * runtime uniform, so only the transfer type shapes dimensions.
 */
struct ConstColorInputs {
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
 * Input contract of the QuadConstColorShader matcher rule. The EmptyXferProcessor requirement is
 * a whole-draw rejection and stays in Extract.
 */
struct QuadConstColorInputs {
  bool hasUVMatrix = false;
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
 * Input contract of the TextureColorMatrixShader matcher rule. alphaOnly / hasRGBAAA / subset are
 * runtime uniforms, so only the transfer type shapes dimensions; the TextureEffect + ColorMatrix
 * structure checks are whole-draw rejections in Extract.
 */
struct TextureColorMatrixInputs {
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
 * Input contract of the DeviceSpaceTextureShader matcher rule. ALPHA_ONLY is a runtime uniform,
 * so only coverage and the transfer type shape dimensions.
 */
struct DeviceSpaceTextureInputs {
  bool hasCoverage = false;
  int xpType = -1;  // -1 = no representable XferProcessor.
};

/**
 * Input contract of the DeviceSpaceTexturedEffectShader matcher rule. The GP kind, quad
 * attribute constraints, and Compose(DeviceSpaceTexture, pointwise) structure checks are
 * whole-draw rejections in Extract; only coverage and the transfer type shape dimensions.
 */
struct DeviceSpaceTexturedEffectInputs {
  bool hasCoverage = false;
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
 * Pure mapping for the ConstColorShader rule; see ComposeRoundStrokeRect for the sharing contract.
 */
std::optional<RuleComposedValues> ComposeConstColor(const ConstColorInputs& inputs);

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
 * Pure mapping for the QuadConstColorShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeQuadConstColor(const QuadConstColorInputs& inputs);

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
 * Pure mapping for the TextureColorMatrixShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeTextureColorMatrix(
    const TextureColorMatrixInputs& inputs);

/**
 * Pure mapping for the YUVTextureFillShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeYUVTextureFill(const YUVTextureFillInputs& inputs);

/**
 * Pure mapping for the DeviceSpaceTextureShader rule; see ComposeRoundStrokeRect for the sharing
 * contract.
 */
std::optional<RuleComposedValues> ComposeDeviceSpaceTexture(
    const DeviceSpaceTextureInputs& inputs);

/**
 * Pure mapping for the DeviceSpaceTexturedEffectShader rule; see ComposeRoundStrokeRect for the
 * sharing contract.
 */
std::optional<RuleComposedValues> ComposeDeviceSpaceTexturedEffect(
    const DeviceSpaceTexturedEffectInputs& inputs);

/**
 * Returns every (vertIndex, fragIndex) pair the RoundStrokeRect rule can ever produce, by
 * enumerating the full input lattice through ComposeRoundStrokeRect.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumerateRoundStrokeRectReachable();

/**
 * Returns every (vertIndex, fragIndex) pair the MaskFill rule can ever produce.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumerateMaskFillReachable();

/**
 * Returns every (vertIndex, fragIndex) pair the ConstColor rule can ever produce.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumerateConstColorReachable();

/**
 * Returns every (vertIndex, fragIndex) pair the HairlineLine rule can ever produce.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumerateHairlineLineReachable();

/**
 * Returns every (vertIndex, fragIndex) pair the HairlineQuad rule can ever produce.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumerateHairlineQuadReachable();

/**
 * Returns every (vertIndex, fragIndex) pair the QuadConstColor rule can ever produce.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumerateQuadConstColorReachable();

/**
 * Returns every (vertIndex, fragIndex) pair the QuadColorFill rule can ever produce.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumerateQuadColorFillReachable();

/**
 * Returns every (vertIndex, fragIndex) pair the SolidColorFill rule can ever produce.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumerateSolidColorFillReachable();

/**
 * Returns every (vertIndex, fragIndex) pair the ComplexEllipseFill rule can ever produce.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumerateComplexEllipseFillReachable();

/**
 * Returns every (vertIndex, fragIndex) pair the EllipseFill rule can ever produce.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumerateEllipseFillReachable();

/**
 * Returns every (vertIndex, fragIndex) pair the NonAARRectFill rule can ever produce.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumerateNonAARRectFillReachable();

/**
 * Returns every (vertIndex, fragIndex) pair the ComplexNonAARRectFill rule can ever produce.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumerateComplexNonAARRectFillReachable();

/**
 * Returns every (vertIndex, fragIndex) pair the MeshFill rule can ever produce.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumerateMeshFillReachable();

/**
 * Returns every (vertIndex, fragIndex) pair the PerlinNoiseFill rule can ever produce.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumeratePerlinNoiseFillReachable();

/**
 * Returns every (vertIndex, fragIndex) pair the TextureColorMatrix rule can ever produce.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumerateTextureColorMatrixReachable();

/**
 * Returns every (vertIndex, fragIndex) pair the YUVTextureFill rule can ever produce.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumerateYUVTextureFillReachable();

/**
 * Returns every (vertIndex, fragIndex) pair the DeviceSpaceTexture rule can ever produce.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumerateDeviceSpaceTextureReachable();

/**
 * Returns every (vertIndex, fragIndex) pair the DeviceSpaceTexturedEffect rule can ever produce.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumerateDeviceSpaceTexturedEffectReachable();

/**
 * Returns the reachable permutation set for a shader whose matcher rule has been migrated to the
 * Compose pattern, or nullopt when the rule has not been migrated yet.
 */
std::optional<std::set<std::pair<uint32_t, uint32_t>>> EnumerateReachablePermutations(
    const std::string& shaderName);

}  // namespace tgfx

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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <variant>
#include <vector>
#include "core/ColorSpaceXformSteps.h"
#include "gpu/SamplerState.h"
#include "gpu/TiledTextureSampling.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/shaders/KernelContract.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {

inline constexpr int MaxFusedAOTSamplers = 4;

class AOTEffectDecomposer;
class FragmentProcessor;

class AOTNodeID {
 public:
  constexpr AOTNodeID() = default;

  explicit constexpr AOTNodeID(uint32_t value) : value(value) {
  }

  static constexpr AOTNodeID Invalid() {
    return AOTNodeID();
  }

  constexpr bool isValid() const {
    return value != std::numeric_limits<uint32_t>::max();
  }

  constexpr uint32_t index() const {
    return value;
  }

  constexpr bool operator==(const AOTNodeID& other) const {
    return value == other.value;
  }

  constexpr bool operator!=(const AOTNodeID& other) const {
    return !(*this == other);
  }

 private:
  uint32_t value = std::numeric_limits<uint32_t>::max();
};

enum class AOTEffectKind {
  GeometryColor,
  TextureSource,
// The pointwise color operators: generated from the single op list (TGFX_POINTWISE_OPS in
// KernelContract.h), in the list's order, so the KindContracts rows below stay aligned.
#define TGFX_AOT_EFFECT_KIND(name, id, params, member, passBoundarySensitive) name,
  TGFX_POINTWISE_OPS(TGFX_AOT_EFFECT_KIND)
#undef TGFX_AOT_EFFECT_KIND
      PerlinNoiseSource,
  RectCoverage,
  GradientSource,
  RRectCoverage,
  // The unit input of a coverage subtree: the GP's output coverage (vCoverage broadcast, or
  // vec4(1.0) when the GP carries none), matching the runtime coverage-chain origin. Never
  // becomes a chain slot; as an op input it maps to the -3 designator.
  GeometryCoverage,
  // The geometry color with alpha forced to 1.0 — the input a two-child xfer feeds its children
  // in the runtime emission (vec4(inputColor.rgb, 1.0)). Never becomes a chain slot; as an op
  // input it maps to the -5 designator.
  GeometryColorOpaqueInput,
  // Opaque white — the input a single-child xfer feeds its child in the runtime emission (the
  // child emission omits the input color, which defaults to vec4(1.0)). Never becomes a chain
  // slot; as an op input it maps to the -4 designator.
  GeometryWhiteInput,
  // vec4(input.rgb, 1.0) over a COMPUTED input — the child input environment a two-child xfer
  // builds when its own input is a computed node C (the runtime feeds the children
  // vec4(inputColor.rgb, 1.0)). Unlike the GeometryColorOpaqueInput designator this is a real
  // chain slot (OP_INPUT_OPAQUE), because the source value is a runtime result register.
  InputOpaque,
  // in0 * in1.a — the two-child xfer epilogue's re-multiply by the xfer input's alpha, with a
  // computed input C supplying the alpha. A real chain slot (OP_MUL_ALPHA).
  MulAlpha,
  // An opaque-kernel operand (GaussianBlur1D, GlassRefraction, GlassUDFTentBlur): the processor's
  // math cannot be expressed in the pointwise chain, so the node marks the barrier. The planner
  // refuses such a graph (the dedicated family's kernel runs it on the plain route), and the
  // family's matching reads the node's facts from the IR instead of walking the processor tree.
  // The chain input rides along as the modulate operand, mirroring TextureSource; the
  // kernel's own sampled textures and geometry children stay inside the family's kernel.
  OpaqueKernel,
};

enum class AOTTextureSamplingKind {
  Plain,
  Tiled,
  Device,
};

using AOTTiledTextureRecipe = TiledTextureSampling;

struct AOTTextureParameters {
  std::shared_ptr<TextureProxy> textureProxy = nullptr;
  AOTTextureSamplingKind samplingKind = AOTTextureSamplingKind::Plain;
  SamplerState samplerState = {};
  SrcRectConstraint constraint = SrcRectConstraint::Fast;
  Matrix uvMatrix = {};
  std::optional<Rect> subset = std::nullopt;
  Point alphaStart = {};
  std::optional<AOTTiledTextureRecipe> tiledRecipe = std::nullopt;
  bool isYUV = false;
  // The multi-plane leaf's plane layout: 0 = I420 (three planes), 1 = NV12 (two planes), -1 =
  // not YUV or an unenumerated format. The YUV family's matching reads this off the IR instead
  // of walking the processor tree (its YUV_FORMAT variant dimension).
  int yuvFormat = -1;
  bool isAlphaOnly = false;
  bool hasRGBAAA = false;
  bool hasPerspective = false;
  // Marks a pass-boundary stand-in leaf: planning admits a cut's replacement before the
  // intermediate texture exists, so the leaf carries no proxy and only the fit-relevant facts
  // (device sampling, identity matrix, no perspective) — construction swaps in the real target.
  bool isIntermediate = false;
};

struct AOTColorMatrixParameters {
  std::array<float, 20> matrix = {};
};

// Color-space transform operand (ColorSpaceXformEffect). The steps object is shared with the JIT
// path so the fused kernel uploads byte-identical parameters through ColorSpaceXformHelper, which
// keeps the AOT and runtime results in agreement.
struct AOTColorSpaceXformParameters {
  std::shared_ptr<ColorSpaceXformSteps> steps = nullptr;
};

// Constant-color operand (ConstColorProcessor). color is premultiplied RGBA; inputMode mirrors
// tgfx::InputMode (0=Ignore, 1=ModulateRGBA, 2=ModulateA) and is a runtime uniform in the fused
// kernel rather than a structural axis.
struct AOTConstColorParameters {
  std::array<float, 4> color = {};
  int inputMode = 0;
};

// Binary blend (XfermodeFragmentProcessor). blendMode mirrors tgfx::BlendMode; childType mirrors
// XfermodeFragmentProcessor::Child (0=DstChild, 1=SrcChild, 2=TwoChild) and selects how the two
// inputs map to the blend's src/dst operands. Both are runtime uniforms in the fused kernel.
struct AOTBlendParameters {
  int blendMode = 0;
  int childType = 0;
  // Two-child blends only: the runtime re-multiplies the xfer output by its input color's alpha
  // (XfermodeFragmentProcessor's TwoChild epilogue). Accepted only at the color root, where the
  // input is the geometry color, so the chain kernel multiplies by TGFX_CHAIN_GEOM_COLOR.a.
  bool multiplyInputAlpha = false;
};

// Immutable upload bytes for graph-local lookup sharing, not a program-cache key.
struct AOTPerlinNoiseLookupData {
  std::array<uint8_t, 256> permutations = {};
  std::array<uint8_t, 4096> noise = {};
};

// Procedural-noise source (PerlinNoiseFragmentProcessor). Unlike AOTTextureParameters, the two
// texture views are internal lookup tables (a 256x1 permutation table and a 256x4 gradient table),
// not a user-sampled image, so this carries the noise math parameters directly instead of texture
// sampling semantics (tileMode/subset/uvMatrix are meaningless here beyond the coordinate matrix).
// noiseType mirrors tgfx::PerlinNoiseType (0=FractalNoise, 1=Turbulence) and is a runtime uniform
// in the fused kernel, matching PerlinNoiseFillShader.
struct AOTPerlinNoiseParameters {
  int noiseType = 0;
  int numOctaves = 1;
  bool stitchTiles = false;
  std::shared_ptr<TextureView> permutationsView = nullptr;
  std::shared_ptr<TextureView> noiseView = nullptr;
  float baseFrequencyX = 0.0f;
  float baseFrequencyY = 0.0f;
  int stitchWidth = 0;
  int stitchHeight = 0;
  Matrix uvMatrix = {};
  std::shared_ptr<const AOTPerlinNoiseLookupData> lookupData = nullptr;
};

bool AOTPerlinNoiseParametersSupported(const AOTPerlinNoiseParameters& parameters);

bool AOTPerlinNoiseLookupsEquivalent(const AOTPerlinNoiseParameters& first,
                                     const AOTPerlinNoiseParameters& second);

// Only compares noise sources within the same draw and coordinate-input environment.
bool AOTPerlinNoiseComputationsEquivalent(const AOTPerlinNoiseParameters& first,
                                          const AOTPerlinNoiseParameters& second);

// Analytic gradient source (ClampedGradientEffect): color computed from the fragment's
// transformed coordinate instead of a texture sample. The layout (linear/radial/conic/diamond)
// and the colorizer kind (single/dual/unrolled-binary) are runtime uniforms in the fused kernel,
// matching the dedicated gradient shaders; the texture (LUT) colorizer is not supported. At most
// one gradient source per chain (enforced by the chain builder). coordMatrix is the layout
// processor's local-to-gradient-space transform; the chain processor exposes it as a coord
// transform so the geometry processor writes the combined matrix through the standard path.
struct AOTGradientParameters {
  int layoutType = 0;  // 0=linear, 1=radial, 2=conic, 3=diamond
  float bias = 0.0f;   // conic only
  float scale = 1.0f;  // conic only
  std::array<float, 4> leftBorder = {};
  std::array<float, 4> rightBorder = {};
  int colorizerKind = 0;  // 0=single-interval, 1=dual-interval, 2=unrolled-binary, 3=LUT
  // LUT colorizer only: the baked gradient texture. It joins the chain as a non-slot child whose
  // sampler the OP_GRADIENT LUT branch reads through the GradientLUTLeaf uniform.
  std::shared_ptr<TextureProxy> lutProxy = nullptr;
  std::array<float, 4> start = {};    // single
  std::array<float, 4> end = {};      // single
  std::array<float, 4> scale01 = {};  // dual
  std::array<float, 4> bias01 = {};
  std::array<float, 4> scale23 = {};
  std::array<float, 4> bias23 = {};
  float threshold = 0.0f;  // dual
  int intervalCount = 0;   // unrolled: 1..8
  std::array<std::array<float, 4>, 8> scales = {};
  std::array<std::array<float, 4>, 8> biases = {};
  std::array<float, 4> thresholds1_7 = {};
  std::array<float, 4> thresholds9_13 = {};
  Matrix coordMatrix = {};
  bool hasPerspective = false;
};

/// The opaque kernels a draw can carry as one IR node: the dedicated families that the chain
/// interpreter cannot express. The value is not an ABI fact — the families are separate shaders —
/// so the enum exists to make a wrong or missing kernel a compile error instead of a silent
/// mismatch at match time.
enum class AOTOpaqueKernel : int {
  GaussianBlur1D = 0,
  GlassRefraction = 1,
  GlassUDFTentBlur = 2,
};

/// The geometry child a GlassRefraction draw carries: a compile-time variant dimension of that
/// family (the UDF kind always declares the edge-light mask sampler).
enum class AOTGlassGeometryKind : int {
  SdfRoundedRect = 0,
  SdfOther = 1,
  Udf = 2,
};

// The glass geometry child's record-time parameters (GlassSDFGeometryParams and
// GlassUDFGeometryParams in one record): the shared fields carry every kind, and the writer
// selects the SDF or UDF half by geometryKind. The UDF mask proxies ride here because the UDF
// bind-time factors (FineMaskUV, EdgeMaskUV) derive from the proxies' sizes at upload time.
struct AOTGlassShapeParameters {
  // Shared by both children (GlassGeometryParams).
  float halfW = 0.0f;
  float halfH = 0.0f;
  float refractionFactor = 0.0f;
  float splay = 0.0f;
  float depthRatio = 0.0f;
  float lightAngle = 0.0f;
  float edgeBandLayerPixels = 1.0f;
  // The SDF child's fields.
  float cornerRadius = 0.0f;
  float glassThickness = 0.0f;
  // The UDF child's fields.
  float udfPixelToLayerPixelX = 1.0f;
  float udfPixelToLayerPixelY = 1.0f;
  float edgeSpanX = 1.0f;
  float edgeSpanY = 1.0f;
  float textureOriginX = 0.0f;
  float textureOriginY = 0.0f;
  float edgeTextureOriginX = 0.0f;
  float edgeTextureOriginY = 0.0f;
  float edgePixelToLayerPixelX = 1.0f;
  float edgePixelToLayerPixelY = 1.0f;
  bool enableEdgeLighting = false;
  std::shared_ptr<TextureProxy> maskProxy = nullptr;
  std::shared_ptr<TextureProxy> edgeMaskProxy = nullptr;
};

// Opaque-kernel operand (see AOTEffectKind::OpaqueKernel). The shape-level facts the dedicated
// families' matching needs ride here — the matcher reads them from the IR, not the processor tree.
struct AOTOpaqueKernelParameters {
  AOTOpaqueKernel kernel = AOTOpaqueKernel::GaussianBlur1D;
  // GaussianBlur1D: the acceptance range is [1, 10] — the precompiled kernel's fixed cap.
  int sigma = 0;
  // GlassUDFTentBlur: the loop's fixed bound; a longer radius exceeds it.
  int maxRadius = 64;
  // GlassRefraction only; unset for the other kernels.
  std::optional<AOTGlassGeometryKind> geometryKind = std::nullopt;
  bool dispersionOn = false;
  // GlassRefraction: the kernel's own background-coordinate transform is projective — the
  // precompiled vertex kernels emit the affine form only.
  bool hasPerspective = false;
  // GlassRefraction: the background sample's raw coordinate transform. The texture normalization
  // is bind-time (it needs the instantiated view), so the writer derives the total from the
  // source proxy — the same value the tree walk's getTotalMatrix() reported.
  Matrix glassBackgroundMatrix = {};
  // GaussianBlur1D's upload values only: the packed half-kernel table, its
  // radius, and the step's length and direction — record-time facts. The step vector itself is
  // bind-time (it maps through the child's coordinate transform, whose normalization needs the
  // instantiated view), so the child leaf in the same graph supplies the raw matrix.
  int kernelRadius = 0;
  float stepLength = 0.0f;
  bool vertical = false;
  // 4 * KERNEL_VEC4_COUNT, with the constant as spelled in Blur1DFragmentProcessor.h
  // (MAX_KERNEL_RADIUS 64 → MAX_KERNEL_SIZE 65 → 17 vec4s).
  std::array<float, 68> packedKernel = {};
  // GlassUDFTentBlur's record-time upload values: the per-field radii, the field selector, the
  // packed-input flag and the step's direction. The step vector itself is bind-time (it maps
  // through the child's coordinate transform, whose normalization needs the instantiated view),
  // so the writer derives it from the child leaf as the Gaussian blur's step is derived.
  // GlassRefraction's record-time upload values (GlassRefractionParams). The optics block's
  // source-relative factors (1/sourceWidth, 1/sourceHeight) stay bind-time, so the writer derives
  // them from the background leaf's view; everything here is a draw-time constant.
  float glassDispersion = 0.0f;
  float glassLightIntensity = 0.0f;
  float glassOrigWidth = 0.0f;
  float glassOrigHeight = 0.0f;
  float glassMaxDisplacement = 0.0f;
  float glassUVScaleX = 0.0f;
  float glassUVScaleY = 0.0f;
  float glassUVOffsetX = 0.0f;
  float glassUVOffsetY = 0.0f;
  float glassLayerToSourceX = 0.0f;
  float glassLayerToSourceY = 0.0f;
  float glassFrost = 0.0f;
  int glassShapeType = 0;
  // The geometry child's record (see AOTGlassShapeParameters); geometryKind selects which half the
  // writer reads.
  AOTGlassShapeParameters glassShape = {};
  // The background source proxy: the optics block's source-relative factors (1/sourceWidth,
  // 1/sourceHeight and the glassUVScale fallbacks) derive from its size at bind time.
  std::shared_ptr<TextureProxy> glassSourceProxy = nullptr;
  float tentFineRadius = 0.0f;
  float tentCoarseRadius = 0.0f;
  int tentField = 0;
  int tentInputIsPacked = 0;
  bool tentVertical = false;
};

// Analytic anti-aliased rect coverage (RectEffect, produced by AA rect clips). Multiplies the
// input color by a per-fragment coverage computed from gl_FragCoord and the rect, so it is a
// pointwise node that reads the destination device coordinate directly and needs no texture or
// varying. rect is {left, top, right, bottom} in device coordinates, already origin-flipped by the
// clip code; the 0.5 outset for the AA falloff is applied at uniform upload time. deviceToLocal
// maps device coordinates into the rect's local space in Matrix::get9 row-major order; identity
// means the rect already lives in device space (the shader then evaluates the separable math
// directly on gl_FragCoord).
struct AOTRectCoverageParameters {
  std::array<float, 4> rect = {};
  std::array<float, 9> deviceToLocal = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
};

// Analytic rounded-rect coverage (RRectEffect). Every parameter rides uniforms — local or device
// space via deviceToLocal (identity for the device-space form, Matrix::get9 row-major order), AA
// or hard edge via antiAlias — so a single node kind covers all RRectEffect forms. rect is
// {left, top, right, bottom} in the local space; radiiX/radiiY pack the per-corner radii in
// [TL, TR, BR, BL] order.
struct AOTRRectCoverageParameters {
  std::array<float, 4> rect = {};
  std::array<float, 4> radiiX = {};
  std::array<float, 4> radiiY = {};
  std::array<float, 9> deviceToLocal = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  float antiAlias = 1.0f;
};

using AOTEffectParameters = std::variant<std::monostate, AOTTextureParameters,
#define TGFX_AOT_PARAM_TYPE(name, id, params, member, passBoundarySensitive) params,
                                         TGFX_POINTWISE_OPS(TGFX_AOT_PARAM_TYPE)
#undef TGFX_AOT_PARAM_TYPE
                                             AOTPerlinNoiseParameters,
                                         AOTRectCoverageParameters, AOTGradientParameters,
                                         AOTRRectCoverageParameters, AOTOpaqueKernelParameters>;

/// Whether the operator amplifies the 1/255 pass-boundary rounding into a visible flip (a step
/// function) and must therefore share its pass with its input's producer. The single declaration
/// both the decomposer's cut placement (CanStartPassAt) and the planner's validation read — the
/// flag lives on the op list's row (TGFX_POINTWISE_OPS in KernelContract.h).
constexpr bool IsPassBoundarySensitive(AOTEffectKind kind) {
#define TGFX_AOT_PASS_SENSITIVE(name, id, params, member, passBoundarySensitive) \
  if (kind == AOTEffectKind::name) {                                             \
    return passBoundarySensitive;                                                \
  }
  TGFX_POINTWISE_OPS(TGFX_AOT_PASS_SENSITIVE)
#undef TGFX_AOT_PASS_SENSITIVE
  return false;
}

/**
 * Splits a 20-element color matrix into the two uniform records every pointwise-slot layout uses:
 * the 4x4 linear part (elements 0-3, 5-8, 10-13, 15-18) and the 4-element translate vector
 * (elements 4, 9, 14, 19). Shared by the plain-slot and chain-slot uploaders so the packing order
 * is defined once.
 */
inline void PackAOTColorMatrix(const std::array<float, 20>& matrix, float* matrixData,
                               float* vectorData) {
  const float packed[] = {matrix[0],  matrix[5],  matrix[10], matrix[15], matrix[1],  matrix[6],
                          matrix[11], matrix[16], matrix[2],  matrix[7],  matrix[12], matrix[17],
                          matrix[3],  matrix[8],  matrix[13], matrix[18]};
  const float vector[] = {matrix[4], matrix[9], matrix[14], matrix[19]};
  std::copy(packed, packed + 16, matrixData);
  std::copy(vector, vector + 4, vectorData);
}

struct AOTEffectNode {
  AOTEffectKind kind = AOTEffectKind::GeometryColor;
  std::vector<AOTNodeID> inputs = {};
  AOTEffectParameters parameters = {};
};

class AOTEffectGraph {
 public:
  size_t nodeCount() const {
    return nodes.size();
  }

  const AOTEffectNode* nodeAt(AOTNodeID nodeID) const;

  AOTNodeID root() const {
    return rootNode;
  }

 private:
  friend class AOTNodeBuilder;

  std::vector<AOTEffectNode> nodes = {};
  AOTNodeID rootNode = AOTNodeID::Invalid();
};

/**
 * Returns true when the given ConstColor node evaluates to its own constant regardless of the value
 * on its input edge: it either ignores its input (InputMode::Ignore), or it is fed the opaque-white
 * designator a single-child xfer gives its child, under which every input mode reduces to the
 * constant (c * 1 and c * 1.a are both c). Kernels whose blend slot reads the constant directly
 * (the OP_BLEND ConstColorValue record) may only accept const operands for which this holds.
 */
bool AOTConstColorIsInputIndependent(const AOTEffectGraph& graph, const AOTEffectNode* node);

class AOTNodeBuilder {
 public:
  bool addGeometryColor(AOTNodeID* output);

  bool addGeometryColorOpaqueInput(AOTNodeID* output);

  // Adds an opaque-white input designator node (the single-child xfer child input). Like the
  // opaque geometry input, it never becomes a chain slot; consumers map it to the -4 designator.
  bool addGeometryWhiteInput(AOTNodeID* output);

  bool addTextureSource(AOTNodeID input, const AOTTextureParameters& parameters, AOTNodeID* output);

  bool addColorMatrix(AOTNodeID input, const AOTColorMatrixParameters& parameters,
                      AOTNodeID* output);

  bool addLuma(AOTNodeID input, const ChainISA::LumaData& parameters, AOTNodeID* output);

  bool addAlphaThreshold(AOTNodeID input, const ChainISA::AlphaThresholdData& parameters,
                         AOTNodeID* output);

  bool addColorSpaceXform(AOTNodeID input, const AOTColorSpaceXformParameters& parameters,
                          AOTNodeID* output);

  bool addConstColor(AOTNodeID input, const AOTConstColorParameters& parameters, AOTNodeID* output);

  // Adds a binary blend node consuming two previously-built operands (src and dst node ids).
  bool addBlend(AOTNodeID src, AOTNodeID dst, const AOTBlendParameters& parameters,
                AOTNodeID* output);

  bool addPerlinNoiseSource(AOTNodeID input, const AOTPerlinNoiseParameters& parameters,
                            AOTNodeID* output);

  bool addRectCoverage(AOTNodeID input, const AOTRectCoverageParameters& parameters,
                       AOTNodeID* output);

  bool addRRectCoverage(AOTNodeID input, const AOTRRectCoverageParameters& parameters,
                        AOTNodeID* output);

  bool addGradientSource(AOTNodeID input, const AOTGradientParameters& parameters,
                         AOTNodeID* output);

  bool addGeometryCoverage(AOTNodeID* output);

  bool addInputOpaque(AOTNodeID input, AOTNodeID* output);
  bool addMulAlpha(AOTNodeID value, AOTNodeID alphaSource, AOTNodeID* output);
  bool addOpaqueKernel(AOTNodeID input, const AOTOpaqueKernelParameters& parameters,
                       AOTNodeID* output);

  /**
   * Appends a copy of an already-admitted node: same kind and input count, with the given inputs
   * and parameter block. The source node passed its kind-specific admission when it was first
   * added, so none runs again — the caller is responsible for remapping coordinate-carrying
   * parameters before handing them over. GeometryColor and GeometryCoverage (graph roots) cannot
   * be copied. The SSA ordering rule (every input precedes its consumer) is enforced by finish().
   */
  bool addCopy(const AOTEffectNode& node, const std::vector<AOTNodeID>& inputs,
               AOTEffectParameters parameters, AOTNodeID* output);

  bool finish(AOTNodeID root, AOTEffectGraph* graph) const;

  size_t nodeCount() const {
    return nodes.size();
  }

  bool isGeometryColorOpaqueInput(AOTNodeID nodeID) const {
    return contains(nodeID) &&
           nodes[nodeID.index()].kind == AOTEffectKind::GeometryColorOpaqueInput;
  }

  bool isGeometryWhiteInput(AOTNodeID nodeID) const {
    return contains(nodeID) && nodes[nodeID.index()].kind == AOTEffectKind::GeometryWhiteInput;
  }

  /// The first processor that failed to lower, for the caller's diagnostic. Null when the build
  /// has not seen a failure.
  const FragmentProcessor* missingLoweringProcessor() const {
    return missingLowering;
  }

 private:
  friend class FragmentProcessor;

  void recordMissingLowering(const FragmentProcessor* processor) {
    if (missingLowering == nullptr) {
      missingLowering = processor;
    }
  }

  bool addUnaryNode(AOTEffectKind kind, AOTNodeID input, AOTEffectParameters parameters,
                    AOTNodeID* output);

  bool addBinaryNode(AOTEffectKind kind, AOTNodeID first, AOTNodeID second,
                     AOTEffectParameters parameters, AOTNodeID* output);

  bool contains(AOTNodeID nodeID) const;

  std::vector<AOTEffectNode> nodes = {};
  const FragmentProcessor* missingLowering = nullptr;
};

}  // namespace tgfx

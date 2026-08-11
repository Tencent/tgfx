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
  ColorMatrix,
  Luma,
  AlphaThreshold,
  ColorSpaceXform,
  ConstColor,
  Blend,
  PerlinNoiseSource,
  RectCoverage,
  GradientSource,
  // The unit input of a coverage subtree: the GP's output coverage (vCoverage broadcast, or
  // vec4(1.0) when the GP carries none), matching the runtime coverage-chain origin. Never
  // becomes a chain slot; as an op input it maps to the -3 designator.
  GeometryCoverage,
};

enum class EffectDomain {
  Pointwise,
  Neighborhood,
  Composite,
  External,
};

enum class EffectInputUsage {
  Ignore,
  ColorRGBA,
  ColorAlpha,
  SameCoordinateChild,
  RepeatedCoordinateChild,
};

struct EffectTraits {
  EffectDomain domain = EffectDomain::Pointwise;
  EffectInputUsage inputUsage = EffectInputUsage::Ignore;
  bool isSelfContained = false;
  bool preservesAlphaRepresentation = false;
  bool preservesColorSpace = false;
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
  bool isAlphaOnly = false;
  bool hasRGBAAA = false;
  bool hasSubset = false;
  bool hasPerspective = false;
};

struct AOTColorMatrixParameters {
  std::array<float, 20> matrix = {};
};

struct AOTLumaParameters {
  float kr = 0.2126f;
  float kg = 0.7152f;
  float kb = 0.0722f;
};

// Alpha-step operand (AlphaThresholdFragmentProcessor). threshold is compared against the input
// alpha; it is a runtime uniform in the fused kernel rather than a structural axis.
struct AOTAlphaThresholdParameters {
  float threshold = 0.0f;
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
};

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
  int colorizerKind = 0;              // 0=single-interval, 1=dual-interval, 2=unrolled-binary
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

// Analytic anti-aliased rect coverage (AARectEffect, produced by AA rect clips). Multiplies the
// input color by a per-fragment coverage computed from gl_FragCoord and the rect, so it is a
// pointwise node that reads the destination device coordinate directly and needs no texture or
// varying. rect is {left, top, right, bottom} in device coordinates, already origin-flipped by the
// clip code; the 0.5 outset for the AA falloff is applied at uniform upload time.
struct AOTRectCoverageParameters {
  std::array<float, 4> rect = {};
};

using AOTEffectParameters =
    std::variant<std::monostate, AOTTextureParameters, AOTColorMatrixParameters, AOTLumaParameters,
                 AOTAlphaThresholdParameters, AOTColorSpaceXformParameters, AOTConstColorParameters,
                 AOTBlendParameters, AOTPerlinNoiseParameters, AOTRectCoverageParameters,
                 AOTGradientParameters>;

// Runtime-selected pointwise operator applied by the fused kernels (PointwiseTail, PointwiseChain
// and PerlinNoiseFill). The values mirror the OP_* constants in pointwise_op.inc: the kernels
// declare every operator's parameter block unconditionally and select at draw time via a uniform,
// so the operator kind is never a compile-time permutation axis.
enum class AOTPointwiseOpType : int {
  ColorMatrix = 0,
  Luma = 1,
  AlphaThreshold = 2,
  ColorSpaceXform = 3,
  None = 4,
};

// One pointwise-operator slot of a fused kernel, carrying the full parameter set of whichever
// operator it holds. Only the member matching type is read.
struct AOTPointwiseSlot {
  AOTPointwiseOpType type = AOTPointwiseOpType::None;
  AOTColorMatrixParameters colorMatrix = {};
  AOTLumaParameters luma = {};
  AOTAlphaThresholdParameters alphaThreshold = {};
  AOTColorSpaceXformParameters colorSpaceXform = {};
};

struct AOTEffectNode {
  AOTEffectKind kind = AOTEffectKind::GeometryColor;
  std::vector<AOTNodeID> inputs = {};
  EffectTraits traits = {};
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

class AOTNodeBuilder {
 public:
  bool addGeometryColor(AOTNodeID* output);

  bool addTextureSource(AOTNodeID input, const AOTTextureParameters& parameters, AOTNodeID* output);

  bool addColorMatrix(AOTNodeID input, const AOTColorMatrixParameters& parameters,
                      AOTNodeID* output);

  bool addLuma(AOTNodeID input, const AOTLumaParameters& parameters, AOTNodeID* output);

  bool addAlphaThreshold(AOTNodeID input, const AOTAlphaThresholdParameters& parameters,
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

  bool addGradientSource(AOTNodeID input, const AOTGradientParameters& parameters,
                         AOTNodeID* output);

  bool addGeometryCoverage(AOTNodeID* output);

  bool finish(AOTNodeID root, AOTEffectGraph* graph) const;

  size_t nodeCount() const {
    return nodes.size();
  }

 private:
  friend class AOTEffectDecomposer;
  friend class FragmentProcessor;

  const FragmentProcessor* missingLoweringProcessor() const {
    return missingLowering;
  }

  void recordMissingLowering(const FragmentProcessor* processor) {
    if (missingLowering == nullptr) {
      missingLowering = processor;
    }
  }

  bool addUnaryNode(AOTEffectKind kind, AOTNodeID input, EffectTraits traits,
                    AOTEffectParameters parameters, AOTNodeID* output);

  bool addBinaryNode(AOTEffectKind kind, AOTNodeID first, AOTNodeID second, EffectTraits traits,
                     AOTEffectParameters parameters, AOTNodeID* output);

  bool contains(AOTNodeID nodeID) const;

  std::vector<AOTEffectNode> nodes = {};
  const FragmentProcessor* missingLowering = nullptr;
};

}  // namespace tgfx

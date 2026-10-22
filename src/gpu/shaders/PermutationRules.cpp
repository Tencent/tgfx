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

#include "gpu/shaders/PermutationRules.h"
#include "gpu/shaders/level1/AtlasTextFillShader.h"
#include "gpu/shaders/level1/ComplexEllipseFillShader.h"
#include "gpu/shaders/level1/ComplexNonAARRectFillShader.h"
#include "gpu/shaders/level1/EllipseFillShader.h"
#include "gpu/shaders/level1/GaussianBlur1DShader.h"
#include "gpu/shaders/level1/GlassRefractionShader.h"
#include "gpu/shaders/level1/GlassUDFTentBlurShader.h"
#include "gpu/shaders/level1/HairlineLineShader.h"
#include "gpu/shaders/level1/HairlineQuadShader.h"
#include "gpu/shaders/level1/MaskFillShader.h"
#include "gpu/shaders/level1/MeshFillShader.h"
#include "gpu/shaders/level1/NonAARRectFillShader.h"
#include "gpu/shaders/level1/PerlinNoiseFillShader.h"
#include "gpu/shaders/level1/PointwiseChainShader.h"
#include "gpu/shaders/level1/QuadColorFillShader.h"
#include "gpu/shaders/level1/QuadTextureFillShader.h"
#include "gpu/shaders/level1/RoundStrokeRectFillShader.h"
#include "gpu/shaders/level1/ShapeInstancedFillShader.h"
#include "gpu/shaders/level1/ShapeInstancedTextureCoverageShader.h"
#include "gpu/shaders/level1/SolidColorFillShader.h"
#include "gpu/shaders/level1/TexturedEffectShader.h"
#include "gpu/shaders/level1/TiledTextureFillShader.h"
#include "gpu/shaders/level1/YUVTextureFillShader.h"

namespace tgfx {

namespace {

// Enumerates every (vert, frag) variant pair of a family whose mirrored dimensions agree, then
// applies the family's frag-side restriction (an empty fragAccept accepts everything). For the
// pass-through families Compose maps every input-axis value onto the domains one-to-one, so the
// reachable set is exactly the mirrored domain product minus the restriction. Chain/Graph and YUV
// keep their dedicated enumerators.
// Vertex-stage placeholder for families with no vertex dimensions: a single trivial variant.
struct EmptyVertDims {
  static PermutationDomain domain() {
    return PermutationDomain({});
  }
};

template <typename D, typename FD>
std::set<std::pair<uint32_t, uint32_t>> EnumerateMirroredReachable(
    const std::function<bool(const std::vector<int>&)>& fragAccept) {
  std::set<std::pair<uint32_t, uint32_t>> result;
  const auto vertDomain = D::domain();
  const auto fragDomain = FD::domain();
  for (uint32_t vertIndex = 0; vertIndex < vertDomain.totalCount(); ++vertIndex) {
    const auto vertValues = vertDomain.decode(vertIndex);
    for (uint32_t fragIndex = 0; fragIndex < fragDomain.totalCount(); ++fragIndex) {
      const auto fragValues = fragDomain.decode(fragIndex);
      if (!MirroredDimsAgree(vertDomain, fragDomain, vertValues, fragValues)) {
        continue;
      }
      if (fragAccept && !fragAccept(fragValues)) {
        continue;
      }
      result.insert({vertIndex, fragIndex});
    }
  }
  return result;
}

// Shared rejection predicates: Compose and the family's enumerator call the same function, so the
// runtime rule and the compiled variant set cannot drift apart. Each predicate takes the family's
// frag domain values; Compose validates its composed values, the enumerator validates each
// mirrored pair it is about to keep.

// NonAARRectFillShader: the textured variant (a TiledTextureEffect fill) is fill-only; a stroked
// rect never carries the tiled texture child.
bool NonAARRectFillValuesAllowed(const std::vector<int>& fragValues) {
  return !(fragValues[NonAARRectFillShader::FD::TEXTURED] != 0 &&
           fragValues[NonAARRectFillShader::FD::STROKE] != 0);
}

// The extended resource kinds (RESOURCE_KIND != 0): the per-quad subset of a strict image draw is
// a plain-chain capability; the extended kernels have no subset branch. Takes the shared chain
// frag values.
bool PointwiseExtendedChainValuesAllowed(const std::vector<int>& chainFragValues) {
  return chainFragValues[PointwiseChainShader::FD::HAS_SUBSET] == 0;
}

}  // namespace

std::optional<RuleComposedValues> ComposeRoundStrokeRect(const RoundStrokeRectInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using D = RoundStrokeRectFillShader::Dims;
  using FD = RoundStrokeRectFillShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(D::COUNT);
  values.vertValues[D::HAS_UV_MATRIX] = inputs.hasUVMatrix ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateRoundStrokeRectReachable() {
  return EnumerateMirroredReachable<RoundStrokeRectFillShader::Dims, RoundStrokeRectFillShader::FD>(
      nullptr);
}

std::optional<RuleComposedValues> ComposeMaskFill(const MaskFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using D = MaskFillShader::D;
  RuleComposedValues values;
  values.fragValues.resize(D::COUNT);
  values.fragValues[D::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateMaskFillReachable() {
  return EnumerateMirroredReachable<EmptyVertDims, MaskFillShader::FragDims>(nullptr);
}

std::optional<RuleComposedValues> ComposeHairlineLine(const HairlineLineInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using FD = HairlineLineShader::FD;
  RuleComposedValues values;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateHairlineLineReachable() {
  return EnumerateMirroredReachable<EmptyVertDims, HairlineLineShader::FD>(nullptr);
}

std::optional<RuleComposedValues> ComposeHairlineQuad(const HairlineQuadInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using FD = HairlineQuadShader::FD;
  RuleComposedValues values;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateHairlineQuadReachable() {
  return EnumerateMirroredReachable<EmptyVertDims, HairlineQuadShader::FD>(nullptr);
}

std::optional<RuleComposedValues> ComposeQuadColorFill(const QuadColorFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using FD = QuadColorFillShader::FD;
  RuleComposedValues values;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  values.fragValues[FD::HAS_MASK_TEXTURE] = inputs.hasMaskTexture ? 1 : 0;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateQuadColorFillReachable() {
  return EnumerateMirroredReachable<EmptyVertDims, QuadColorFillShader::FD>(nullptr);
}

std::optional<RuleComposedValues> ComposeSolidColorFill(const SolidColorFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using FD = SolidColorFillShader::FD;
  RuleComposedValues values;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateSolidColorFillReachable() {
  return EnumerateMirroredReachable<SolidColorFillShader::VD, SolidColorFillShader::FD>(nullptr);
}

std::optional<RuleComposedValues> ComposeComplexEllipseFill(
    const ComplexEllipseFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using D = ComplexEllipseFillShader::Dims;
  using FD = ComplexEllipseFillShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(D::COUNT);
  values.vertValues[D::STROKE] = inputs.isStroke ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::STROKE] = values.vertValues[D::STROKE];
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateComplexEllipseFillReachable() {
  return EnumerateMirroredReachable<ComplexEllipseFillShader::Dims, ComplexEllipseFillShader::FD>(
      nullptr);
}

std::optional<RuleComposedValues> ComposeEllipseFill(const EllipseFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using FD = EllipseFillShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(EllipseFillShader::D::COUNT);
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  values.fragValues[FD::HAS_DEVICE_MASK] = inputs.deviceMask;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateEllipseFillReachable() {
  return EnumerateMirroredReachable<EllipseFillShader::D, EllipseFillShader::FD>(nullptr);
}

std::optional<RuleComposedValues> ComposeNonAARRectFill(const NonAARRectFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using D = NonAARRectFillShader::Dims;
  using FD = NonAARRectFillShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(D::COUNT);
  values.vertValues[D::STROKE] = inputs.isStroke ? 1 : 0;
  values.vertValues[D::TEXTURED] = inputs.textured ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::STROKE] = values.vertValues[D::STROKE];
  values.fragValues[FD::TEXTURED] = values.vertValues[D::TEXTURED];
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  if (!NonAARRectFillValuesAllowed(values.fragValues)) {
    return std::nullopt;
  }
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateNonAARRectFillReachable() {
  return EnumerateMirroredReachable<NonAARRectFillShader::Dims, NonAARRectFillShader::FD>(
      &NonAARRectFillValuesAllowed);
}

std::optional<RuleComposedValues> ComposeComplexNonAARRectFill(
    const ComplexNonAARRectFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using D = ComplexNonAARRectFillShader::Dims;
  using FD = ComplexNonAARRectFillShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(D::COUNT);
  values.vertValues[D::STROKE] = inputs.isStroke ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::STROKE] = values.vertValues[D::STROKE];
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateComplexNonAARRectFillReachable() {
  return EnumerateMirroredReachable<ComplexNonAARRectFillShader::Dims,
                                    ComplexNonAARRectFillShader::FD>(nullptr);
}

std::optional<RuleComposedValues> ComposeMeshFill(const MeshFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using D = MeshFillShader::Dims;
  using FD = MeshFillShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(D::COUNT);
  values.vertValues[D::HAS_TEX_COORDS] = inputs.hasTexCoords ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateMeshFillReachable() {
  return EnumerateMirroredReachable<MeshFillShader::Dims, MeshFillShader::FD>(nullptr);
}

std::optional<RuleComposedValues> ComposePerlinNoiseFill(const PerlinNoiseFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using D = PerlinNoiseFillShader::D;
  RuleComposedValues values;
  values.fragValues.resize(D::COUNT);
  values.fragValues[D::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumeratePerlinNoiseFillReachable() {
  return EnumerateMirroredReachable<PerlinNoiseFillShader::VD, PerlinNoiseFillShader::FragDims>(
      nullptr);
}

std::optional<RuleComposedValues> ComposeYUVTextureFill(const YUVTextureFillInputs& inputs) {
  if (inputs.xpType < 0 || inputs.yuvFormat < 0) {
    return std::nullopt;
  }
  using VD = YUVTextureFillShader::VD;
  using FD = YUVTextureFillShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(VD::COUNT);
  values.vertValues[VD::HAS_UV_COORD] = inputs.hasUVMatrix ? 0 : 1;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::YUV_FORMAT] = inputs.yuvFormat;
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateYUVTextureFillReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int hasUVMatrix = 0; hasUVMatrix <= 1; ++hasUVMatrix) {
    for (int yuvFormat = -1; yuvFormat <= 1; ++yuvFormat) {
      for (int xpType = -1; xpType <= 2; ++xpType) {
        YUVTextureFillInputs inputs;
        inputs.hasUVMatrix = hasUVMatrix != 0;
        inputs.yuvFormat = yuvFormat;
        inputs.xpType = xpType;
        auto composed = ComposeYUVTextureFill(inputs);
        if (!composed) {
          continue;
        }
        auto vertIndex = YUVTextureFillShader::VD::domain().encode(composed->vertValues);
        auto fragIndex = YUVTextureFillShader::FD::domain().encode(composed->fragValues);
        result.insert({vertIndex, fragIndex});
      }
    }
  }
  return result;
}

std::optional<RuleComposedValues> ComposeShapeInstancedTextureCoverage(
    const ShapeInstancedTextureCoverageInputs& inputs) {
  using D = ShapeInstancedTextureCoverageShader::D;
  using FD = ShapeInstancedTextureCoverageShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(D::COUNT);
  values.vertValues[D::GRADIENT] = inputs.gradient ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::GRADIENT] = values.vertValues[D::GRADIENT];
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateShapeInstancedTextureCoverageReachable() {
  return EnumerateMirroredReachable<ShapeInstancedTextureCoverageShader::Dims,
                                    ShapeInstancedTextureCoverageShader::FD>(nullptr);
}

std::optional<RuleComposedValues> ComposeShapeInstancedFill(
    const ShapeInstancedFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using FD = ShapeInstancedFillShader::FD;
  RuleComposedValues values;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateShapeInstancedFillReachable() {
  return EnumerateMirroredReachable<ShapeInstancedFillShader::Dims, ShapeInstancedFillShader::FD>(
      nullptr);
}

std::optional<RuleComposedValues> ComposeTiledTextureFill(const TiledTextureFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using FD = TiledTextureFillShader::FragDims;
  RuleComposedValues values;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateTiledTextureFillReachable() {
  return EnumerateMirroredReachable<TiledTextureFillShader::VD, TiledTextureFillShader::FD>(
      nullptr);
}

std::optional<RuleComposedValues> ComposeQuadTextureFill(const QuadTextureFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using VD = QuadTextureFillShader::VD;
  using FD = QuadTextureFillShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(VD::COUNT);
  values.vertValues[VD::HAS_UV_COORD] = inputs.hasUVCoord ? 1 : 0;
  values.vertValues[VD::HAS_SUBSET] = inputs.hasSubset ? 1 : 0;
  values.vertValues[VD::HAS_LOCAL_MASK] = inputs.hasLocalMask ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  // ALPHA_ONLY and HAS_RGBAAA are runtime uniforms (set by GLSLTextureEffect::onSetData), not
  // variants.
  values.fragValues[FD::HAS_SUBSET] = values.vertValues[VD::HAS_SUBSET];
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  values.fragValues[FD::HAS_LOCAL_MASK] = values.vertValues[VD::HAS_LOCAL_MASK];
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateQuadTextureFillReachable() {
  return EnumerateMirroredReachable<QuadTextureFillShader::VD, QuadTextureFillShader::FD>(nullptr);
}

std::optional<RuleComposedValues> ComposeGaussianBlur1D(const GaussianBlur1DInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using FD = GaussianBlur1DShader::FD;
  RuleComposedValues values;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateGaussianBlur1DReachable() {
  return EnumerateMirroredReachable<EmptyVertDims, GaussianBlur1DShader::FD>(nullptr);
}

std::optional<RuleComposedValues> ComposeGlassUDFTentBlur(const GlassUDFTentBlurInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using FD = GlassUDFTentBlurShader::FD;
  RuleComposedValues values;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateGlassUDFTentBlurReachable() {
  return EnumerateMirroredReachable<EmptyVertDims, GlassUDFTentBlurShader::FD>(nullptr);
}

std::optional<RuleComposedValues> ComposeGlassRefraction(const GlassRefractionInputs& inputs) {
  if (inputs.gpKind < 0 || inputs.geometryKind < 0 || inputs.xpType < 0) {
    return std::nullopt;
  }
  using FD = GlassRefractionShader::FD;
  using VD = GlassRefractionShader::VD;
  RuleComposedValues values;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::GP_KIND] = inputs.gpKind;
  values.fragValues[FD::GEOMETRY_KIND] = inputs.geometryKind;
  values.fragValues[FD::DISPERSION_ON] = inputs.dispersionOn ? 1 : 0;
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  values.vertValues.resize(VD::COUNT);
  values.vertValues[VD::GP_KIND] = inputs.gpKind;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateGlassRefractionReachable() {
  return EnumerateMirroredReachable<GlassRefractionShader::VD, GlassRefractionShader::FD>(nullptr);
}

std::optional<RuleComposedValues> ComposeTexturedEffect(const TexturedEffectInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using D = TexturedEffectShader::D;
  RuleComposedValues values;
  values.fragValues.resize(D::COUNT);
  values.fragValues[D::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateTexturedEffectReachable() {
  return EnumerateMirroredReachable<TexturedEffectShader::VD, TexturedEffectShader::FragDims>(
      nullptr);
}

std::optional<RuleComposedValues> ComposeAtlasTextFill(const AtlasTextFillInputs& inputs) {
  if (inputs.xpType < 0) {
    return std::nullopt;
  }
  using D = AtlasTextFillShader::D;
  using FD = AtlasTextFillShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(D::COUNT);
  values.vertValues[D::HAS_COVERAGE] = inputs.hasCoverage ? 1 : 0;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::HAS_COVERAGE] = values.vertValues[D::HAS_COVERAGE];
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumerateAtlasTextFillReachable() {
  return EnumerateMirroredReachable<AtlasTextFillShader::Dims, AtlasTextFillShader::FD>(nullptr);
}

static std::optional<RuleComposedValues> ComposePointwiseGP(const PointwiseChainInputs& inputs) {
  if (inputs.xpType < 0 || inputs.xpType > 2 || inputs.textureCount < 0 ||
      inputs.textureCount > 1) {
    return std::nullopt;
  }
  // Each GP profile fixes the attribute slots its vertex buffers carry. The color attribute is
  // unconditional on every layout (a white filler where the buffer is shared across draws), so
  // only the other slots shape dimensions. Quad and Mesh buffers always carry the coverage slot
  // (providers emit 1.0 for non-AA draws); atlas text buffers ride maskCoord on the uvCoord slot
  // with no coverage slot; the ellipse layout evaluates coverage per pixel and carries no
  // coverage or uvCoord attributes; DefaultGP carries none of the quad attributes and takes
  // coverage from its AA type.
  int gpLayout = 0;
  int hasCoverage = 0;
  int hasUVCoord = 0;
  switch (inputs.gpKind) {
    case 0:
      if (inputs.hasUVCoord) {
        return std::nullopt;
      }
      // The DefaultGP's coverage attribute is unconditional (the non-AA path triangles carry a
      // constant 1.0 slot), so the matcher always reports coverage: reject the vacuous lattice
      // cell so the enumeration equals the producible set.
      if (!inputs.hasCoverage) {
        return std::nullopt;
      }
      hasCoverage = 1;
      break;
    case 1:
    case 2:
      if (!inputs.hasCoverage) {
        return std::nullopt;
      }
      hasCoverage = 1;
      hasUVCoord = inputs.hasUVCoord ? 1 : 0;
      break;
    case 3:
      if (inputs.hasCoverage || !inputs.hasUVCoord) {
        return std::nullopt;
      }
      // The atlas chain route exists to sample the atlas coverage leaf: the decomposer always
      // attaches it, so a leaf-free atlas chain is structurally producible but semantically
      // vacuous. Rejecting it here keeps the reachable set equal to the compiled set (the gap was
      // previously masked by the IsBuildablePermutation post-check falling back to JIT).
      if (inputs.textureCount == 0) {
        return std::nullopt;
      }
      hasUVCoord = 1;
      break;
    case 4:
      if (inputs.hasCoverage || inputs.hasUVCoord) {
        return std::nullopt;
      }
      gpLayout = 1;
      break;
    case 5:
      // HairlineLineGP buffers are (position, edgeDistance, color): the color slot is a white
      // filler (the exact color rides the Color uniform) and the edge AA is evaluated per pixel
      // from the edge-distance varying, so no coverage or uvCoord attribute exists.
      if (inputs.hasCoverage || inputs.hasUVCoord) {
        return std::nullopt;
      }
      gpLayout = 2;
      break;
    case 6:
    case 7:
      // ComplexEllipseGP buffers are (position, color, ellipseOffset, ellipseRadii, edgeDist,
      // [strokeWidth]): device-space like the ellipse, with per-pixel coverage and no coverage or
      // uvCoord attribute. The stroke form (7) adds the strokeWidth attribute, hence its own
      // layout.
      if (inputs.hasCoverage || inputs.hasUVCoord) {
        return std::nullopt;
      }
      gpLayout = inputs.gpKind == 6 ? 3 : 4;
      break;
    default:
      return std::nullopt;
  }
  // Coverage subtrees draw their unit input from the GP coverage: the rect and mesh layouts supply
  // it as the vCoverage varying, and the ellipse, hairline and complex rounded-rect layouts
  // evaluate their per-pixel edge coverage ahead of the chain in the kernel (gpPixelCoverage). The
  // atlas route never carries a subtree: its glyph leaf rides the clip-coverage register instead
  // (AOTChainBuilder).
  // Four-leaf variants apply the mask through a runtime uniform, so the compile-time mask
  // dimension exists only for leaf-free chains, and those stay on the rect layout without quad
  // attributes, the hairline layout (a path clip over a solid or gradient hairline) or the
  // complex rounded-rect layouts.
  int hasMaskTexture = inputs.textureCount == 0 && inputs.hasMask ? 1 : 0;
  if (hasMaskTexture != 0 && (gpLayout == 1 || hasUVCoord != 0)) {
    return std::nullopt;
  }
  // The per-quad subset is a QuadPerEdgeAAGP attribute that clamps a texture leaf, so it exists
  // only on the quad profile with the four-leaf slots.
  if (inputs.hasSubset && (inputs.gpKind != 1 || inputs.textureCount != 1)) {
    return std::nullopt;
  }
  int hasSubset = inputs.hasSubset ? 1 : 0;
  using VD = PointwiseChainShader::VD;
  using FD = PointwiseChainShader::FD;
  RuleComposedValues values;
  values.vertValues.resize(VD::COUNT);
  values.vertValues[VD::GP_LAYOUT] = gpLayout;
  values.vertValues[VD::HAS_COVERAGE] = hasCoverage;
  values.vertValues[VD::HAS_UV_COORD] = hasUVCoord;
  values.vertValues[VD::TEXTURE_COUNT] = inputs.textureCount;
  values.vertValues[VD::HAS_SUBSET] = hasSubset;
  values.fragValues.resize(FD::COUNT);
  values.fragValues[FD::GP_LAYOUT] = gpLayout;
  values.fragValues[FD::HAS_XP] = inputs.xpType;
  values.fragValues[FD::HAS_COVERAGE] = hasCoverage;
  values.fragValues[FD::TEXTURE_COUNT] = inputs.textureCount;
  values.fragValues[FD::HAS_MASK_TEXTURE] = hasMaskTexture;
  values.fragValues[FD::HAS_SUBSET] = hasSubset;
  return values;
}

std::optional<RuleComposedValues> ComposePointwiseChain(const PointwiseChainInputs& inputs) {
  auto profile = inputs.resourceProfile;
  // The plan layer resolves the profile before matching (an Automatic profile never auto-promotes,
  // as it never did on the pre-merge Graph family's match path): a graph profile selects the
  // extended resource kind, everything else is a plain chain.
  int resourceKind = 0;
  if (profile == AOTResourceProfile::NoiseGraph) {
    resourceKind = 2;
  } else if (profile == AOTResourceProfile::TiledGraph) {
    resourceKind = 1;
  } else if (inputs.hasNoise || inputs.hasGradient ||
             (profile != AOTResourceProfile::Automatic && profile != AOTResourceProfile::Chain)) {
    return std::nullopt;
  }
  if (resourceKind == 2 && (!inputs.hasNoise || inputs.hasGradient || inputs.gpKind != 1 ||
                            !inputs.hasCoverage || inputs.hasUVCoord || inputs.textureCount != 1)) {
    return std::nullopt;
  }
  if (resourceKind == 1 && inputs.hasNoise) {
    return std::nullopt;
  }
  auto values = ComposePointwiseGP(inputs);
  if (!values) {
    return std::nullopt;
  }
  if (resourceKind != 0) {
    if (!PointwiseExtendedChainValuesAllowed(values->fragValues)) {
      return std::nullopt;
    }
    values->vertValues.resize(PointwiseChainShader::VD::COUNT);
    values->vertValues[PointwiseChainShader::VD::HAS_SOURCE_COORDS] = 1;
  }
  values->fragValues.resize(PointwiseChainShader::FD::COUNT);
  values->fragValues[PointwiseChainShader::FD::RESOURCE_KIND] = resourceKind;
  return values;
}

std::set<std::pair<uint32_t, uint32_t>> EnumeratePointwiseChainReachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  for (int gpKind = 0; gpKind <= 7; ++gpKind) {
    for (int hasCoverage = 0; hasCoverage <= 1; ++hasCoverage) {
      for (int hasUVCoord = 0; hasUVCoord <= 1; ++hasUVCoord) {
        for (int textureCount = 0; textureCount <= 1; ++textureCount) {
          for (int hasMask = 0; hasMask <= 1; ++hasMask) {
            for (int hasCoverageSubtree = 0; hasCoverageSubtree <= 1; ++hasCoverageSubtree) {
              for (int xpType = -1; xpType <= 2; ++xpType) {
                for (int hasSubset = 0; hasSubset <= 1; ++hasSubset) {
                  PointwiseChainInputs inputs;
                  inputs.gpKind = gpKind;
                  inputs.hasCoverage = hasCoverage != 0;
                  inputs.hasUVCoord = hasUVCoord != 0;
                  inputs.textureCount = textureCount;
                  inputs.hasMask = hasMask != 0;
                  inputs.hasCoverageSubtree = hasCoverageSubtree != 0;
                  inputs.xpType = xpType;
                  inputs.hasSubset = hasSubset != 0;
                  auto composed = ComposePointwiseChain(inputs);
                  if (!composed) {
                    continue;
                  }
                  auto vertIndex = PointwiseChainShader::VD::domain().encode(composed->vertValues);
                  auto fragIndex = PointwiseChainShader::FD::domain().encode(composed->fragValues);
                  result.insert({vertIndex, fragIndex});
                }
              }
            }
          }
        }
      }
    }
  }
  // The extended resource kinds mirror the plain chain's reachable set (every shape their rule
  // admits — no per-quad subset), plus the noise kind's fixed common-color AA quad subset. This is
  // the pre-merge PointwiseGraphShader enumeration, folded in.
  using VD = PointwiseChainShader::VD;
  using FD = PointwiseChainShader::FD;
  std::vector<std::pair<uint32_t, uint32_t>> plainPairs(result.begin(), result.end());
  for (const auto& indices : plainPairs) {
    auto fragValues = FD::domain().decode(indices.second);
    if (!PointwiseExtendedChainValuesAllowed(fragValues)) {
      continue;
    }
    auto vertValues = VD::domain().decode(indices.first);
    fragValues.resize(FD::COUNT);
    fragValues[FD::RESOURCE_KIND] = 1;
    vertValues.resize(VD::COUNT);
    vertValues[VD::HAS_SOURCE_COORDS] = 1;
    result.insert({VD::domain().encode(vertValues), FD::domain().encode(fragValues)});
  }
  PointwiseChainInputs inputs;
  inputs.gpKind = 1;
  inputs.hasCoverage = true;
  inputs.textureCount = 1;
  inputs.resourceProfile = AOTResourceProfile::NoiseGraph;
  inputs.hasNoise = true;
  for (int xpType = -1; xpType <= 3; ++xpType) {
    inputs.xpType = xpType;
    auto composed = ComposePointwiseChain(inputs);
    if (composed) {
      result.insert(
          {VD::domain().encode(composed->vertValues), FD::domain().encode(composed->fragValues)});
    }
  }
  return result;
}

std::optional<std::set<std::pair<uint32_t, uint32_t>>> EnumerateReachablePermutations(
    const std::string& shaderName) {
  return ShaderRegistry::Enumerate(shaderName);
}

}  // namespace tgfx

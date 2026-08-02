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

#include "PermutationMatcher.h"
#include <skcms.h>
#include "gpu/Swizzle.h"
#include "gpu/processors/AARectEffect.h"
#include "gpu/processors/AOTPointwiseTailProcessor.h"
#include "gpu/processors/AlphaThresholdFragmentProcessor.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "gpu/processors/ClampedGradientEffect.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/ColorSpaceXFormEffect.h"
#include "gpu/processors/ComplexEllipseGeometryProcessor.h"
#include "gpu/processors/ComplexNonAARRectGeometryProcessor.h"
#include "gpu/processors/ComposeFragmentProcessor.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/DualIntervalGradientColorizer.h"
#include "gpu/processors/EllipseGeometryProcessor.h"
#include "gpu/processors/EmptyXferProcessor.h"
#include "gpu/processors/GaussianBlur1DFragmentProcessor.h"
#include "gpu/processors/HairlineLineGeometryProcessor.h"
#include "gpu/processors/HairlineQuadGeometryProcessor.h"
#include "gpu/processors/LumaFragmentProcessor.h"
#include "gpu/processors/MeshGeometryProcessor.h"
#include "gpu/processors/NonAARRectGeometryProcessor.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "gpu/processors/RoundStrokeRectGeometryProcessor.h"
#include "gpu/processors/ShapeInstancedGeometryProcessor.h"
#include "gpu/processors/SingleIntervalGradientColorizer.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TextureGradientColorizer.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/processors/UnrolledBinaryGradientColorizer.h"
#include "gpu/processors/XfermodeFragmentProcessor.h"
#include "gpu/shaders/level1/AtlasTextFillShader.h"
#include "gpu/shaders/level1/BlendMergeShader.h"
#include "gpu/shaders/level1/ComplexEllipseFillShader.h"
#include "gpu/shaders/level1/ComplexNonAARRectFillShader.h"
#include "gpu/shaders/level1/ConstColorShader.h"
#include "gpu/shaders/level1/DeviceSpaceTextureShader.h"
#include "gpu/shaders/level1/DeviceSpaceTexturedEffectShader.h"
#include "gpu/shaders/level1/DualIntervalGradientShader.h"
#include "gpu/shaders/level1/EllipseFillShader.h"
#include "gpu/shaders/level1/GaussianBlur1DShader.h"
#include "gpu/shaders/level1/GradientFillShader.h"
#include "gpu/shaders/level1/HairlineLineShader.h"
#include "gpu/shaders/level1/HairlineQuadShader.h"
#include "gpu/shaders/level1/MaskFillShader.h"
#include "gpu/shaders/level1/MeshFillShader.h"
#include "gpu/shaders/level1/NonAARRectFillShader.h"
#include "gpu/shaders/level1/PerlinNoiseFillShader.h"
#include "gpu/shaders/level1/PointwiseDirectShader.h"
#include "gpu/shaders/level1/PointwiseTailShader.h"
#include "gpu/shaders/level1/QuadColorFillShader.h"
#include "gpu/shaders/level1/QuadTextureFillShader.h"
#include "gpu/shaders/level1/RoundStrokeRectFillShader.h"
#include "gpu/shaders/level1/ShapeInstancedFillShader.h"
#include "gpu/shaders/level1/SingleIntervalGradientShader.h"
#include "gpu/shaders/level1/SolidColorFillShader.h"
#include "gpu/shaders/level1/TextureColorMatrixShader.h"
#include "gpu/shaders/level1/TextureFillShader.h"
#include "gpu/shaders/level1/TextureGradientShader.h"
#include "gpu/shaders/level1/TexturedEffectShader.h"
#include "gpu/shaders/level1/TiledTextureFillShader.h"

namespace tgfx {

// Returns 0 for EmptyXferProcessor, 1 for PorterDuffXferProcessor with dst texture (DST_TEX mode),
// or -1 for unsupported XP types (including PorterDuff with framebuffer fetch).
static int GetXPType(const ProgramInfo* programInfo) {
  auto xp = programInfo->getXferProcessor();
  if (xp == EmptyXferProcessor::GetInstance()) {
    return 0;
  }
  if (xp->name() == "PorterDuffXferProcessor") {
    if (xp->dstTextureView() != nullptr) {
      return 1;  // DST_TEX mode
    }
    return 2;  // Framebuffer Fetch mode
  }
  return -1;
}

// Determines whether a texture-based coverage child can be served by the local-space mask path
// (HAS_LOCAL_MASK): the shader samples it via a coordinate-transform varying and takes .a, relying
// on the hardware sampler for tiling. That is only valid for an RGBA texture (so .a is the alpha),
// with no RGBAAA dual-plane and no shader-emitted subset/tile math. A plain TextureEffect always
// qualifies (hardware tiling + a benign full-bounds Subset); a TiledTextureEffect qualifies only
// when both axes resolve to the hardware path (ShaderMode::None == 0), i.e. no shader tile math.
static bool IsLocalMaskChild(const FragmentProcessor* child) {
  auto name = child->name();
  if (name == "TextureEffect") {
    auto* te = static_cast<const TextureEffect*>(child);
    return !te->isYUV() && !te->isAlphaOnly() && !te->hasRGBAAA() && !te->hasSubset();
  }
  if (name == "TiledTextureEffect") {
    auto* tte = static_cast<const TiledTextureEffect*>(child);
    if (tte->isAlphaOnly()) {
      return false;
    }
    int modeX = 0;
    int modeY = 0;
    tte->getShaderModes(&modeX, &modeY);
    return modeX == 0 && modeY == 0;
  }
  return false;
}

enum class CoverageKind {
  None,
  AARect,
  DeviceMask,
  LocalMask,
};

// Classifies coverage semantics independently of any shader's permutation layout. LocalMask is a
// distinct capability: it needs a local-space varying and sampler and therefore cannot be encoded
// in the shared three-value HAS_COVERAGE dimension.
static std::optional<CoverageKind> ClassifyCoverageFP(const ProgramInfo* programInfo) {
  auto numFP = programInfo->numFragmentProcessors();
  auto numColorFP = programInfo->numColorFragmentProcessors();
  if (numFP == numColorFP) {
    return CoverageKind::None;
  }
  if (numFP != numColorFP + 1) {
    return std::nullopt;
  }
  auto coverageFP = programInfo->getFragmentProcessor(numColorFP);
  auto coverageName = coverageFP->name();
  if (coverageName == "AARectEffect") {
    return CoverageKind::AARect;
  }
  if (coverageName == "XfermodeFragmentProcessor - dst") {
    // Only the non-inverted ShaderMaskFilter is input * mask.a (SrcIn). The inverted SrcOut form
    // needs 1 - mask.a, which the local-mask kernel does not implement.
    auto* xfermode = static_cast<const XfermodeFragmentProcessor*>(coverageFP);
    if (xfermode->getMode() != BlendMode::SrcIn || coverageFP->numChildProcessors() != 1 ||
        !IsLocalMaskChild(coverageFP->childProcessor(0))) {
      return std::nullopt;
    }
    return CoverageKind::LocalMask;
  }
  if (coverageName == "ComposeFragmentProcessor") {
    if (coverageFP->numChildProcessors() != 2) {
      return std::nullopt;
    }
    auto child0 = coverageFP->childProcessor(0);
    auto child1 = coverageFP->childProcessor(1);
    if (child0->name() != "DeviceSpaceTextureEffect" || child1->name() != "AARectEffect") {
      return std::nullopt;
    }
    auto* dste = static_cast<const DeviceSpaceTextureEffect*>(child0);
    if (!dste->isAlphaOnly()) {
      return std::nullopt;
    }
    return CoverageKind::DeviceMask;
  }
  if (coverageName == "DeviceSpaceTextureEffect") {
    auto* dste = static_cast<const DeviceSpaceTextureEffect*>(coverageFP);
    if (!dste->isAlphaOnly()) {
      return std::nullopt;
    }
    return CoverageKind::DeviceMask;
  }
  return std::nullopt;
}

static std::optional<int> SharedCoverageValue(const ProgramInfo* programInfo) {
  auto coverage = ClassifyCoverageFP(programInfo);
  if (!coverage || *coverage == CoverageKind::LocalMask) {
    return std::nullopt;
  }
  return static_cast<int>(*coverage);
}

static std::optional<PermutationMatchResult> TryMatchTextureFill(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "DefaultGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numColorFragmentProcessors() != 1) {
    return std::nullopt;
  }
  auto coverageType = SharedCoverageValue(programInfo);
  if (!coverageType) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "TextureEffect") {
    return std::nullopt;
  }
  auto* te = static_cast<const TextureEffect*>(fp);
  if (te->numTextureSamplers() == 0) {
    return std::nullopt;
  }
  // YUV textures require additional dimensions (Limited/Full range, I420/NV12 format) that are not
  // yet covered by the precompiled shader. Fall back to ProgramBuilder for YUV.
  if (te->isYUV()) {
    return std::nullopt;
  }
  // alphaOnly / hasRGBAAA are runtime uniforms (AlphaOnly / HasRgbaaa) written by
  // GLSLTextureEffect::onSetData, not permutation dimensions.
  TextureFillShader::VertexValues vertexValues = {};
  vertexValues.hasSubset = te->hasSubset();
  auto vertIndex = TextureFillShader::EncodeVertex(vertexValues);

  TextureFillShader::FragmentValues fragmentValues = {};
  fragmentValues.hasSubset = te->hasSubset();
  fragmentValues.xp = static_cast<uint32_t>(xpType);
  fragmentValues.coverage = static_cast<uint32_t>(*coverageType);
  auto fragIndex = TextureFillShader::EncodeFragment(fragmentValues);
  return PermutationMatchResult{TextureFillShader::Name(), vertIndex, fragIndex};
}

// ShaderMode values the shared single-tap tiled_sample.inc handles for the blur tap loop and the
// blend-merge tiled child: None(0), Clamp(1), RepeatNearestNone(2), MirrorRepeat(6),
// ClampToBorderNearest(7), ClampToBorderLinear(8). The RepeatLinear/mipmap modes 3/4/5 are only
// implemented by the tiled fill kernel (see TiledFillModeSupported); blur and blend-merge keep
// falling back for those to avoid diverging from the runtime path in those consumers.
static bool TiledModeSupported(int mode) {
  return mode == 0 || mode == 1 || mode == 2 || mode == 6 || mode == 7 || mode == 8;
}

// The tiled fill kernel additionally handles RepeatLinearNone(3) — shares mode 2's coordinate, the
// linear vs nearest difference being the sampler filter — and the mipmap-repeat modes 4/5 via an
// inline 4-tap seam blend. So it accepts every mode.
static bool TiledFillModeSupported(int mode) {
  return TiledModeSupported(mode) || mode == 3 || mode == 4 || mode == 5;
}

static std::optional<PermutationMatchResult> TryMatchTiledTextureFill(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  int gpType = 0;
  bool isQuad = false;
  int hasCoverage = 0;
  if (gp->name() == "DefaultGeometryProcessor") {
    gpType = 0;
    hasCoverage =
        static_cast<const DefaultGeometryProcessor*>(gp)->getAAType() == AAType::Coverage ? 1 : 0;
  } else if (gp->name() == "QuadPerEdgeAAGeometryProcessor") {
    auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
    // The precompiled quad vert declares aPosition plus an optional AA coverage attribute; it has no
    // uvCoord/color/subset attributes. hasUVMatrix() means the coord comes from aPosition. Per-vertex
    // AA coverage is now supported via the HAS_COVERAGE dimension, so it is no longer rejected.
    if (!quadGP->hasCommonColor() || !quadGP->hasUVMatrix() || quadGP->getHasSubset()) {
      return std::nullopt;
    }
    gpType = 1;
    isQuad = true;
    hasCoverage = quadGP->getAAType() == AAType::Coverage ? 1 : 0;
  } else {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 1) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "TiledTextureEffect") {
    return std::nullopt;
  }
  auto* tte = static_cast<const TiledTextureEffect*>(fp);
  if (tte->hasPerspective()) {
    return std::nullopt;
  }
  int modeX = 0;
  int modeY = 0;
  tte->getShaderModes(&modeX, &modeY);
  if (!TiledFillModeSupported(modeX) || !TiledFillModeSupported(modeY)) {
    return std::nullopt;
  }
  // For the DefaultGeometryProcessor path a both-None (0/0) tiled effect reduces to a plain texture
  // fill, which TryMatchTextureFill already handles, so reject it here. The QuadPerEdgeAA path has
  // no such plain fallback for a TiledTextureEffect, so a 0/0 quad tiled fill must match here.
  if (!isQuad && modeX == 0 && modeY == 0) {
    return std::nullopt;
  }
  // ALPHA_ONLY and HAS_STRICT are folded into runtime uniforms (AlphaOnly / Strict), set by
  // GLSLTiledTextureEffect::onSetData; they are no longer compile-time dimensions.
  using VD = TiledTextureFillShader::VD;
  auto vertDomain = VD::domain();
  std::vector<int> vertValues(VD::COUNT, 0);
  vertValues[VD::GP_TYPE] = gpType;
  vertValues[VD::HAS_COVERAGE] = hasCoverage;
  auto vertIndex = vertDomain.encode(vertValues);

  using FD = TiledTextureFillShader::FragDims;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  fragValues[FD::HAS_XP] = xpType;
  fragValues[FD::HAS_COVERAGE] = hasCoverage;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"TiledTextureFillShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchSolidColorFill(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "DefaultGeometryProcessor") {
    return std::nullopt;
  }
  // Solid fill: no color and no coverage fragment processors. The fill color comes from the
  // DefaultGeometryProcessor Color uniform.
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto* dgp = static_cast<const DefaultGeometryProcessor*>(gp);
  int hasCoverage = dgp->getAAType() == AAType::Coverage ? 1 : 0;

  using VD = SolidColorFillShader::VD;
  auto vertDomain = VD::domain();
  std::vector<int> vertValues(VD::COUNT, 0);
  vertValues[VD::HAS_COVERAGE] = hasCoverage;
  auto vertIndex = vertDomain.encode(vertValues);

  using FD = SolidColorFillShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT, 0);
  fragValues[FD::HAS_COVERAGE] = hasCoverage;
  fragValues[FD::HAS_XP] = xpType;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"SolidColorFillShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchMaskFill(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "DefaultGeometryProcessor") {
    return std::nullopt;
  }
  // Solid color (from the DefaultGP Color uniform) masked by a single alpha-only coverage
  // TextureEffect (a rasterized shape mask from ShapeDrawOp). No color fragment processors.
  if (programInfo->numColorFragmentProcessors() != 0) {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 1) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "TextureEffect") {
    return std::nullopt;
  }
  auto* te = static_cast<const TextureEffect*>(fp);
  // Only the simple mask form is supported: a plain alpha-only texture with no subset, no packed
  // alpha, no YUV. Anything else falls back to ProgramBuilder.
  if (te->numTextureSamplers() == 0 || te->isYUV() || !te->isAlphaOnly() || te->hasRGBAAA() ||
      te->hasSubset()) {
    return std::nullopt;
  }
  // The mask coverage feeds the XferProcessor's coverage lerp, so Porter-Duff blends (DST_TEX and
  // framebuffer-fetch) are supported via the HAS_XP fragment dimension. Only a genuinely
  // unsupported transfer processor (xpType < 0) falls back to ProgramBuilder.
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  using D = MaskFillShader::D;
  std::vector<int> fragValues(D::COUNT, 0);
  fragValues[D::HAS_XP] = xpType;
  auto fragIndex = D::domain().encode(fragValues);
  return PermutationMatchResult{"MaskFillShader", 0, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchConstColor(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "DefaultGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 1) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "ConstColorProcessor") {
    return std::nullopt;
  }
  using FD = ConstColorShader::FragDims;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  // inputMode is a runtime uniform (InputMode), not a permutation dimension.
  fragValues[FD::HAS_XP] = xpType;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"ConstColorShader", 0, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchQuadColorFill(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "QuadPerEdgeAAGeometryProcessor") {
    return std::nullopt;
  }
  // Accept either zero fragment processors (plain colored quad) or a single device-space mask
  // coverage FP, which this shader samples via HAS_MASK_TEXTURE. Analytic AARect and local-space
  // masks are not supported here because QuadColorFill has no corresponding sampling path.
  bool hasMaskTexture = false;
  if (programInfo->numFragmentProcessors() != 0) {
    if (programInfo->numColorFragmentProcessors() != 0) {
      return std::nullopt;
    }
    auto coverage = ClassifyCoverageFP(programInfo);
    if (!coverage || *coverage != CoverageKind::DeviceMask) {
      return std::nullopt;
    }
    hasMaskTexture = true;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
  using VD = QuadColorFillShader::VD;
  auto vertDomain = VD::domain();
  std::vector<int> vertValues(VD::COUNT, 0);
  vertValues[VD::HAS_COVERAGE] = quadGP->getAAType() == AAType::Coverage ? 1 : 0;
  vertValues[VD::HAS_COLOR] = !quadGP->hasCommonColor() ? 1 : 0;
  auto vertIndex = vertDomain.encode(vertValues);

  using FD = QuadColorFillShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT, 0);
  fragValues[FD::HAS_COVERAGE] = quadGP->getAAType() == AAType::Coverage ? 1 : 0;
  fragValues[FD::HAS_COLOR] = !quadGP->hasCommonColor() ? 1 : 0;
  fragValues[FD::HAS_XP] = xpType;
  fragValues[FD::HAS_MASK_TEXTURE] = hasMaskTexture ? 1 : 0;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"QuadColorFillShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchQuadTextureFill(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "QuadPerEdgeAAGeometryProcessor") {
    return std::nullopt;
  }
  // Color must be exactly one TextureEffect. Coverage is optional: a device-space mask maps to
  // HAS_MASK_TEXTURE, while a local-space texture-alpha mask maps to HAS_LOCAL_MASK.
  if (programInfo->numColorFragmentProcessors() != 1) {
    return std::nullopt;
  }
  bool hasMaskTexture = false;
  bool hasLocalMask = false;
  if (programInfo->numFragmentProcessors() != 1) {
    auto coverage = ClassifyCoverageFP(programInfo);
    if (coverage && *coverage == CoverageKind::DeviceMask) {
      hasMaskTexture = true;
    } else if (coverage && *coverage == CoverageKind::LocalMask) {
      hasLocalMask = true;
    } else {
      return std::nullopt;
    }
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "TextureEffect") {
    return std::nullopt;
  }
  auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
  auto* te = static_cast<const TextureEffect*>(fp);
  if (te->numTextureSamplers() == 0) {
    return std::nullopt;
  }
  if (te->isYUV()) {
    return std::nullopt;
  }
  // No color-texture guards are needed for the local mask: the coverage texture writes its
  // per-texture uniforms (Subset/AlphaOnly/HasRgbaaa) under a distinct structural ordinal
  // (Subset_1, ...), so it can no longer clobber the color texture's Subset / AlphaOnly slots. The
  // color may therefore carry a subset, be alpha-only, or use RGBAAA freely.
  // Subset clamping logic:
  //   gpSubset=true  -> HAS_SUBSET=1: vertex shader outputs vTexSubset varying, fragment double-
  //                     clamps by the varying and the Subset uniform.
  //   gpSubset=false -> HAS_SUBSET=0: fragment always clamps by the Subset uniform alone. The
  //                     uniform is populated from computeSubsetRect, which yields the full texture
  //                     bounds when the source has no real subset, so the clamp is a no-op. This
  //                     subsumes the former HAS_CLAMP_SUBSET dimension.
  bool gpSubset = quadGP->getHasSubset();

  using VD = QuadTextureFillShader::VD;
  auto vertDomain = VD::domain();
  std::vector<int> vertValues(VD::COUNT, 0);
  vertValues[VD::HAS_COVERAGE] = quadGP->getAAType() == AAType::Coverage ? 1 : 0;
  vertValues[VD::HAS_UV_COORD] = !quadGP->hasUVMatrix() ? 1 : 0;
  vertValues[VD::HAS_COLOR] = !quadGP->hasCommonColor() ? 1 : 0;
  vertValues[VD::HAS_SUBSET] = gpSubset ? 1 : 0;
  vertValues[VD::HAS_LOCAL_MASK] = hasLocalMask ? 1 : 0;
  auto vertIndex = vertDomain.encode(vertValues);

  using FD = QuadTextureFillShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT, 0);
  // ALPHA_ONLY and HAS_RGBAAA are now runtime uniforms (set by GLSLTextureEffect::onSetData), not
  // variants.
  fragValues[FD::HAS_SUBSET] = gpSubset ? 1 : 0;
  fragValues[FD::HAS_COVERAGE] = quadGP->getAAType() == AAType::Coverage ? 1 : 0;
  fragValues[FD::HAS_COLOR] = !quadGP->hasCommonColor() ? 1 : 0;
  fragValues[FD::HAS_XP] = xpType;
  fragValues[FD::HAS_MASK_TEXTURE] = hasMaskTexture ? 1 : 0;
  fragValues[FD::HAS_LOCAL_MASK] = hasLocalMask ? 1 : 0;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"QuadTextureFillShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchDeviceSpaceTexture(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "DefaultGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 1) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "DeviceSpaceTextureEffect") {
    return std::nullopt;
  }
  using FD = DeviceSpaceTextureShader::Dims;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  // ALPHA_ONLY is a runtime uniform (AlphaOnly), not a permutation dimension.
  fragValues[FD::HAS_XP] = xpType;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"DeviceSpaceTextureShader", 0, fragIndex};
}

static int GetGPType(const GeometryProcessor* gp) {
  auto name = gp->name();
  if (name == "DefaultGeometryProcessor") {
    return 0;
  }
  if (name == "QuadPerEdgeAAGeometryProcessor") {
    return 1;
  }
  return -1;
}

// Returns 1 if the geometry processor supplies a per-vertex AA coverage attribute (inCoverage), 0
// otherwise. Coverage is orthogonal to the GP type: both DefaultGP and QuadPerEdgeAAGP emit it only
// when their AA type is Coverage.
static int GetGPCoverage(const GeometryProcessor* gp) {
  auto name = gp->name();
  if (name == "DefaultGeometryProcessor") {
    return static_cast<const DefaultGeometryProcessor*>(gp)->getAAType() == AAType::Coverage ? 1
                                                                                             : 0;
  }
  if (name == "QuadPerEdgeAAGeometryProcessor") {
    return static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp)->getAAType() == AAType::Coverage
               ? 1
               : 0;
  }
  return 0;
}

static std::optional<PermutationMatchResult> TryMatchPointwiseTail(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  int gpType = GetGPType(gp);
  if (gpType < 0 || programInfo->numColorFragmentProcessors() != 1 ||
      programInfo->numFragmentProcessors() != 1) {
    return std::nullopt;
  }
  if (gpType == 1) {
    auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
    if (!quadGP->hasCommonColor() || !quadGP->hasUVMatrix() || quadGP->getHasSubset()) {
      return std::nullopt;
    }
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "AOTPointwiseTailProcessor" || fp->numChildProcessors() != 1) {
    return std::nullopt;
  }
  auto* tail = static_cast<const AOTPointwiseTailProcessor*>(fp);
  auto source = tail->source();
  bool hasSubset = false;
  if (tail->sourceKind() == AOTPointwiseTailProcessor::SourceKind::Plain) {
    if (source->name() != "TextureEffect" || source->numTextureSamplers() != 1) {
      return std::nullopt;
    }
    auto* texture = static_cast<const TextureEffect*>(source);
    if (texture->isYUV() || texture->isAlphaOnly() || texture->hasRGBAAA() ||
        source->coordTransform(0)->matrix.hasPerspective()) {
      return std::nullopt;
    }
    hasSubset = texture->hasSubset();
  } else {
    if (source->name() != "DeviceSpaceTextureEffect" || source->numTextureSamplers() != 1) {
      return std::nullopt;
    }
    auto* texture = static_cast<const DeviceSpaceTextureEffect*>(source);
    if (texture->isAlphaOnly() || texture->hasPerspective()) {
      return std::nullopt;
    }
  }

  int hasCoverage = GetGPCoverage(gp);
  using VD = PointwiseTailShader::VD;
  std::vector<int> vertValues(VD::COUNT, 0);
  vertValues[VD::GP_TYPE] = gpType;
  vertValues[VD::HAS_COVERAGE] = hasCoverage;
  auto vertIndex = VD::domain().encode(vertValues);

  using FD = PointwiseTailShader::FD;
  std::vector<int> fragValues(FD::COUNT, 0);
  fragValues[FD::HAS_SUBSET] = hasSubset ? 1 : 0;
  fragValues[FD::HAS_XP] = xpType;
  fragValues[FD::HAS_COVERAGE] = hasCoverage;
  auto fragIndex = FD::domain().encode(fragValues);
  return PermutationMatchResult{"PointwiseTailShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchDeviceSpaceTexturedEffect(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  int gpType = GetGPType(gp);
  if (gpType < 0) {
    return std::nullopt;
  }
  if (gpType == 1) {
    auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
    if (!quadGP->hasCommonColor() || !quadGP->hasUVMatrix() || quadGP->getHasSubset()) {
      return std::nullopt;
    }
  }
  if (programInfo->numFragmentProcessors() != 1 || programInfo->numColorFragmentProcessors() != 1) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto composed = programInfo->getFragmentProcessor(0);
  if (composed->name() != "ComposeFragmentProcessor" || composed->numChildProcessors() != 2) {
    return std::nullopt;
  }
  auto source = composed->childProcessor(0);
  auto pointwise = composed->childProcessor(1);
  if (source->name() != "DeviceSpaceTextureEffect" || source->numTextureSamplers() != 1) {
    return std::nullopt;
  }
  auto* deviceTexture = static_cast<const DeviceSpaceTextureEffect*>(source);
  if (deviceTexture->isAlphaOnly()) {
    return std::nullopt;
  }
  if (pointwise->name() != "ColorMatrixFragmentProcessor" &&
      pointwise->name() != "LumaFragmentProcessor") {
    return std::nullopt;
  }

  int hasCoverage = GetGPCoverage(gp);
  using VD = DeviceSpaceTexturedEffectShader::VD;
  std::vector<int> vertValues(VD::COUNT, 0);
  vertValues[VD::GP_TYPE] = gpType;
  vertValues[VD::HAS_COVERAGE] = hasCoverage;
  auto vertIndex = VD::domain().encode(vertValues);

  using FD = DeviceSpaceTexturedEffectShader::FD;
  std::vector<int> fragValues(FD::COUNT, 0);
  fragValues[FD::HAS_XP] = xpType;
  fragValues[FD::HAS_COVERAGE] = hasCoverage;
  auto fragIndex = FD::domain().encode(fragValues);
  return PermutationMatchResult{"DeviceSpaceTexturedEffectShader", vertIndex, fragIndex};
}

static int GradientLayoutTypeIndex(const std::string& layoutName) {
  if (layoutName == "LinearGradientLayout") {
    return 0;
  }
  if (layoutName == "RadialGradientLayout") {
    return 1;
  }
  if (layoutName == "ConicGradientLayout") {
    return 2;
  }
  if (layoutName == "DiamondGradientLayout") {
    return 3;
  }
  return -1;
}

static std::optional<PermutationMatchResult> TryMatchGradientFill(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  int gpType = GetGPType(gp);
  if (gpType < 0) {
    return std::nullopt;
  }
  if (gpType == 1) {
    auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
    if (!quadGP->hasUVMatrix()) {
      return std::nullopt;
    }
  }
  if (programInfo->numColorFragmentProcessors() != 1) {
    return std::nullopt;
  }
  auto coverageType = SharedCoverageValue(programInfo);
  if (!coverageType) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "ClampedGradientEffect") {
    return std::nullopt;
  }
  if (fp->numChildProcessors() != 2) {
    return std::nullopt;
  }
  auto colorizer = fp->childProcessor(0);
  auto layout = fp->childProcessor(1);
  if (colorizer->name() != "UnrolledBinaryGradientColorizer") {
    return std::nullopt;
  }
  int layoutType = GradientLayoutTypeIndex(layout->name());
  if (layoutType < 0) {
    return std::nullopt;
  }
  if (layout->numCoordTransforms() > 0 && layout->coordTransform(0)->matrix.hasPerspective()) {
    return std::nullopt;
  }
  auto* ubgc = static_cast<const UnrolledBinaryGradientColorizer*>(colorizer);
  int intervalCount = ubgc->getIntervalCount();
  if (intervalCount < 1 || intervalCount > 8) {
    return std::nullopt;
  }

  // Per-vertex AA coverage is carried through the HAS_VCOVERAGE dimension (the shared vertex shader
  // emits a vCoverage varying the fragment shader multiplies in), so coverage-carrying draws are
  // served rather than rejected.
  int vCoverage = GetGPCoverage(gp);

  using VD = GradientFillShader::VD;
  auto vertDomain = VD::domain();
  std::vector<int> vertValues(VD::COUNT);
  vertValues[VD::GP_TYPE] = gpType;
  vertValues[VD::HAS_VCOVERAGE] = vCoverage;
  auto vertIndex = vertDomain.encode(vertValues);

  using FD = GradientFillShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  fragValues[FD::HAS_XP] = xpType;
  fragValues[FD::HAS_COVERAGE] = *coverageType;
  fragValues[FD::HAS_VCOVERAGE] = vCoverage;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"GradientFillShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchSingleIntervalGradient(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  int gpType = GetGPType(gp);
  if (gpType < 0) {
    return std::nullopt;
  }
  if (gpType == 1) {
    auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
    if (!quadGP->hasUVMatrix()) {
      return std::nullopt;
    }
  }
  if (programInfo->numColorFragmentProcessors() != 1) {
    return std::nullopt;
  }
  auto coverageType = SharedCoverageValue(programInfo);
  if (!coverageType) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "ClampedGradientEffect") {
    return std::nullopt;
  }
  if (fp->numChildProcessors() != 2) {
    return std::nullopt;
  }
  auto colorizer = fp->childProcessor(0);
  auto layout = fp->childProcessor(1);
  if (colorizer->name() != "SingleIntervalGradientColorizer") {
    return std::nullopt;
  }
  int layoutType = GradientLayoutTypeIndex(layout->name());
  if (layoutType < 0) {
    return std::nullopt;
  }
  if (layout->numCoordTransforms() > 0 && layout->coordTransform(0)->matrix.hasPerspective()) {
    return std::nullopt;
  }

  int vCoverage = GetGPCoverage(gp);

  using VD = SingleIntervalGradientShader::VD;
  auto vertDomain = VD::domain();
  std::vector<int> vertValues(VD::COUNT);
  vertValues[VD::GP_TYPE] = gpType;
  vertValues[VD::HAS_VCOVERAGE] = vCoverage;
  auto vertIndex = vertDomain.encode(vertValues);

  using FD = SingleIntervalGradientShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  // GP_TYPE is a vertex-only dimension; the fragment stage is identical for all GP types.
  fragValues[FD::HAS_XP] = xpType;
  fragValues[FD::HAS_COVERAGE] = *coverageType;
  fragValues[FD::HAS_VCOVERAGE] = vCoverage;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"SingleIntervalGradientShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchDualIntervalGradient(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  int gpType = GetGPType(gp);
  if (gpType < 0) {
    return std::nullopt;
  }
  if (gpType == 1) {
    auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
    if (!quadGP->hasUVMatrix()) {
      return std::nullopt;
    }
  }
  if (programInfo->numColorFragmentProcessors() != 1) {
    return std::nullopt;
  }
  auto coverageType = SharedCoverageValue(programInfo);
  if (!coverageType) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "ClampedGradientEffect") {
    return std::nullopt;
  }
  if (fp->numChildProcessors() != 2) {
    return std::nullopt;
  }
  auto colorizer = fp->childProcessor(0);
  auto layout = fp->childProcessor(1);
  if (colorizer->name() != "DualIntervalGradientColorizer") {
    return std::nullopt;
  }
  int layoutType = GradientLayoutTypeIndex(layout->name());
  if (layoutType < 0) {
    return std::nullopt;
  }
  if (layout->numCoordTransforms() > 0 && layout->coordTransform(0)->matrix.hasPerspective()) {
    return std::nullopt;
  }

  // Per-vertex AA coverage is carried through the HAS_VCOVERAGE dimension (the shared vertex shader
  // emits a vCoverage varying the fragment shader multiplies in), so coverage-carrying draws are
  // served rather than rejected.
  int vCoverage = GetGPCoverage(gp);

  using VD = DualIntervalGradientShader::VD;
  auto vertDomain = VD::domain();
  std::vector<int> vertValues(VD::COUNT);
  vertValues[VD::GP_TYPE] = gpType;
  vertValues[VD::HAS_VCOVERAGE] = vCoverage;
  auto vertIndex = vertDomain.encode(vertValues);

  using FD = DualIntervalGradientShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  fragValues[FD::HAS_XP] = xpType;
  fragValues[FD::HAS_COVERAGE] = *coverageType;
  fragValues[FD::HAS_VCOVERAGE] = vCoverage;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"DualIntervalGradientShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchTextureGradient(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  int gpType = GetGPType(gp);
  if (gpType < 0) {
    return std::nullopt;
  }
  if (gpType == 1) {
    auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
    if (!quadGP->hasUVMatrix()) {
      return std::nullopt;
    }
  }
  if (programInfo->numColorFragmentProcessors() != 1) {
    return std::nullopt;
  }
  auto coverageType = SharedCoverageValue(programInfo);
  if (!coverageType) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "ClampedGradientEffect") {
    return std::nullopt;
  }
  if (fp->numChildProcessors() != 2) {
    return std::nullopt;
  }
  auto colorizer = fp->childProcessor(0);
  auto layout = fp->childProcessor(1);
  if (colorizer->name() != "TextureGradientColorizer") {
    return std::nullopt;
  }
  int layoutType = GradientLayoutTypeIndex(layout->name());
  if (layoutType < 0) {
    return std::nullopt;
  }
  if (layout->numCoordTransforms() > 0 && layout->coordTransform(0)->matrix.hasPerspective()) {
    return std::nullopt;
  }

  // This kernel does not consume per-vertex AA coverage and its shared vertex shader (HAS_VCOVERAGE
  // defaults to 0 here) declares no inCoverage attribute, so reject coverage-carrying draws.
  if (GetGPCoverage(gp)) {
    return std::nullopt;
  }

  using VD = TextureGradientShader::VD;
  auto vertDomain = VD::domain();
  std::vector<int> vertValues(VD::COUNT);
  vertValues[VD::GP_TYPE] = gpType;
  auto vertIndex = vertDomain.encode(vertValues);

  using FD = TextureGradientShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  fragValues[FD::HAS_XP] = xpType;
  fragValues[FD::HAS_COVERAGE] = *coverageType;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"TextureGradientShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchTextureColorMatrix(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "DefaultGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 2) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto fp0 = programInfo->getFragmentProcessor(0);
  auto fp1 = programInfo->getFragmentProcessor(1);
  if (fp0->name() != "TextureEffect" || fp1->name() != "ColorMatrixFragmentProcessor") {
    return std::nullopt;
  }
  auto* te = static_cast<const TextureEffect*>(fp0);
  if (te->isYUV()) {
    return std::nullopt;
  }
  using D = TextureColorMatrixShader::D;
  auto fragDomain = D::domain();
  std::vector<int> fragValues(D::COUNT);
  // alphaOnly / hasRGBAAA are runtime uniforms (AlphaOnly / HasRgbaaa), not permutation dimensions.
  fragValues[D::HAS_SUBSET] = te->hasSubset() ? 1 : 0;
  fragValues[D::HAS_XP] = xpType;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"TextureColorMatrixShader", 0, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchAtlasTextFill(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "AtlasTextGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto* atgp = static_cast<const AtlasTextGeometryProcessor*>(gp);
  using D = AtlasTextFillShader::D;
  auto domain = D::domain();
  std::vector<int> values(D::COUNT);
  values[D::HAS_COVERAGE] = atgp->getAAType() == AAType::Coverage ? 1 : 0;
  values[D::HAS_COMMON_COLOR] = atgp->hasCommonColor() ? 1 : 0;
  auto vertIndex = domain.encode(values);
  using FD = AtlasTextFillShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  for (size_t i = 0; i < D::COUNT; ++i) {
    fragValues[i] = values[i];
  }
  fragValues[FD::HAS_XP] = xpType;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"AtlasTextFillShader", vertIndex, fragIndex};
}

static int TFTypeToIndex(gfx::skcms_TFType type) {
  switch (type) {
    case gfx::skcms_TFType_sRGBish:
      return 0;
    case gfx::skcms_TFType_PQish:
      return 1;
    case gfx::skcms_TFType_HLGish:
      return 2;
    case gfx::skcms_TFType_HLGinvish:
      return 3;
    default:
      return -1;
  }
}

// Matches a single pointwise operator applied straight to the input color, with no texture source:
// Luma, AlphaThreshold or ColorSpaceXform. These were three separate shaders whose skeletons were
// identical; the operator is now the OpType runtime uniform of PointwiseDirectShader.
static std::optional<PermutationMatchResult> TryMatchPointwiseDirect(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  int gpType = GetGPType(gp);
  if (gpType < 0 || programInfo->numFragmentProcessors() != 1) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  auto name = fp->name();
  if (name != "LumaFragmentProcessor" && name != "AlphaStepFragmentProcessor" &&
      name != "ColorSpaceXformEffect") {
    return std::nullopt;
  }
  if (name == "ColorSpaceXformEffect") {
    auto* effect = static_cast<const ColorSpaceXformEffect*>(fp);
    auto* xform = effect->colorXform();
    if (xform == nullptr) {
      return std::nullopt;
    }
    // The pipeline steps are runtime uniforms (CSFlags), so any flag combination hits the same
    // variant. Only the transfer-function types must be in the range the shared operator encodes.
    if (xform->flags.linearize &&
        TFTypeToIndex(gfx::skcms_TransferFunction_getType(
            reinterpret_cast<const gfx::skcms_TransferFunction*>(&xform->srcTransferFunction))) <
            0) {
      return std::nullopt;
    }
    if (xform->flags.encode && TFTypeToIndex(gfx::skcms_TransferFunction_getType(
                                   reinterpret_cast<const gfx::skcms_TransferFunction*>(
                                       &xform->dstTransferFunctionInverse))) < 0) {
      return std::nullopt;
    }
  }
  int hasCoverage = GetGPCoverage(gp);
  using VD = PointwiseDirectShader::VD;
  std::vector<int> vertValues(VD::COUNT, 0);
  vertValues[VD::GP_TYPE] = gpType;
  vertValues[VD::HAS_COVERAGE] = hasCoverage;
  auto vertIndex = VD::domain().encode(vertValues);
  using FD = PointwiseDirectShader::FD;
  std::vector<int> fragValues(FD::COUNT, 0);
  fragValues[FD::HAS_XP] = xpType;
  fragValues[FD::HAS_COVERAGE] = hasCoverage;
  auto fragIndex = FD::domain().encode(fragValues);
  return PermutationMatchResult{"PointwiseDirectShader", vertIndex, fragIndex};
}

// Returns true if fp is one of the four pointwise operators that share the runtime OpType uniform
// (ColorMatrix / Luma / AlphaThreshold / ColorSpaceXform). For ColorSpaceXform the transfer-function
// type must be in the supported 0-3 range; the pipeline flags themselves are runtime uniforms.
static bool IsSupportedPointwiseOp(const FragmentProcessor* fp) {
  if (fp->name() == "ColorMatrixFragmentProcessor" || fp->name() == "LumaFragmentProcessor" ||
      fp->name() == "AlphaStepFragmentProcessor") {
    return true;
  }
  if (fp->name() != "ColorSpaceXformEffect") {
    return false;
  }
  auto* cse = static_cast<const ColorSpaceXformEffect*>(fp);
  auto* xform = cse->colorXform();
  if (!xform) {
    return false;
  }
  if (xform->flags.linearize) {
    int srcIdx = TFTypeToIndex(gfx::skcms_TransferFunction_getType(
        reinterpret_cast<const gfx::skcms_TransferFunction*>(&xform->srcTransferFunction)));
    if (srcIdx < 0) {
      return false;
    }
  }
  if (xform->flags.encode) {
    int dstIdx = TFTypeToIndex(gfx::skcms_TransferFunction_getType(
        reinterpret_cast<const gfx::skcms_TransferFunction*>(&xform->dstTransferFunctionInverse)));
    if (dstIdx < 0) {
      return false;
    }
  }
  return true;
}

static std::optional<PermutationMatchResult> TryMatchComposedTexture(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  int gpType = 0;
  int hasCoverage = 0;
  if (gp->name() == "DefaultGeometryProcessor") {
    gpType = 0;
    hasCoverage =
        static_cast<const DefaultGeometryProcessor*>(gp)->getAAType() == AAType::Coverage ? 1 : 0;
  } else if (gp->name() == "QuadPerEdgeAAGeometryProcessor") {
    auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
    // The precompiled quad vert declares aPosition alone and derives the texture coordinate from it
    // via CoordTransformMatrix_0, so reject quads carrying color/uvCoord/subset attributes. Per-
    // vertex AA coverage is now supported via the HAS_COVERAGE dimension, so it is no longer
    // rejected.
    if (!quadGP->hasCommonColor() || !quadGP->hasUVMatrix() || quadGP->getHasSubset()) {
      return std::nullopt;
    }
    gpType = 1;
    hasCoverage = quadGP->getAAType() == AAType::Coverage ? 1 : 0;
  } else {
    return std::nullopt;
  }
  using VD = TexturedEffectShader::VD;
  std::vector<int> vertValues(VD::COUNT, 0);
  vertValues[VD::GP_TYPE] = gpType;
  vertValues[VD::HAS_COVERAGE] = hasCoverage;
  auto vertIndex = VD::domain().encode(vertValues);
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  // Accept two structurally-equivalent forms of "texture followed by one pointwise transform":
  //   (a) a single ComposeFragmentProcessor(TextureEffect, X), or
  //   (b) two sequential top-level color processors [TextureEffect, X].
  // Sequential color processors are chained (each consumes the previous output), so [A, B] produces
  // the identical result to Compose(A, B) and maps to the same precompiled variant.
  const FragmentProcessor* child0 = nullptr;
  const FragmentProcessor* child1 = nullptr;
  if (programInfo->numFragmentProcessors() == 1 && programInfo->numColorFragmentProcessors() == 1) {
    auto fp = programInfo->getFragmentProcessor(0);
    if (fp->name() != "ComposeFragmentProcessor" || fp->numChildProcessors() != 2) {
      return std::nullopt;
    }
    child0 = fp->childProcessor(0);
    child1 = fp->childProcessor(1);
  } else if (programInfo->numFragmentProcessors() == 2 &&
             programInfo->numColorFragmentProcessors() == 2) {
    child0 = programInfo->getFragmentProcessor(0);
    child1 = programInfo->getFragmentProcessor(1);
  } else {
    return std::nullopt;
  }
  if (child0->name() != "TextureEffect") {
    return std::nullopt;
  }
  auto* te = static_cast<const TextureEffect*>(child0);
  if (te->isYUV() || te->isAlphaOnly() || te->hasRGBAAA()) {
    return std::nullopt;
  }
  int hasSubset = te->hasSubset() ? 1 : 0;

  // All four pointwise operators (ColorMatrix / Luma / AlphaThreshold / ColorSpaceXform) share the
  // same structural class (1 sampler, no CoverageFP, RGBA) and thus the same precompiled shader:
  // TexturedEffectShader. The operator is selected at draw time by the OpType runtime uniform (set
  // by the pointwise FP's onSetData), so it is not a compile-time dimension. The frag index only
  // encodes the structural axes HAS_SUBSET and HAS_XP.
  using D = TexturedEffectShader::D;
  auto fragDomain = D::domain();
  std::vector<int> fragValues(D::COUNT, 0);
  fragValues[D::HAS_SUBSET] = hasSubset;
  fragValues[D::HAS_XP] = xpType;
  fragValues[D::HAS_COVERAGE] = hasCoverage;
  auto fragIndex = fragDomain.encode(fragValues);

  if (!IsSupportedPointwiseOp(child1)) {
    return std::nullopt;
  }
  return PermutationMatchResult{"TexturedEffectShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchGaussianBlur1D(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  int gpType = GetGPType(gp);
  if (gpType < 0) {
    return std::nullopt;
  }
  if (programInfo->numColorFragmentProcessors() != 1) {
    return std::nullopt;
  }
  auto coverageType = SharedCoverageValue(programInfo);
  if (!coverageType) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "GaussianBlur1DFragmentProcessor") {
    return std::nullopt;
  }
  auto* blur = static_cast<const GaussianBlur1DFragmentProcessor*>(fp);
  if (blur->numChildProcessors() != 1) {
    return std::nullopt;
  }
  auto childFP = blur->childProcessor(0);
  if (childFP == nullptr) {
    return std::nullopt;
  }
  // The blur child is either a plain TextureEffect (inlined texture() call) or a TiledTextureEffect,
  // in which case the shared tiled_sample.inc tiling is applied per tap (HAS_TILED_CHILD). Tiled
  // children are limited to the ShaderMode values tiled_sample.inc implements; others fall back.
  bool childHasSubset = false;
  bool tiledChild = false;
  if (childFP->name() == "TextureEffect") {
    auto* childTE = static_cast<const TextureEffect*>(childFP);
    if (childTE->numTextureSamplers() == 0) {
      return std::nullopt;
    }
    childHasSubset = childTE->hasSubset();
  } else if (childFP->name() == "TiledTextureEffect") {
    auto* childTiled = static_cast<const TiledTextureEffect*>(childFP);
    if (childTiled->numTextureSamplers() == 0 || childTiled->hasPerspective()) {
      return std::nullopt;
    }
    int modeX = 0;
    int modeY = 0;
    childTiled->getShaderModes(&modeX, &modeY);
    if (!TiledModeSupported(modeX) || !TiledModeSupported(modeY)) {
      return std::nullopt;
    }
    tiledChild = true;
  } else {
    return std::nullopt;
  }
  // Sigma is now a runtime uniform, not a variant dimension. The precompiled shader uses a fixed
  // maximum kernel (MAX_SIGMA=9 → radius up to 10) and breaks early at the runtime radius, so any
  // sigma in [1,10] maps to the same variant. Sigma > 10 exceeds the fixed kernel and falls back.
  int sigma = blur->getMaxSigma();
  if (sigma < 1 || sigma > 10) {
    return std::nullopt;
  }

  using VD = GaussianBlur1DShader::VD;
  auto vertDomain = VD::domain();
  std::vector<int> vertValues(VD::COUNT);
  vertValues[VD::GP_TYPE] = gpType;
  auto vertIndex = vertDomain.encode(vertValues);

  using FD = GaussianBlur1DShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  fragValues[FD::HAS_XP] = xpType;
  fragValues[FD::HAS_CHILD_SUBSET] = childHasSubset ? 1 : 0;
  fragValues[FD::HAS_COVERAGE] = *coverageType;
  fragValues[FD::HAS_TILED_CHILD] = tiledChild ? 1 : 0;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"GaussianBlur1DShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchBlendMerge(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  int gpType = GetGPType(gp);
  if (gpType < 0) {
    return std::nullopt;
  }
  // When gpType==1 (QuadPerEdgeAAGP), the coord transform source depends on hasUVMatrix():
  //   hasUVMatrix()=true  -> uvCoord attribute is empty, uses aPosition as coord source
  //   hasUVMatrix()=false -> uvCoord attribute exists, uses uvCoord as coord source
  // The precompiled vert supports both cases via the HAS_UV_COORD dimension.
  auto numFP = programInfo->numFragmentProcessors();
  if (numFP < 1 || numFP > 2) {
    return std::nullopt;
  }
  int hasMaskTexture = 0;
  if (numFP == 2) {
    if (programInfo->numColorFragmentProcessors() != 1) {
      return std::nullopt;
    }
    auto coverageFP = programInfo->getFragmentProcessor(1);
    auto coverageName = coverageFP->name();
    if (coverageName == "AARectEffect") {
      // Handled via HasClip runtime uniform in the shader.
    } else if (coverageName == "DeviceSpaceTextureEffect") {
      auto* dste = static_cast<const DeviceSpaceTextureEffect*>(coverageFP);
      if (!dste->isAlphaOnly()) {
        return std::nullopt;
      }
      hasMaskTexture = 1;
    } else if (coverageName == "ComposeFragmentProcessor") {
      if (coverageFP->numChildProcessors() != 2) {
        return std::nullopt;
      }
      auto child0 = coverageFP->childProcessor(0);
      auto child1 = coverageFP->childProcessor(1);
      if (child0->name() != "DeviceSpaceTextureEffect" || child1->name() != "AARectEffect") {
        return std::nullopt;
      }
      auto* dste = static_cast<const DeviceSpaceTextureEffect*>(child0);
      if (!dste->isAlphaOnly()) {
        return std::nullopt;
      }
      hasMaskTexture = 1;
    } else {
      return std::nullopt;
    }
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  auto fpName = fp->name();
  int childType = -1;
  if (fpName == "XfermodeFragmentProcessor - dst") {
    childType = 0;
  } else if (fpName == "XfermodeFragmentProcessor - src") {
    childType = 1;
  } else if (fpName == "XfermodeFragmentProcessor - two") {
    childType = 2;
  } else {
    return std::nullopt;
  }
  auto* xfp = static_cast<const XfermodeFragmentProcessor*>(fp);
  int blendMode = static_cast<int>(xfp->getMode());
  if (blendMode < 0 || blendMode >= 30) {
    return std::nullopt;
  }

  // Determine child[0] mode:
  //   0 = TextureEffect (standard texture sampling, no YUV)
  //   1 = ConstColorProcessor (uniform color, no texture)
  //   2 = TiledTextureEffect (tiling via runtime uniforms TileModeX/TileModeY)
  int child0Mode = 0;

  // Validate each child processor. We support:
  //   - TextureEffect (plain, no YUV; subset is supported via the Child{N}Subset uniforms)
  //   - ConstColorProcessor: uniform color output, only supported as child[0]
  // TiledTextureEffect children are NOT supported: the precompiled shader has no populated tiling
  // uniforms, so such draws fall back to ProgramBuilder for a correct result.
  for (size_t i = 0; i < xfp->numChildProcessors(); i++) {
    auto child = xfp->childProcessor(i);
    if (child == nullptr) {
      continue;
    }
    if (child->name() == "ConstColorProcessor") {
      // ConstColorProcessor is only supported as child[0]. If it appears as child[1] in TwoChild
      // mode, we cannot handle it (child[1] always needs a texture sampler in our current layout).
      if (i != 0) {
        return std::nullopt;
      }
      child0Mode = 1;
      continue;
    }
    if (child->name() != "TextureEffect") {
      return std::nullopt;
    }
    auto* childTE = static_cast<const TextureEffect*>(child);
    // YUV children need the multi-plane sampling path, which the precompiled blend shader does not
    // implement, so we still fall back for those.
    if (childTE->numTextureSamplers() == 0 || childTE->isYUV()) {
      return std::nullopt;
    }
    // Subset clamping is unconditional in the shader: the Child{N}Subset uniform is always present
    // for a plain TextureEffect child and is populated from computeSubsetRect (full [0,1] bounds
    // when the source has no real subset, making the clamp a no-op). No permutation dimension is
    // needed to gate it.
  }

  int hasCoverage = 0;
  int hasColor = 0;
  int hasUVCoord = 0;
  if (gpType == 1) {
    auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
    hasCoverage = quadGP->getAAType() == AAType::Coverage ? 1 : 0;
    hasColor = !quadGP->hasCommonColor() ? 1 : 0;
    hasUVCoord = !quadGP->hasUVMatrix() ? 1 : 0;
  }

  using VD = BlendMergeShader::VD;
  auto vertDomain = VD::domain();
  std::vector<int> vertValues(VD::COUNT, 0);
  vertValues[VD::GP_TYPE] = gpType;
  vertValues[VD::HAS_COVERAGE] = hasCoverage;
  vertValues[VD::HAS_UV_COORD] = hasUVCoord;
  vertValues[VD::HAS_COLOR] = hasColor;
  auto vertIndex = vertDomain.encode(vertValues);

  using FD = BlendMergeShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT, 0);
  fragValues[FD::CHILD_TYPE] = childType;
  fragValues[FD::HAS_XP] = xpType;
  fragValues[FD::CHILD0_MODE] = child0Mode;
  fragValues[FD::HAS_COVERAGE] = hasCoverage;
  fragValues[FD::HAS_COLOR] = hasColor;
  fragValues[FD::HAS_MASK_TEXTURE] = hasMaskTexture;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"BlendMergeShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchHairlineLine(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "HairlineLineGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto* hlgp = static_cast<const HairlineLineGeometryProcessor*>(gp);
  using D = HairlineLineShader::Dims;
  auto domain = D::domain();
  std::vector<int> values(D::COUNT);
  values[D::HAS_AA] = hlgp->getAAType() == AAType::Coverage ? 1 : 0;
  auto vertIndex = domain.encode(values);
  using FD = HairlineLineShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  for (size_t i = 0; i < D::COUNT; ++i) {
    fragValues[i] = values[i];
  }
  fragValues[FD::HAS_XP] = xpType;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"HairlineLineShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchHairlineQuad(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "HairlineQuadGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto* hqgp = static_cast<const HairlineQuadGeometryProcessor*>(gp);
  using D = HairlineQuadShader::Dims;
  auto domain = D::domain();
  std::vector<int> values(D::COUNT);
  values[D::HAS_AA] = hqgp->getAAType() == AAType::Coverage ? 1 : 0;
  auto vertIndex = domain.encode(values);
  using FD = HairlineQuadShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  for (size_t i = 0; i < D::COUNT; ++i) {
    fragValues[i] = values[i];
  }
  fragValues[FD::HAS_XP] = xpType;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"HairlineQuadShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchEllipseFill(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "EllipseGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numColorFragmentProcessors() != 0) {
    return std::nullopt;
  }
  // A clip/mask coverage FP is served through the shared HAS_COVERAGE dimension (device-space
  // sampling in coverage_output.inc). LocalMask needs per-vertex UV the ellipse vertex shader does
  // not provide, so SharedCoverageValue rejects it.
  auto coverageType = SharedCoverageValue(programInfo);
  if (!coverageType) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto* egp = static_cast<const EllipseGeometryProcessor*>(gp);
  using D = EllipseFillShader::Dims;
  auto domain = D::domain();
  std::vector<int> values(D::COUNT);
  values[D::STROKE] = egp->isStroke() ? 1 : 0;
  values[D::HAS_COMMON_COLOR] = egp->hasCommonColor() ? 1 : 0;
  auto vertIndex = domain.encode(values);
  using FD = EllipseFillShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  for (size_t i = 0; i < D::COUNT; ++i) {
    fragValues[i] = values[i];
  }
  fragValues[FD::HAS_XP] = xpType;
  fragValues[FD::HAS_COVERAGE] = *coverageType;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"EllipseFillShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchComplexEllipseFill(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "ComplexEllipseGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto* cegp = static_cast<const ComplexEllipseGeometryProcessor*>(gp);
  using D = ComplexEllipseFillShader::Dims;
  auto domain = D::domain();
  std::vector<int> values(D::COUNT);
  values[D::STROKE] = cegp->isStroke() ? 1 : 0;
  values[D::HAS_COMMON_COLOR] = cegp->hasCommonColor() ? 1 : 0;
  auto vertIndex = domain.encode(values);
  using FD = ComplexEllipseFillShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  for (size_t i = 0; i < D::COUNT; ++i) {
    fragValues[i] = values[i];
  }
  fragValues[FD::HAS_XP] = xpType;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"ComplexEllipseFillShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchNonAARRectFill(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "NonAARRectGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto* ngp = static_cast<const NonAARRectGeometryProcessor*>(gp);
  using D = NonAARRectFillShader::Dims;
  auto domain = D::domain();
  std::vector<int> values(D::COUNT);
  values[D::HAS_COMMON_COLOR] = ngp->hasCommonColor() ? 1 : 0;
  values[D::STROKE] = ngp->isStroke() ? 1 : 0;
  auto vertIndex = domain.encode(values);
  using FD = NonAARRectFillShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  for (size_t i = 0; i < D::COUNT; ++i) {
    fragValues[i] = values[i];
  }
  fragValues[FD::HAS_XP] = xpType;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"NonAARRectFillShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchComplexNonAARRectFill(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "ComplexNonAARRectGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto* cngp = static_cast<const ComplexNonAARRectGeometryProcessor*>(gp);
  using D = ComplexNonAARRectFillShader::Dims;
  auto domain = D::domain();
  std::vector<int> values(D::COUNT);
  values[D::HAS_COMMON_COLOR] = cngp->hasCommonColor() ? 1 : 0;
  values[D::STROKE] = cngp->isStroke() ? 1 : 0;
  auto vertIndex = domain.encode(values);
  using FD = ComplexNonAARRectFillShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  for (size_t i = 0; i < D::COUNT; ++i) {
    fragValues[i] = values[i];
  }
  fragValues[FD::HAS_XP] = xpType;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"ComplexNonAARRectFillShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchRoundStrokeRectFill(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "RoundStrokeRectGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto* rsgp = static_cast<const RoundStrokeRectGeometryProcessor*>(gp);
  // Skip perspective UV transforms — not covered by precompiled variants.
  if (rsgp->isUVPerspective()) {
    return std::nullopt;
  }
  using D = RoundStrokeRectFillShader::Dims;
  auto domain = D::domain();
  std::vector<int> values(D::COUNT);
  values[D::HAS_AA] = rsgp->getAAType() == AAType::Coverage ? 1 : 0;
  values[D::HAS_COMMON_COLOR] = rsgp->hasCommonColor() ? 1 : 0;
  values[D::HAS_UV_MATRIX] = rsgp->hasUVMatrixTransform() ? 1 : 0;
  auto vertIndex = domain.encode(values);
  using FD = RoundStrokeRectFillShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  for (size_t i = 0; i < D::COUNT; ++i) {
    fragValues[i] = values[i];
  }
  fragValues[FD::HAS_XP] = xpType;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"RoundStrokeRectFillShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchShapeInstancedFill(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "ShapeInstancedGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto* sigp = static_cast<const ShapeInstancedGeometryProcessor*>(gp);
  using D = ShapeInstancedFillShader::Dims;
  auto domain = D::domain();
  std::vector<int> values(D::COUNT);
  values[D::HAS_COLOR] = sigp->getHasColors() ? 1 : 0;
  values[D::HAS_AA] = sigp->getAAType() == AAType::Coverage ? 1 : 0;
  auto vertIndex = domain.encode(values);
  using FD = ShapeInstancedFillShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  for (size_t i = 0; i < D::COUNT; ++i) {
    fragValues[i] = values[i];
  }
  fragValues[FD::HAS_XP] = xpType;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"ShapeInstancedFillShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchMeshFill(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "MeshGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto* mgp = static_cast<const MeshGeometryProcessor*>(gp);
  using D = MeshFillShader::Dims;
  auto domain = D::domain();
  std::vector<int> values(D::COUNT);
  values[D::HAS_TEX_COORDS] = mgp->getHasTexCoords() ? 1 : 0;
  values[D::HAS_COLOR] = mgp->getHasColors() ? 1 : 0;
  values[D::HAS_COVERAGE] = mgp->getHasCoverage() ? 1 : 0;
  auto vertIndex = domain.encode(values);
  using FD = MeshFillShader::FD;
  auto fragDomain = FD::domain();
  std::vector<int> fragValues(FD::COUNT);
  for (size_t i = 0; i < D::COUNT; ++i) {
    fragValues[i] = values[i];
  }
  fragValues[FD::HAS_XP] = xpType;
  auto fragIndex = fragDomain.encode(fragValues);
  return PermutationMatchResult{"MeshFillShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchPerlin(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  int gpType = 0;
  int hasCoverage = 0;
  if (gp->name() == "DefaultGeometryProcessor") {
    gpType = 0;
    hasCoverage =
        static_cast<const DefaultGeometryProcessor*>(gp)->getAAType() == AAType::Coverage ? 1 : 0;
  } else if (gp->name() == "QuadPerEdgeAAGeometryProcessor") {
    auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
    // The precompiled vert derives the noise coordinate from aPosition via CoordTransformMatrix_0,
    // so reject quads carrying color/uvCoord/subset attributes (same constraint as the textured
    // matchers). Per-vertex AA coverage is supported via HAS_COVERAGE.
    if (!quadGP->hasCommonColor() || !quadGP->hasUVMatrix() || quadGP->getHasSubset()) {
      return std::nullopt;
    }
    gpType = 1;
    hasCoverage = quadGP->getAAType() == AAType::Coverage ? 1 : 0;
  } else {
    return std::nullopt;
  }
  // Accept a PerlinNoiseFragmentProcessor optionally followed by one pointwise operator, in the
  // same two structurally-equivalent forms as TryMatchComposedTexture:
  //   (a) a bare PerlinNoiseFragmentProcessor (OpType == OP_NONE, set by the Perlin FP), or
  //   (b) Compose(Perlin, op) / sequential [Perlin, op] where op is a supported pointwise operator.
  // The operator is folded into the shared OpType runtime uniform, so the composed case hits the
  // same PerlinNoiseFillShader variant losslessly (no materialization, no extra compile-time axis).
  const FragmentProcessor* perlin = nullptr;
  if (programInfo->numFragmentProcessors() == 1 && programInfo->numColorFragmentProcessors() == 1) {
    auto fp = programInfo->getFragmentProcessor(0);
    if (fp->name() == "PerlinNoiseFragmentProcessor") {
      perlin = fp;
    } else if (fp->name() == "ComposeFragmentProcessor" && fp->numChildProcessors() == 2 &&
               fp->childProcessor(0)->name() == "PerlinNoiseFragmentProcessor" &&
               IsSupportedPointwiseOp(fp->childProcessor(1))) {
      perlin = fp->childProcessor(0);
    }
  } else if (programInfo->numFragmentProcessors() == 2 &&
             programInfo->numColorFragmentProcessors() == 2 &&
             programInfo->getFragmentProcessor(0)->name() == "PerlinNoiseFragmentProcessor" &&
             IsSupportedPointwiseOp(programInfo->getFragmentProcessor(1))) {
    perlin = programInfo->getFragmentProcessor(0);
  }
  if (perlin == nullptr) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  using VD = PerlinNoiseFillShader::VD;
  std::vector<int> vertValues(VD::COUNT, 0);
  vertValues[VD::GP_TYPE] = gpType;
  vertValues[VD::HAS_COVERAGE] = hasCoverage;
  auto vertIndex = VD::domain().encode(vertValues);
  using D = PerlinNoiseFillShader::D;
  std::vector<int> fragValues(D::COUNT, 0);
  fragValues[D::HAS_XP] = xpType;
  fragValues[D::HAS_COVERAGE] = hasCoverage;
  auto fragIndex = D::domain().encode(fragValues);
  return PermutationMatchResult{"PerlinNoiseFillShader", vertIndex, fragIndex};
}

static bool IsOpenGLStage1Pointwise(const FragmentProcessor* processor) {
  if (processor == nullptr) {
    return false;
  }
  if (processor->name() == "LumaFragmentProcessor") {
    return true;
  }
  return processor->name() == "ColorMatrixFragmentProcessor" &&
         static_cast<const ColorMatrixFragmentProcessor*>(processor)->isChannelPermutation();
}

static bool IsOpenGLStage1Candidate(const ProgramInfo* programInfo) {
  if (!programInfo->usesOpenGLDesktopAOTProfile() || !programInfo->samplersAre2D() ||
      GetXPType(programInfo) != 0 ||
      programInfo->numFragmentProcessors() != programInfo->numColorFragmentProcessors()) {
    return false;
  }
  auto gp = programInfo->getGeometryProcessor();
  auto colorCount = programInfo->numColorFragmentProcessors();
  if (colorCount == 1 &&
      programInfo->getFragmentProcessor(0)->name() == "AOTPointwiseTailProcessor") {
    int gpType = GetGPType(gp);
    if (gpType < 0) {
      return false;
    }
    if (gpType == 1) {
      auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
      return quadGP->hasCommonColor() && quadGP->hasUVMatrix() && !quadGP->getHasSubset();
    }
    return true;
  }
  if (gp->name() != "QuadPerEdgeAAGeometryProcessor") {
    return false;
  }
  auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
  if (!quadGP->hasCommonColor() || !quadGP->hasUVMatrix() || quadGP->getHasSubset()) {
    return false;
  }
  if (colorCount == 1) {
    auto processor = programInfo->getFragmentProcessor(0);
    if (processor->name() != "ComposeFragmentProcessor" || processor->numChildProcessors() != 2) {
      return false;
    }
    auto source = processor->childProcessor(0);
    auto pointwise = processor->childProcessor(1);
    if (source->name() == "DeviceSpaceTextureEffect") {
      return IsOpenGLStage1Pointwise(pointwise);
    }
    return source->name() == "TextureEffect" &&
           pointwise->name() == "ColorMatrixFragmentProcessor" &&
           IsOpenGLStage1Pointwise(pointwise);
  }
  auto pointwise = colorCount == 2 ? programInfo->getFragmentProcessor(1) : nullptr;
  return pointwise != nullptr && programInfo->getFragmentProcessor(0)->name() == "TextureEffect" &&
         pointwise->name() == "ColorMatrixFragmentProcessor" && IsOpenGLStage1Pointwise(pointwise);
}

static std::optional<PermutationMatchResult> MatchPermutationImpl(const ProgramInfo* programInfo) {
  // The first OpenGL AOT profile is intentionally limited to the exact texture and pointwise
  // structures covered by pixel cross-validation. Other shader families stay on ProgramBuilder
  // until their OpenGL variants are validated independently.
  if (programInfo->backend() == Backend::OpenGL && !IsOpenGLStage1Candidate(programInfo)) {
    return std::nullopt;
  }

  // Only RGBA and alpha-only (AAAA, Swizzle::ForWrite(ALPHA_8)) render targets are supported. Any
  // other write swizzle would need a mapping the precompiled shaders do not implement.
  auto outputSwizzle = programInfo->getOutputSwizzle();
  if (outputSwizzle != Swizzle::RGBA() && outputSwizzle != Swizzle::AAAA()) {
    return std::nullopt;
  }
  if (outputSwizzle == Swizzle::AAAA()) {
    // Alpha-only targets: only the fill kernels that apply the write swizzle at output (via the
    // OutputAlphaSwizzle uniform) may serve the draw. Other kernels assume RGBA output.
    if (auto result = TryMatchSolidColorFill(programInfo)) {
      return result;
    }
    if (auto result = TryMatchMaskFill(programInfo)) {
      return result;
    }
    if (auto result = TryMatchQuadTextureFill(programInfo)) {
      return result;
    }
    return std::nullopt;
  }
  if (auto result = TryMatchTextureFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchQuadColorFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchQuadTextureFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchTiledTextureFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchSolidColorFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchMaskFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchConstColor(programInfo)) {
    return result;
  }
  if (auto result = TryMatchPointwiseTail(programInfo)) {
    return result;
  }
  if (auto result = TryMatchDeviceSpaceTexturedEffect(programInfo)) {
    return result;
  }
  if (auto result = TryMatchDeviceSpaceTexture(programInfo)) {
    return result;
  }
  if (auto result = TryMatchGradientFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchSingleIntervalGradient(programInfo)) {
    return result;
  }
  if (auto result = TryMatchDualIntervalGradient(programInfo)) {
    return result;
  }
  if (auto result = TryMatchTextureGradient(programInfo)) {
    return result;
  }
  if (auto result = TryMatchTextureColorMatrix(programInfo)) {
    return result;
  }
  if (auto result = TryMatchComposedTexture(programInfo)) {
    return result;
  }

  if (auto result = TryMatchPerlin(programInfo)) {
    return result;
  }

  if (auto result = TryMatchAtlasTextFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchPointwiseDirect(programInfo)) {
    return result;
  }
  if (auto result = TryMatchGaussianBlur1D(programInfo)) {
    return result;
  }
  if (auto result = TryMatchBlendMerge(programInfo)) {
    return result;
  }
  if (auto result = TryMatchHairlineLine(programInfo)) {
    return result;
  }
  if (auto result = TryMatchHairlineQuad(programInfo)) {
    return result;
  }
  if (auto result = TryMatchEllipseFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchComplexEllipseFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchNonAARRectFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchComplexNonAARRectFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchRoundStrokeRectFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchShapeInstancedFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchMeshFill(programInfo)) {
    return result;
  }
  return std::nullopt;
}

std::optional<PermutationMatchResult> MatchPermutation(const ProgramInfo* programInfo,
                                                       PermutationMatchFailure* failure) {
  auto outputSwizzle = programInfo->getOutputSwizzle();
  if (outputSwizzle != Swizzle::RGBA() && outputSwizzle != Swizzle::AAAA()) {
    if (failure != nullptr) {
      *failure = PermutationMatchFailure::UnsupportedOutputSwizzle;
    }
    return std::nullopt;
  }
  auto result = MatchPermutationImpl(programInfo);
  if (result) {
    auto info = ShaderRegistry::Find(result->shaderName);
    if (info == nullptr || !IsBuildablePermutation(*info, result->vertPermutationIndex,
                                                   result->fragPermutationIndex)) {
      result.reset();
    }
  }
  if (failure != nullptr) {
    *failure = result ? PermutationMatchFailure::None : PermutationMatchFailure::NoMatchingRule;
  }
  return result;
}

}  // namespace tgfx

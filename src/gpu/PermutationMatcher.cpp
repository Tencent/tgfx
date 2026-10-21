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
#include "ColorSpaceXformHelper.h"
#include "gpu/Swizzle.h"
#include "gpu/processors/AOTPointwiseChainProcessor.h"
#include "gpu/processors/AOTYUVChainProcessor.h"
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
#include "gpu/processors/RectEffect.h"
#include "gpu/processors/RoundStrokeRectGeometryProcessor.h"
#include "gpu/processors/ShapeInstancedGeometryProcessor.h"
#include "gpu/processors/SingleIntervalGradientColorizer.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/processors/XfermodeFragmentProcessor.h"
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
#include "gpu/shaders/level1/PointwiseGraphShader.h"
#include "gpu/shaders/level1/QuadColorFillShader.h"
#include "gpu/shaders/level1/QuadTextureFillShader.h"
#include "gpu/shaders/level1/RoundStrokeRectFillShader.h"
#include "gpu/shaders/level1/ShapeInstancedFillShader.h"
#include "gpu/shaders/level1/ShapeInstancedTextureCoverageShader.h"
#include "gpu/shaders/level1/SolidColorFillShader.h"
#include "gpu/shaders/level1/TexturedEffectShader.h"
#include "gpu/shaders/level1/TiledTextureFillShader.h"
#include "gpu/shaders/level1/YUVTextureFillShader.h"
#include "layers/processors/GlassRefractionFragmentProcessor.h"
#include "layers/processors/GlassShapeGeometryFragmentProcessor.h"
#include "layers/processors/GlassUDFTentBlurFragmentProcessor.h"

namespace tgfx {

// Sub-reason of the most recent rejection on this thread. The TryMatch* helpers only ever return
// nullopt, so the first rejecting site records why here; MatchPermutation copies it into the
// caller's out-parameter and clears it. Diagnostic only: no control flow depends on it.
static thread_local PermutationMatchFailure gPendingMatchFailure = PermutationMatchFailure::None;

static std::optional<PermutationMatchResult> Fail(PermutationMatchFailure reason) {
  if (gPendingMatchFailure == PermutationMatchFailure::None) {
    gPendingMatchFailure = reason;
  }
  return std::nullopt;
}

// Returns 0 for EmptyXferProcessor, 1 for PorterDuffXferProcessor with dst texture (DST_TEX mode),
// 2 for PorterDuffXferProcessor with framebuffer fetch, or -1 for unsupported XP types.
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

// A RectEffect serves as chain/AARect coverage in its AA form; both spaces are accepted because
// the runtime clip contract carries a DeviceToLocal matrix (identity for the device-space form).
// NonAA rects keep the hard-edge step math out of the contract and stay on the fallback route.
static bool IsFoldableAARectEffect(const FragmentProcessor* fp) {
  if (fp == nullptr || fp->name() != "RectEffect" || fp->numChildProcessors() != 0) {
    return false;
  }
  auto rectEffect = static_cast<const RectEffect*>(fp);
  return rectEffect->isAntiAlias();
}

enum class CoverageKind {
  None,
  AARect,
  RRect,
  DeviceMask,
  LocalMask,
};

// Classifies coverage semantics independently of any shader's permutation layout. LocalMask is a
// distinct capability: it needs a local-space varying and sampler and therefore cannot be encoded
// in the shared three-value HAS_COVERAGE dimension.
static std::optional<CoverageKind> ClassifyCoverage(const FragmentProcessor* coverageFP) {
  if (coverageFP == nullptr) {
    return std::nullopt;
  }
  auto coverageName = coverageFP->name();
  if (coverageName == "RectEffect" && IsFoldableAARectEffect(coverageFP)) {
    return CoverageKind::AARect;
  }
  if (coverageName == "RRectEffect" && coverageFP->numChildProcessors() == 0) {
    // Every rrect parameter rides the runtime clip contract (LocalRect/RadiiX/RadiiY/AntiAlias/
    // DeviceToLocal), so any leaf RRectEffect classifies without extra conditions; perspective
    // and degenerate matrices were already rejected at construction.
    return CoverageKind::RRect;
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
  if (coverageName == "DeviceSpaceTextureEffect") {
    auto* dste = static_cast<const DeviceSpaceTextureEffect*>(coverageFP);
    if (!dste->isAlphaOnly()) {
      return std::nullopt;
    }
    return CoverageKind::DeviceMask;
  }
  return std::nullopt;
}

// Classifies the coverage stage of a ProgramInfo carrying at most one coverage FP.
static std::optional<CoverageKind> ClassifyCoverageFP(const ProgramInfo* programInfo) {
  auto numFP = programInfo->numFragmentProcessors();
  auto numColorFP = programInfo->numColorFragmentProcessors();
  if (numFP == numColorFP) {
    return CoverageKind::None;
  }
  if (numFP != numColorFP + 1) {
    return std::nullopt;
  }
  return ClassifyCoverage(programInfo->getFragmentProcessor(numColorFP));
}

// Direct analytic clip coverage: the sole coverage FP is an AA rect or a leaf rrect.
static bool HasDirectAnalyticClipCoverage(const ProgramInfo* programInfo) {
  auto coverage = ClassifyCoverageFP(programInfo);
  return coverage == CoverageKind::AARect || coverage == CoverageKind::RRect;
}

// Kernels whose color comes from the GP Color uniform: a color FP would be silently dropped, so
// the draw may carry no FP at all or only a direct analytic clip evaluated from the runtime clip
// contract uniforms.
static bool HasNoProcessorsOrDirectClip(const ProgramInfo* programInfo) {
  return programInfo->numFragmentProcessors() == 0 ||
         (programInfo->numColorFragmentProcessors() == 0 &&
          HasDirectAnalyticClipCoverage(programInfo));
}

// Analytic clip coverage (AA rect or rrect) changes only fragment math, whereas a device-space
// mask adds a sampler and shifts the terminal XP binding. Kernels using the runtime clip contract
// therefore compile only this ABI distinction and carry the clip state through uniforms.
static std::optional<int> SharedDeviceMaskValue(const ProgramInfo* programInfo) {
  auto coverage = ClassifyCoverageFP(programInfo);
  if (!coverage || *coverage == CoverageKind::LocalMask) {
    return std::nullopt;
  }
  return *coverage == CoverageKind::DeviceMask ? 1 : 0;
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

// The position-only rect GP forms the texture and noise fill kernels share: a DefaultGP, or a
// QuadPerEdgeAA GP whose coordinate comes from aPosition (a common color, a uv matrix, no
// per-quad subset), since those precompiled vertex stages declare aPosition plus the optional AA
// coverage attribute only. *coverage receives that attribute's presence.
static bool IsPositionOnlyRectGP(const GeometryProcessor* gp, int* coverage) {
  int gpType = GetGPType(gp);
  if (gpType < 0) {
    return false;
  }
  if (gpType == 1) {
    auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
    if (!quadGP->hasCommonColor() || !quadGP->hasUVMatrix() || quadGP->getHasSubset()) {
      return false;
    }
  }
  if (coverage != nullptr) {
    *coverage = GetGPCoverage(gp);
  }
  return true;
}

// ShaderMode values the shared single-tap tiled_sample.inc handles for the blur tap loop:
// None(0), Clamp(1), RepeatNearestNone(2), RepeatLinearNone(3), MirrorRepeat(6),
// ClampToBorderNearest(7), ClampToBorderLinear(8). Mode 3 shares mode 2's wrap-and-clamp
// coordinate math — the linear-vs-nearest difference is the hardware sampler filter, and each
// blur tap goes through the same tiledMapCoord path the fill kernel's pixel-verified mode-3
// handling uses. The mipmap-repeat modes 4/5 need a 4-tap seam blend per tap, which only the
// tiled fill kernel implements (see TiledFillModeSupported); blur keeps falling back for those.
static bool TiledModeSupported(int mode) {
  return mode == 0 || mode == 1 || mode == 2 || mode == 3 || mode == 6 || mode == 7 || mode == 8;
}

// The tiled fill kernel additionally handles the mipmap-repeat modes 4/5 via an inline 4-tap seam
// blend. So it accepts every mode.
static bool TiledFillModeSupported(int mode) {
  return TiledModeSupported(mode) || mode == 4 || mode == 5;
}

static std::optional<PermutationMatchResult> TryMatchTiledTextureFill(
    const ProgramInfo* programInfo) {
  int hasCoverage = 0;
  if (!IsPositionOnlyRectGP(programInfo->getGeometryProcessor(), &hasCoverage)) {
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
  // A both-None (0/0) mode means the hardware sampler already resolves the tiling (e.g. Repeat
  // with a full-boundary subset), so this kernel serves it like any other mode.
  // ALPHA_ONLY and HAS_STRICT are folded into runtime uniforms (AlphaOnly / Strict), set by
  // GLSLTiledTextureEffect::onSetData; they are no longer compile-time dimensions.
  TiledTextureFillInputs inputs;
  inputs.hasCoverage = hasCoverage != 0;
  inputs.xpType = xpType;
  auto composed = ComposeTiledTextureFill(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto vertIndex = TiledTextureFillShader::VD::domain().encode(composed->vertValues);
  auto fragIndex = TiledTextureFillShader::FragDims::domain().encode(composed->fragValues);
  return PermutationMatchResult{"TiledTextureFillShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchYUVTextureFill(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "QuadPerEdgeAAGeometryProcessor") {
    return std::nullopt;
  }
  auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
  // The precompiled vert has no texSubset attribute, so subset-carrying quads fall back.
  if (quadGP->getHasSubset()) {
    return std::nullopt;
  }
  if (programInfo->numColorFragmentProcessors() != 1 || programInfo->numFragmentProcessors() != 1) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  const TextureEffect* texture = nullptr;
  if (fp->name() == "AOTYUVChainProcessor") {
    // The decomposed color-grade route: the multi-plane source plus folded pointwise slots,
    // all carried by the same YUVTextureFillShader variant set (the operators are per-draw
    // uniform data, so no dimension changes).
    auto* chain = static_cast<const AOTYUVChainProcessor*>(fp);
    if (chain->source() == nullptr || chain->source()->name() != "TextureEffect") {
      return std::nullopt;
    }
    texture = static_cast<const TextureEffect*>(chain->source());
  } else if (fp->name() == "TextureEffect") {
    texture = static_cast<const TextureEffect*>(fp);
  } else {
    return std::nullopt;
  }
  if (!texture->isYUV()) {
    return std::nullopt;
  }
  // Only I420 and NV12 have precompiled plane layouts.
  YUVTextureFillInputs inputs;
  inputs.hasUVMatrix = quadGP->hasUVMatrix();
  if (texture->yuvFormat() == YUVFormat::I420) {
    inputs.yuvFormat = 0;
  } else if (texture->yuvFormat() == YUVFormat::NV12) {
    inputs.yuvFormat = 1;
  }
  inputs.xpType = GetXPType(programInfo);
  auto composed = ComposeYUVTextureFill(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto vertIndex = YUVTextureFillShader::VD::domain().encode(composed->vertValues);
  auto fragIndex = YUVTextureFillShader::FD::domain().encode(composed->fragValues);
  return PermutationMatchResult{"YUVTextureFillShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchSolidColorFill(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "DefaultGeometryProcessor") {
    return std::nullopt;
  }
  // The fill color comes from the DefaultGeometryProcessor Color uniform, so a color fragment
  // processor would be silently dropped and must reject. The only accepted FP is one direct
  // analytic clip coverage FP (AA rect or rrect), which this shader evaluates from the runtime
  // clip contract uniforms.
  if (!HasNoProcessorsOrDirectClip(programInfo)) {
    return std::nullopt;
  }
  auto* dgp = static_cast<const DefaultGeometryProcessor*>(gp);
  SolidColorFillInputs inputs;
  inputs.isCoverageAA = dgp->getAAType() == AAType::Coverage;
  inputs.xpType = GetXPType(programInfo);
  auto composed = ComposeSolidColorFill(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto vertIndex = SolidColorFillShader::VD::domain().encode(composed->vertValues);
  auto fragIndex = SolidColorFillShader::FD::domain().encode(composed->fragValues);
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
  MaskFillInputs inputs;
  inputs.xpType = GetXPType(programInfo);
  auto composed = ComposeMaskFill(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto fragIndex = MaskFillShader::D::domain().encode(composed->fragValues);
  return PermutationMatchResult{"MaskFillShader", 0, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchQuadColorFill(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "QuadPerEdgeAAGeometryProcessor") {
    return std::nullopt;
  }
  // Accept either zero fragment processors, a device-space mask, or one direct analytic clip
  // coverage FP (AA rect or rrect). The clip is evaluated by the runtime clip contract uniforms.
  QuadColorFillInputs inputs;
  if (programInfo->numFragmentProcessors() != 0) {
    if (programInfo->numColorFragmentProcessors() != 0) {
      return std::nullopt;
    }
    if (HasDirectAnalyticClipCoverage(programInfo)) {
      // No additional sampler or permutation dimension is needed.
    } else {
      auto coverage = ClassifyCoverageFP(programInfo);
      if (!coverage || *coverage != CoverageKind::DeviceMask) {
        return std::nullopt;
      }
      inputs.hasMaskTexture = true;
    }
  }
  inputs.xpType = GetXPType(programInfo);
  auto composed = ComposeQuadColorFill(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto fragIndex = QuadColorFillShader::FD::domain().encode(composed->fragValues);
  return PermutationMatchResult{"QuadColorFillShader", 0, fragIndex};
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
  bool hasLocalMask = false;
  if (programInfo->numFragmentProcessors() != 1) {
    if (HasDirectAnalyticClipCoverage(programInfo)) {
      // Evaluated through the runtime clip contract uniforms.
    } else {
      auto coverage = ClassifyCoverageFP(programInfo);
      if (coverage && *coverage == CoverageKind::DeviceMask) {
        // Device masks are a runtime uniform now, so there is nothing to encode.
      } else if (coverage && *coverage == CoverageKind::LocalMask) {
        hasLocalMask = true;
      } else {
        // Unrecognized coverage kinds (including unclassifiable ones) stay on other routes.
        return std::nullopt;
      }
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
  QuadTextureFillInputs inputs;
  inputs.hasUVCoord = !quadGP->hasUVMatrix();
  inputs.hasSubset = quadGP->getHasSubset();
  // The precompiled vTexSubset path transforms the subset corners WITHOUT a perspective divide
  // (quad_texture_fill.vert: perspLT = (CoordTransformMatrix_0 * vec3(src, 1)).xy), while the
  // runtime route's emitTransformedPoint divides by w when the transform is projective. A
  // projective source carrying a GP subset would clamp the texture coordinate to wrong bounds
  // (one edge-line class of A/B divergence), so it must stay on the runtime route — the same
  // conservative policy the plain-coordinate kernels apply to projective sources. Projective
  // draws without a GP subset are fine: TransformedCoords_0 is a vec3 varying that the fragment
  // shader divides.
  if (inputs.hasSubset && (quadGP->getHasUVPerspective() || te->hasPerspective())) {
    return std::nullopt;
  }
  inputs.hasLocalMask = hasLocalMask;
  inputs.xpType = xpType;
  auto composed = ComposeQuadTextureFill(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto vertIndex = QuadTextureFillShader::VD::domain().encode(composed->vertValues);
  auto fragIndex = QuadTextureFillShader::FD::domain().encode(composed->fragValues);
  return PermutationMatchResult{"QuadTextureFillShader", vertIndex, fragIndex};
}

// Matches the fused pointwise-DAG kernel. The chain processor carries the whole DAG in uniforms,
// so this rule only validates the parts that are baked into the program: the GP, the leaf count
// (sampler/varying layout), and that every leaf is a plain sampled texture.
// A quad's per-quad subset (SrcRectConstraint::Strict) reaches every strict texture of the
// runtime program as the GP's vTexSubset varying, which the GP transforms with the program's
// first coordinate transform. The HAS_SUBSET chain variant computes it with leaf 0's transform and
// clamps leaf 0 alone, so it serves the draw only when leaf 0 is the one strict texture: a plain
// sampled leaf (the tiled and device-space leaf forms clamp differently) with every other leaf and
// tiling recipe non-strict. A projective subset stays on the runtime route, as for
// QuadTextureFillShader: the precompiled vertex stage transforms the subset corners affinely.
static bool IsChainQuadSubsetServable(const QuadPerEdgeAAGeometryProcessor* quadGP,
                                      const AOTPointwiseChainProcessor* chain) {
  if (chain->leafCount() == 0 || chain->tiledLeaf() == 0 || chain->tiledLeaf2() == 0 ||
      (chain->deviceLeafMask() & 1u) != 0 || quadGP->getHasUVPerspective()) {
    return false;
  }
  for (const auto& tiled : chain->tiledLeafParameters()) {
    if (tiled.recipe.strict) {
      return false;
    }
  }
  for (size_t index = 0; index < chain->leafCount(); ++index) {
    auto leaf = chain->childProcessor(index);
    if (leaf->name() != "TextureEffect") {
      return false;
    }
    auto* texture = static_cast<const TextureEffect*>(leaf);
    bool strict = texture->getConstraint() == SrcRectConstraint::Strict;
    if (strict != (index == 0) || (index == 0 && texture->hasPerspective())) {
      return false;
    }
  }
  return true;
}

static std::optional<PermutationMatchResult> TryMatchPointwiseDAG(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  // GP profiles: 0 Default, 1 Quad, 2 Mesh (whose vertex buffers follow the
  // chain's attribute order), 3 the sampler-free AtlasText twin, 4 the ellipse layout, 5 the
  // hairline-line layout, 6 / 7 the complex rounded-rect fill / stroke layouts.
  int gpKind = -1;
  if (gp->name() == "DefaultGeometryProcessor") {
    gpKind = 0;
  } else if (gp->name() == "QuadPerEdgeAAGeometryProcessor") {
    gpKind = 1;
  } else if (gp->name() == "MeshGeometryProcessor") {
    gpKind = 2;
  } else if (gp->name() == "AtlasTextGeometryProcessor") {
    gpKind = 3;
  } else if (gp->name() == "EllipseGeometryProcessor") {
    gpKind = 4;
  } else if (gp->name() == "HairlineLineGeometryProcessor") {
    gpKind = 5;
  } else if (gp->name() == "ComplexEllipseGeometryProcessor") {
    gpKind = static_cast<const ComplexEllipseGeometryProcessor*>(gp)->isStroke() ? 7 : 6;
  }
  if (gpKind < 0 || programInfo->numColorFragmentProcessors() != 1 ||
      programInfo->numFragmentProcessors() != 1) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "AOTPointwiseChainProcessor") {
    return std::nullopt;
  }
  auto* chain = static_cast<const AOTPointwiseChainProcessor*>(fp);
  auto leafCount = chain->leafCount();
  // Sampler-only padding makes every non-empty chain present exactly four sampler children.
  if (leafCount != 0 && leafCount != 4) {
    return std::nullopt;
  }
  // Color-space transforms and gradients each share one chain-wide parameter block.
  int colorSpaceXformCount = 0;
  int gradientCount = 0;
  for (size_t index = 0; index < chain->slotCount(); ++index) {
    if (chain->slot(index).op == AOTChainOp::ColorSpaceXform) {
      ++colorSpaceXformCount;
    } else if (chain->slot(index).op == AOTChainOp::Gradient) {
      ++gradientCount;
    }
  }
  if (colorSpaceXformCount > 1 || gradientCount > 1) {
    return Fail(PermutationMatchFailure::GradientOrNoiseBudget);
  }
  for (size_t index = 0; index < leafCount; ++index) {
    auto leaf = chain->childProcessor(index);
    // A deferred (task-local) proxy reports zero samplers until it is instantiated; it always
    // binds exactly one when it is (a lazy proxy is internally rendered, hence never multi-plane
    // YUV), so only a multi-sampler leaf is rejected here.
    if (leaf->name() != "TextureEffect" || leaf->numTextureSamplers() > 1) {
      return std::nullopt;
    }
    auto* texture = static_cast<const TextureEffect*>(leaf);
    // A leaf subset is a runtime uniform (the kernel always clamps, full bounds when absent), an
    // alpha-only leaf splats .r through its slot selector bit, and an RGBAAA leaf reads its alpha
    // area through the per-leaf AlphaStart / HasRgbaaa uniforms, so none needs a variant.
    // Perspective transforms are served: leaf coordinates carry w and divide per-pixel.
    if (texture->isYUV()) {
      return std::nullopt;
    }
  }
  PointwiseChainInputs inputs;
  inputs.gpKind = gpKind;
  inputs.textureCount = leafCount == 0 ? 0 : 1;
  inputs.hasMask = chain->hasMask();
  inputs.hasCoverageSubtree = chain->coverageRoot() >= 0;
  inputs.hasNoise = chain->hasNoise();
  inputs.hasGradient = gradientCount != 0;
  inputs.resourceProfile = chain->resourceProfile();
  inputs.xpType = GetXPType(programInfo);
  if (gpKind == 1) {
    auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
    if (quadGP->getHasSubset()) {
      if (!IsChainQuadSubsetServable(quadGP, chain)) {
        return Fail(PermutationMatchFailure::QuadSubset);
      }
      inputs.hasSubset = true;
    }
    inputs.hasUVCoord = !quadGP->hasUVMatrix();
    inputs.hasColor = !quadGP->hasCommonColor();
    inputs.hasCoverage = true;
  } else if (gpKind == 2) {
    auto* meshGP = static_cast<const MeshGeometryProcessor*>(gp);
    // Mesh vertex buffers follow the chain's attribute order (position, coverage, texCoord,
    // color): texCoords ride the uvCoord slot and per-vertex colors the color slot. Mesh buffers
    // always carry the coverage slot (1.0 for vertex meshes).
    inputs.hasUVCoord = meshGP->getHasTexCoords();
    inputs.hasColor = meshGP->getHasColors();
    inputs.hasCoverage = true;
  } else if (gpKind == 3) {
    // Only the sampler-free chain-route twin is servable: a sampler-owning atlas GP would bind
    // its atlas ahead of the chain's leaf samplers.
    if (gp->numTextureSamplers() != 0) {
      return std::nullopt;
    }
    auto* atlasGP = static_cast<const AtlasTextGeometryProcessor*>(gp);
    // Atlas text buffers are (position, maskCoord, color?) with no coverage slot; maskCoord
    // rides the uvCoord slot.
    inputs.hasUVCoord = true;
    inputs.hasColor = !atlasGP->hasCommonColor();
    inputs.hasCoverage = false;
  } else if (gpKind == 4) {
    auto* ellipseGP = static_cast<const EllipseGeometryProcessor*>(gp);
    inputs.hasColor = !ellipseGP->hasCommonColor();
    inputs.hasCoverage = false;
  } else if (gpKind == 5) {
    // The hairline edge AA is per-pixel (edge-distance varying); the color is a uniform.
    inputs.hasCoverage = false;
  } else if (gpKind == 6 || gpKind == 7) {
    // Per-pixel edge AA like the ellipse; per-vertex colors ride the color attribute.
    auto* complexGP = static_cast<const ComplexEllipseGeometryProcessor*>(gp);
    inputs.hasColor = !complexGP->hasCommonColor();
    inputs.hasCoverage = false;
  } else {
    inputs.hasCoverage = GetGPCoverage(gp) != 0;
  }
  if (IsAOTGraphProfile(inputs.resourceProfile)) {
    auto composed = ComposePointwiseGraph(inputs);
    if (!composed) {
      return Fail(PermutationMatchFailure::PermutationNotEnumerated);
    }
    auto vertIndex = PointwiseGraphShader::VD::domain().encode(composed->vertValues);
    auto fragIndex = PointwiseGraphShader::FD::domain().encode(composed->fragValues);
    return PermutationMatchResult{"PointwiseGraphShader", vertIndex, fragIndex};
  }
  auto composed = ComposePointwiseChain(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto vertIndex = PointwiseChainShader::VD::domain().encode(composed->vertValues);
  auto fragIndex = PointwiseChainShader::FD::domain().encode(composed->fragValues);
  return PermutationMatchResult{"PointwiseChainShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchAtlasTextFill(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "AtlasTextGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numColorFragmentProcessors() != 0 || programInfo->numFragmentProcessors() > 1) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() == 1) {
    // A single coverage FP multiplies the atlas glyph coverage: either an alpha-only device-space
    // mask, or a direct analytic clip (AA rect / rrect) via the runtime clip contract.
    if (!HasDirectAnalyticClipCoverage(programInfo)) {
      auto fp = programInfo->getFragmentProcessor(0);
      if (fp->name() != "DeviceSpaceTextureEffect") {
        return std::nullopt;
      }
      auto* mask = static_cast<const DeviceSpaceTextureEffect*>(fp);
      if (!mask->isAlphaOnly() || mask->hasPerspective()) {
        return std::nullopt;
      }
    }
  }
  auto* atgp = static_cast<const AtlasTextGeometryProcessor*>(gp);
  AtlasTextFillInputs inputs;
  inputs.hasCoverage = atgp->getAAType() == AAType::Coverage;
  inputs.hasCommonColor = atgp->hasCommonColor();
  inputs.xpType = xpType;
  auto composed = ComposeAtlasTextFill(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto vertIndex = AtlasTextFillShader::D::domain().encode(composed->vertValues);
  auto fragIndex = AtlasTextFillShader::FD::domain().encode(composed->fragValues);
  return PermutationMatchResult{"AtlasTextFillShader", vertIndex, fragIndex};
}

// TFTypeToIndex lives in ColorSpaceXformHelper as the single mapping authority: unknown kinds
// return -1 there, which this matcher treats as "cannot ride the AOT path" and the upload path
// reports loudly instead of silently downgrading (see that header for the contract).

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

// An image plus one pointwise operator. The pointwise chain kernel serves the same shape, but this
// kernel stays as a fast path: it is what every drawImage of an image whose color space differs
// from the target takes (the color-space transform is appended automatically, e.g. a Display P3
// photo on an sRGB surface), and it uploads a few hundred bytes of uniforms with straight-line
// code where the chain uploads its instruction arrays (about 5 KB) and runs the interpreter loop.
static std::optional<PermutationMatchResult> TryMatchComposedTexture(
    const ProgramInfo* programInfo) {
  int hasCoverage = 0;
  if (!IsPositionOnlyRectGP(programInfo->getGeometryProcessor(), &hasCoverage)) {
    return std::nullopt;
  }
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
  // The precompiled vertex template transforms the coordinate affinely; a projective source must
  // stay off this artifact (the plain templates transform coordinates affinely, with no w division).
  if (te->hasPerspective()) {
    return std::nullopt;
  }

  // All four pointwise operators (ColorMatrix / Luma / AlphaThreshold / ColorSpaceXform) share the
  // same structural class (1 sampler, no CoverageFP, RGBA) and thus the same precompiled shader:
  // TexturedEffectShader. The operator is selected at draw time by the OpType runtime uniform (set
  // by the pointwise FP's onSetData), so it is not a compile-time dimension. The frag index only
  // encodes the structural axes HAS_XP and HAS_COVERAGE; subset clamping is a runtime uniform.
  if (!IsSupportedPointwiseOp(child1)) {
    return std::nullopt;
  }
  TexturedEffectInputs inputs;
  inputs.hasCoverage = hasCoverage != 0;
  inputs.xpType = xpType;
  auto composed = ComposeTexturedEffect(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto vertIndex = TexturedEffectShader::VD::domain().encode(composed->vertValues);
  auto fragIndex = TexturedEffectShader::D::domain().encode(composed->fragValues);
  return PermutationMatchResult{"TexturedEffectShader", vertIndex, fragIndex};
}

// The child a blur kernel samples per tap: a plain TextureEffect (inlined texture() call) or a
// TiledTextureEffect, whose shared tiled_sample.inc tiling is applied per tap. The kernels clamp
// plain-child taps to the subset (edge stretch), which is only the Clamp tap semantics: a Decal
// child must return transparent outside its subset — a drop shadow's blurred source is exactly
// that — so a non-Clamp plain child falls back. Tiled children are limited to the ShaderMode values
// tiled_sample.inc implements.
static bool IsServableBlurChild(const FragmentProcessor* childFP) {
  if (childFP == nullptr || childFP->numTextureSamplers() == 0) {
    return false;
  }
  if (childFP->name() == "TextureEffect") {
    TileMode modeX = TileMode::Clamp;
    TileMode modeY = TileMode::Clamp;
    static_cast<const TextureEffect*>(childFP)->getTileModes(&modeX, &modeY);
    return modeX == TileMode::Clamp && modeY == TileMode::Clamp;
  }
  if (childFP->name() == "TiledTextureEffect") {
    auto* childTiled = static_cast<const TiledTextureEffect*>(childFP);
    if (childTiled->hasPerspective()) {
      return false;
    }
    int modeX = 0;
    int modeY = 0;
    childTiled->getShaderModes(&modeX, &modeY);
    return TiledModeSupported(modeX) && TiledModeSupported(modeY);
  }
  return false;
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
  auto coverageType = SharedDeviceMaskValue(programInfo);
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
  if (!IsServableBlurChild(blur->childProcessor(0))) {
    return std::nullopt;
  }
  // The kernel radius is a runtime uniform, not a variant dimension. The precompiled shader uses
  // a fixed maximum kernel (MAX_SIGMA=9 → radius up to 10) and breaks early at the runtime
  // radius, so any sigma in [1,10] maps to the same variant. Sigma > 10 exceeds the fixed kernel
  // and falls back.
  int sigma = blur->getMaxSigma();
  if (sigma < 1 || sigma > 10) {
    return std::nullopt;
  }
  GaussianBlur1DInputs inputs;
  inputs.xpType = xpType;
  auto composed = ComposeGaussianBlur1D(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto fragIndex = GaussianBlur1DShader::FD::domain().encode(composed->fragValues);
  return PermutationMatchResult{"GaussianBlur1DShader", 0, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchGlassUDFTentBlur(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  int gpType = GetGPType(gp);
  if (gpType < 0) {
    return std::nullopt;
  }
  if (programInfo->numColorFragmentProcessors() != 1) {
    return std::nullopt;
  }
  auto coverageType = SharedDeviceMaskValue(programInfo);
  if (!coverageType) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "GlassUDFTentBlurFragmentProcessor") {
    return std::nullopt;
  }
  auto* tent = static_cast<const GlassUDFTentBlurFragmentProcessor*>(fp);
  if (tent->numChildProcessors() != 1) {
    return std::nullopt;
  }
  // The horizontal pass reads a plain TextureEffect; the vertical pass a TiledTextureEffect.
  if (!IsServableBlurChild(tent->childProcessor(0))) {
    return std::nullopt;
  }
  // The loop upper bound is the fixed constant 64; a longer max radius exceeds it and falls back.
  if (tent->getMaxRadius() > 64) {
    return std::nullopt;
  }
  GlassUDFTentBlurInputs inputs;
  inputs.xpType = xpType;
  auto composed = ComposeGlassUDFTentBlur(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto fragIndex = GlassUDFTentBlurShader::FD::domain().encode(composed->fragValues);
  return PermutationMatchResult{"GlassUDFTentBlurShader", 0, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchGlassRefraction(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp == nullptr) {
    return std::nullopt;
  }
  // The GP selects the vertex layout and the initial-coverage source. Both forms must be the
  // common-color attribute layout: the kernels read the Color uniform, and a per-vertex color
  // draw would quantize fractional paint colors through the UByte4 attribute.
  int gpKind = -1;
  if (gp->name() == "EllipseGeometryProcessor") {
    auto* egp = static_cast<const EllipseGeometryProcessor*>(gp);
    if (!egp->hasCommonColor()) {
      return std::nullopt;
    }
    gpKind = 0;
  } else if (gp->name() == "QuadPerEdgeAAGeometryProcessor") {
    auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
    // The uvMatrix form is required: it declares no uvCoord attribute, so the GP transforms the
    // position for the background coordinate, which is what the quad vertex kernel reads. A
    // subset-carrying quad has a different transform contract and stays on the fallback route.
    if (!quadGP->hasCommonColor() || !quadGP->hasUVMatrix() || quadGP->getHasSubset() ||
        quadGP->getHasUVPerspective()) {
      return std::nullopt;
    }
    gpKind = 1;
  } else {
    return std::nullopt;
  }
  if (programInfo->numColorFragmentProcessors() != 1) {
    return std::nullopt;
  }
  // The only coverage processor the kernel takes is the quad form's local shape mask (a SrcIn
  // ShaderMaskFilter over a hardware-tiled RGBA texture), applied through the HasLocalMask runtime
  // branch. It needs an affine mask transform, like the background coordinate below.
  if (programInfo->numFragmentProcessors() != 1) {
    auto coverage = ClassifyCoverageFP(programInfo);
    if (gpKind != 1 || programInfo->numFragmentProcessors() != 2 || !coverage ||
        *coverage != CoverageKind::LocalMask) {
      return std::nullopt;
    }
    auto mask = programInfo->getFragmentProcessor(1)->childProcessor(0);
    if (mask->numCoordTransforms() != 1 || mask->coordTransform(0)->matrix.hasPerspective()) {
      return std::nullopt;
    }
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "GlassRefractionFragmentProcessor") {
    return std::nullopt;
  }
  auto* refraction = static_cast<const GlassRefractionFragmentProcessor*>(fp);
  if (refraction->numChildProcessors() != 1 || refraction->numTextureSamplers() != 1) {
    return std::nullopt;
  }
  // Both vertex kernels emit the affine coordinate form ((Matrix * vec3(pos, 1)).xy computed in
  // the vertex stage), matching GeometryProcessor::emitTransforms' non-perspective branch. A
  // projective transform makes the runtime emit a vec3 varying divided in the fragment stage, so
  // it must ride the runtime route, as every plain-coordinate kernel does with projective sources.
  if (fp->numCoordTransforms() > 0 && fp->coordTransform(0)->matrix.hasPerspective()) {
    return std::nullopt;
  }
  auto child = refraction->childProcessor(0);
  if (child == nullptr) {
    return std::nullopt;
  }
  // The geometry child selects the compile-time dimension because it changes the sampler layout;
  // the dispersion and lighting branches are runtime uniforms and need no dimension.
  int geometryKind = -1;
  if (child->name() == "GlassSDFGeometryFragmentProcessor") {
    auto* sdf = static_cast<const GlassSDFGeometryFragmentProcessor*>(child);
    geometryKind = sdf->getShapeType() == GlassShapeType::RoundedRect ? 0 : 1;
  } else if (child->name() == "GlassUDFGeometryFragmentProcessor") {
    auto* udf = static_cast<const GlassUDFGeometryFragmentProcessor*>(child);
    geometryKind = udf->isEdgeLightingEnabled() ? 3 : 2;
  }
  if (geometryKind < 0) {
    return std::nullopt;
  }
  GlassRefractionInputs inputs;
  inputs.gpKind = gpKind;
  inputs.geometryKind = geometryKind;
  // Same predicate the runtime folds into the program key, so the compile-time variant tracks the
  // runtime program one-for-one.
  inputs.dispersionOn = refraction->hasDispersion();
  inputs.xpType = xpType;
  auto composed = ComposeGlassRefraction(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto vertIndex = GlassRefractionShader::VD::domain().encode(composed->vertValues);
  auto fragIndex = GlassRefractionShader::FD::domain().encode(composed->fragValues);
  return PermutationMatchResult{"GlassRefractionShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchHairlineLine(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "HairlineLineGeometryProcessor") {
    return std::nullopt;
  }
  if (!HasNoProcessorsOrDirectClip(programInfo)) {
    return std::nullopt;
  }
  HairlineLineInputs inputs;
  inputs.xpType = GetXPType(programInfo);
  auto composed = ComposeHairlineLine(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto fragIndex = HairlineLineShader::FD::domain().encode(composed->fragValues);
  return PermutationMatchResult{"HairlineLineShader", 0, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchHairlineQuad(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "HairlineQuadGeometryProcessor") {
    return std::nullopt;
  }
  if (!HasNoProcessorsOrDirectClip(programInfo)) {
    return std::nullopt;
  }
  HairlineQuadInputs inputs;
  inputs.xpType = GetXPType(programInfo);
  auto composed = ComposeHairlineQuad(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto fragIndex = HairlineQuadShader::FD::domain().encode(composed->fragValues);
  return PermutationMatchResult{"HairlineQuadShader", 0, fragIndex};
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
  auto coverageType = SharedDeviceMaskValue(programInfo);
  if (!coverageType) {
    return std::nullopt;
  }
  auto* egp = static_cast<const EllipseGeometryProcessor*>(gp);
  EllipseFillInputs inputs;
  inputs.hasCommonColor = egp->hasCommonColor();
  inputs.deviceMask = *coverageType;
  inputs.xpType = GetXPType(programInfo);
  auto composed = ComposeEllipseFill(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto vertIndex = EllipseFillShader::Dims::domain().encode(composed->vertValues);
  auto fragIndex = EllipseFillShader::FD::domain().encode(composed->fragValues);
  return PermutationMatchResult{"EllipseFillShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchComplexEllipseFill(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "ComplexEllipseGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numColorFragmentProcessors() != 0) {
    return std::nullopt;
  }
  // A sole coverage FP is served through the shared coverage contract, with no dimension either
  // way: an AA rect/rrect clip rides the runtime clip uniforms (coverage_uniforms.inc /
  // clip_coverage.inc) and a device-space mask is the runtime HasDeviceMask uniform over the
  // always-declared MaskTextureSampler (padded with the dummy texture when absent). LocalMask needs
  // a per-vertex UV this geometry does not provide and stays rejected.
  if (!SharedDeviceMaskValue(programInfo)) {
    return Fail(PermutationMatchFailure::CoverageShape);
  }
  auto* cegp = static_cast<const ComplexEllipseGeometryProcessor*>(gp);
  ComplexEllipseFillInputs inputs;
  inputs.isStroke = cegp->isStroke();
  inputs.hasCommonColor = cegp->hasCommonColor();
  inputs.xpType = GetXPType(programInfo);
  auto composed = ComposeComplexEllipseFill(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto vertIndex = ComplexEllipseFillShader::Dims::domain().encode(composed->vertValues);
  auto fragIndex = ComplexEllipseFillShader::FD::domain().encode(composed->fragValues);
  return PermutationMatchResult{"ComplexEllipseFillShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchNonAARRectFill(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "NonAARRectGeometryProcessor") {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() > 1) {
    return std::nullopt;
  }
  auto* ngp = static_cast<const NonAARRectGeometryProcessor*>(gp);
  NonAARRectFillInputs inputs;
  inputs.hasCommonColor = ngp->hasCommonColor();
  inputs.isStroke = ngp->isStroke();
  if (programInfo->numFragmentProcessors() == 1) {
    // A sole analytic clip (AA rect or rounded rect) rides the runtime clip contract uniforms, so
    // it needs no dimension: the FP is there only to write them.
    const bool clipOnly = programInfo->numColorFragmentProcessors() == 0 &&
                          HasDirectAnalyticClipCoverage(programInfo);
    if (!clipOnly) {
      auto fp = programInfo->getFragmentProcessor(0);
      // The textured variant carries the single-tap tiled modes only: the mipmap-repeat 4-tap path
      // and perspective coordinates stay on the runtime route. The textured form never combines
      // with stroke; ComposeNonAARRectFill applies that rejection through the shared predicate.
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
      if (!TiledModeSupported(modeX) || !TiledModeSupported(modeY)) {
        return std::nullopt;
      }
      inputs.textured = true;
    }
  }
  inputs.xpType = GetXPType(programInfo);
  auto composed = ComposeNonAARRectFill(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto vertIndex = NonAARRectFillShader::Dims::domain().encode(composed->vertValues);
  auto fragIndex = NonAARRectFillShader::FD::domain().encode(composed->fragValues);
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
  auto* cngp = static_cast<const ComplexNonAARRectGeometryProcessor*>(gp);
  ComplexNonAARRectFillInputs inputs;
  inputs.hasCommonColor = cngp->hasCommonColor();
  inputs.isStroke = cngp->isStroke();
  inputs.xpType = GetXPType(programInfo);
  auto composed = ComposeComplexNonAARRectFill(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto vertIndex = ComplexNonAARRectFillShader::Dims::domain().encode(composed->vertValues);
  auto fragIndex = ComplexNonAARRectFillShader::FD::domain().encode(composed->fragValues);
  return PermutationMatchResult{"ComplexNonAARRectFillShader", vertIndex, fragIndex};
}

// Extracts the RoundStrokeRect rule's input predicates from the ProgramInfo. Whole-draw
// rejections (wrong GP kind, unexpected fragment processors, perspective UV) stay here: they
// route the draw elsewhere and never shape the rule's reachable variant set, which is defined
// by ComposeRoundStrokeRect in PermutationRules.cpp and shared with the build tool.
static std::optional<RoundStrokeRectInputs> ExtractRoundStrokeRect(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "RoundStrokeRectGeometryProcessor") {
    return std::nullopt;
  }
  // The kernel reads the clip from the runtime clip contract uniforms, so a sole analytic clip is
  // admitted alongside the FP-free form.
  if (!HasNoProcessorsOrDirectClip(programInfo)) {
    return std::nullopt;
  }
  auto* rsgp = static_cast<const RoundStrokeRectGeometryProcessor*>(gp);
  // Skip perspective UV transforms — not covered by precompiled variants.
  if (rsgp->isUVPerspective()) {
    return std::nullopt;
  }
  RoundStrokeRectInputs inputs;
  inputs.isCoverageAA = rsgp->getAAType() == AAType::Coverage;
  inputs.hasCommonColor = rsgp->hasCommonColor();
  inputs.hasUVMatrix = rsgp->hasUVMatrixTransform();
  inputs.xpType = GetXPType(programInfo);
  return inputs;
}

static std::optional<PermutationMatchResult> TryMatchRoundStrokeRectFill(
    const ProgramInfo* programInfo) {
  auto inputs = ExtractRoundStrokeRect(programInfo);
  if (!inputs) {
    return std::nullopt;
  }
  auto composed = ComposeRoundStrokeRect(*inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto vertIndex = RoundStrokeRectFillShader::Dims::domain().encode(composed->vertValues);
  auto fragIndex = RoundStrokeRectFillShader::FD::domain().encode(composed->fragValues);
  return PermutationMatchResult{"RoundStrokeRectFillShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchShapeInstancedTextureCoverage(
    const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "ShapeInstancedGeometryProcessor" || GetXPType(programInfo) != 0) {
    return std::nullopt;
  }
  // Two forms: a bare coverage texture, or a single-interval gradient color FP ahead of it.
  int gradient = 0;
  size_t colorCount = programInfo->numColorFragmentProcessors();
  if (colorCount == 1) {
    auto colorFP = programInfo->getFragmentProcessor(0);
    if (colorFP->name() != "ClampedGradientEffect" || colorFP->numChildProcessors() != 2 ||
        colorFP->childProcessor(0)->name() != "SingleIntervalGradientColorizer") {
      return std::nullopt;
    }
    auto layout = colorFP->childProcessor(1);
    if (layout->numCoordTransforms() != 1 || layout->coordTransform(0)->matrix.hasPerspective()) {
      return std::nullopt;
    }
    gradient = 1;
  } else if (colorCount != 0) {
    return std::nullopt;
  }
  // One coverage texture, optionally preceded/followed by an analytic clip. The clip rides the
  // runtime clip contract uniforms (no permutation change), so both forms select the same variant.
  auto fpCount = programInfo->numFragmentProcessors();
  const FragmentProcessor* fp = nullptr;
  if (fpCount == colorCount + 1) {
    fp = programInfo->getFragmentProcessor(colorCount);
  } else if (fpCount == colorCount + 2) {
    auto first = programInfo->getFragmentProcessor(colorCount);
    auto second = programInfo->getFragmentProcessor(colorCount + 1);
    auto firstKind = ClassifyCoverage(first);
    auto secondKind = ClassifyCoverage(second);
    auto isClip = [](const std::optional<CoverageKind>& kind) {
      return kind && (*kind == CoverageKind::AARect || *kind == CoverageKind::RRect);
    };
    if (isClip(firstKind) && !isClip(secondKind)) {
      fp = second;
    } else if (isClip(secondKind) && !isClip(firstKind)) {
      fp = first;
    } else {
      // Two masks, or two clips, are not a shape this kernel evaluates.
      return Fail(PermutationMatchFailure::CoverageShape);
    }
  } else {
    return std::nullopt;
  }
  if (fp->name() != "TextureEffect") {
    return std::nullopt;
  }
  auto* texture = static_cast<const TextureEffect*>(fp);
  if (texture->numTextureSamplers() != 1 || texture->isYUV() || texture->hasRGBAAA()) {
    return std::nullopt;
  }
  auto* shape = static_cast<const ShapeInstancedGeometryProcessor*>(gp);
  // The color attribute is unconditional (white broadcast for colorless batches), so both forms
  // read the same vColor varying. This kernel carries no AA coverage attribute.
  if (shape->getAAType() != AAType::None) {
    return std::nullopt;
  }
  ShapeInstancedTextureCoverageInputs inputs;
  inputs.gradient = gradient != 0;
  auto composed = ComposeShapeInstancedTextureCoverage(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto vertIndex = ShapeInstancedTextureCoverageShader::D::domain().encode(composed->vertValues);
  auto fragIndex = ShapeInstancedTextureCoverageShader::FD::domain().encode(composed->fragValues);
  return PermutationMatchResult{"ShapeInstancedTextureCoverageShader", vertIndex, fragIndex};
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
  auto* sigp = static_cast<const ShapeInstancedGeometryProcessor*>(gp);
  ShapeInstancedFillInputs inputs;
  inputs.isCoverageAA = sigp->getAAType() == AAType::Coverage;
  inputs.xpType = GetXPType(programInfo);
  auto composed = ComposeShapeInstancedFill(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto vertIndex = ShapeInstancedFillShader::Dims::domain().encode(composed->vertValues);
  auto fragIndex = ShapeInstancedFillShader::FD::domain().encode(composed->fragValues);
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
  auto* mgp = static_cast<const MeshGeometryProcessor*>(gp);
  MeshFillInputs inputs;
  inputs.hasTexCoords = mgp->getHasTexCoords();
  inputs.hasCoverage = mgp->getHasCoverage();
  inputs.xpType = GetXPType(programInfo);
  auto composed = ComposeMeshFill(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto vertIndex = MeshFillShader::Dims::domain().encode(composed->vertValues);
  auto fragIndex = MeshFillShader::FD::domain().encode(composed->fragValues);
  return PermutationMatchResult{"MeshFillShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> TryMatchPerlin(const ProgramInfo* programInfo) {
  int hasCoverage = 0;
  if (!IsPositionOnlyRectGP(programInfo->getGeometryProcessor(), &hasCoverage)) {
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
  PerlinNoiseFillInputs inputs;
  inputs.hasCoverage = hasCoverage != 0;
  inputs.xpType = xpType;
  auto composed = ComposePerlinNoiseFill(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto vertIndex = PerlinNoiseFillShader::VD::domain().encode(composed->vertValues);
  auto fragIndex = PerlinNoiseFillShader::D::domain().encode(composed->fragValues);
  return PermutationMatchResult{"PerlinNoiseFillShader", vertIndex, fragIndex};
}

static std::optional<PermutationMatchResult> MatchPermutationImpl(const ProgramInfo* programInfo) {
  // OpenGL is served from the bundle profile matching the context (desktop GL or the GLES
  // family: GLES, WebGL, SwiftShader). The precompiled kernels only declare 2D samplers. Desktop GL
  // rectangle textures are copied into 2D textures where they enter (ProxyProvider::
  // wrapExternalTexture, CGLHardwareTexture, and the dst copy of a rectangle render target), so a
  // rectangle sampler only reaches this gate through an unconverted view. External (OES) textures
  // (Android SurfaceTexture and YUV hardware buffers) are not converted yet: with a bundle loaded
  // there is no runtime route, so their draws are dropped and counted as droppedDraws.
  if (programInfo->backend() == Backend::OpenGL &&
      (!programInfo->glProfileMatchesCache() || !programInfo->samplersAre2D())) {
    return Fail(PermutationMatchFailure::GlGateRejected);
  }

  // MatchPermutation already refused every write swizzle other than RGBA and alpha-only (AAAA).
  if (programInfo->getOutputSwizzle() == Swizzle::AAAA()) {
    // Alpha-only targets: only the kernels that apply the write swizzle at output (via the
    // OutputAlphaSwizzle uniform) may serve the draw. Other kernels assume RGBA output. The
    // gaussian blur kernel applies it too (an alpha-only blur pass writes its mask accumulation
    // through the AAAA swizzle), with the AlphaChild flag carrying the .rrrr readback.
    if (auto result = TryMatchSolidColorFill(programInfo)) {
      return result;
    }
    if (auto result = TryMatchMaskFill(programInfo)) {
      return result;
    }
    if (auto result = TryMatchQuadTextureFill(programInfo)) {
      return result;
    }
    if (auto result = TryMatchGaussianBlur1D(programInfo)) {
      return result;
    }
    // The pointwise kernels now carry the OutputAlphaSwizzle uniform (output_swizzle.inc), so a
    // chain-serving draw onto an alpha-only target — a const-color or gradient mask pass, the
    // largest remaining libpag gap — is served by the same variants as the RGBA case.
    if (auto result = TryMatchPointwiseDAG(programInfo)) {
      return result;
    }
    return Fail(PermutationMatchFailure::NoMatchingFamily);
  }
  if (auto result = TryMatchQuadColorFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchQuadTextureFill(programInfo)) {
    return result;
  }
  if (auto result = TryMatchYUVTextureFill(programInfo)) {
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
  if (auto result = TryMatchPointwiseDAG(programInfo)) {
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
  if (auto result = TryMatchGaussianBlur1D(programInfo)) {
    return result;
  }
  if (auto result = TryMatchGlassUDFTentBlur(programInfo)) {
    return result;
  }
  if (auto result = TryMatchGlassRefraction(programInfo)) {
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
  if (auto result = TryMatchShapeInstancedTextureCoverage(programInfo)) {
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

// The ShaderTextureRead semantics the draw's sampled textures need. YUV planes are excluded: the
// YUV kernels read the plane channels they need (.r / .rg), which the read swizzle preserves.
static uint32_t RequiredTextureReads(const ProgramInfo* programInfo) {
  uint32_t required = 0;
  auto addTexture = [&](const std::shared_ptr<Texture>& texture) {
    if (texture != nullptr && AOTReadSwizzleMode(texture->format()) != 0) {
      required |= ShaderTextureReadSwizzle;
    }
  };
  auto gp = programInfo->getGeometryProcessor();
  for (size_t i = 0; gp != nullptr && i < gp->numTextureSamplers(); ++i) {
    addTexture(gp->textureAt(i));
  }
  FragmentProcessor::Iter iter(programInfo);
  for (auto fp = iter.next(); fp != nullptr; fp = iter.next()) {
    if (fp->name() == "TextureEffect" && static_cast<const TextureEffect*>(fp)->isYUV()) {
      continue;
    }
    for (size_t i = 0; i < fp->numTextureSamplers(); ++i) {
      addTexture(fp->textureAt(i));
    }
  }
  return required;
}

std::optional<PermutationMatchResult> MatchPermutation(const ProgramInfo* programInfo,
                                                       PermutationMatchFailure* failure) {
  // Sub-reason carried out of the (deeply nested) TryMatch* helpers: they only return nullopt, so
  // the first rejecting site records why on this thread and the public entry point copies it into
  // the caller's out-parameter.
  gPendingMatchFailure = PermutationMatchFailure::None;
  // Only RGBA and alpha-only (AAAA, Swizzle::ForWrite(ALPHA_8)) render targets are supported. Any
  // other write swizzle would need a mapping the precompiled shaders do not implement.
  auto outputSwizzle = programInfo->getOutputSwizzle();
  if (outputSwizzle != Swizzle::RGBA() && outputSwizzle != Swizzle::AAAA()) {
    if (failure != nullptr) {
      *failure = PermutationMatchFailure::UnsupportedOutputSwizzle;
    }
    return std::nullopt;
  }
  auto matchFailure = PermutationMatchFailure::NoMatchingRule;
  auto result = MatchPermutationImpl(programInfo);
  if (result) {
    auto info = ShaderRegistry::Find(result->shaderName);
    if (info == nullptr || !IsBuildablePermutation(*info, result->vertPermutationIndex,
                                                   result->fragPermutationIndex)) {
      result.reset();
      matchFailure = PermutationMatchFailure::PermutationNotEnumerated;
    } else if ((RequiredTextureReads(programInfo) & ~info->textureReads) != 0) {
      // Default deny: the matched family's shader would read these textures without the
      // semantic the runtime applies, so the draw is a reported miss rather than wrong pixels.
      result.reset();
      matchFailure = PermutationMatchFailure::UnsupportedTextureRead;
    }
  }
  if (!result && matchFailure == PermutationMatchFailure::NoMatchingRule &&
      gPendingMatchFailure != PermutationMatchFailure::None) {
    matchFailure = gPendingMatchFailure;
  }
  gPendingMatchFailure = PermutationMatchFailure::None;
  if (failure != nullptr) {
    *failure = result ? PermutationMatchFailure::None : matchFailure;
  }
  return result;
}

const char* PermutationMatchFailureName(PermutationMatchFailure failure) {
  switch (failure) {
    case PermutationMatchFailure::None:
      return "None";
    case PermutationMatchFailure::UnsupportedOutputSwizzle:
      return "UnsupportedOutputSwizzle";
    case PermutationMatchFailure::NoMatchingRule:
      return "NoMatchingRule";
    case PermutationMatchFailure::UnsupportedTextureRead:
      return "UnsupportedTextureRead";
    case PermutationMatchFailure::GlGateRejected:
      return "GlGateRejected";
    case PermutationMatchFailure::QuadSubset:
      return "QuadSubset";
    case PermutationMatchFailure::GradientOrNoiseBudget:
      return "GradientOrNoiseBudget";
    case PermutationMatchFailure::CoverageShape:
      return "CoverageShape";
    case PermutationMatchFailure::NoMatchingFamily:
      return "NoMatchingFamily";
    case PermutationMatchFailure::PermutationNotEnumerated:
      return "PermutationNotEnumerated";
    case PermutationMatchFailure::MissingLowering:
      return "MissingLowering";
  }
  return "Unknown";
}

}  // namespace tgfx

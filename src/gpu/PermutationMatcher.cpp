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
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include "ColorSpaceXformHelper.h"
#include "ShaderMatchHelpers.h"
#include "gpu/PrecompiledShaderCache.h"
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
#include "gpu/shaders/AllShaderFamilies.h"
#include "layers/processors/GlassRefractionFragmentProcessor.h"
#include "layers/processors/GlassShapeGeometryFragmentProcessor.h"
#include "layers/processors/GlassUDFTentBlurFragmentProcessor.h"
#include "tgfx/core/BytesKey.h"

namespace tgfx {

// Sub-reason of the most recent rejection on this thread. The TryMatch* helpers only ever return
// nullopt, so the first rejecting site records why here; MatchPermutation copies it into the
// caller's out-parameter and clears it. Diagnostic only: no control flow depends on it.
static thread_local PermutationMatchFailure gPendingMatchFailure = PermutationMatchFailure::None;

std::nullopt_t Fail(PermutationMatchFailure reason) {
  if (gPendingMatchFailure == PermutationMatchFailure::None) {
    gPendingMatchFailure = reason;
  }
  return std::nullopt;
}

bool RejectsAlphaOnlyTarget(bool servesAlphaOnly, const ProgramInfo* programInfo) {
  return !servesAlphaOnly && programInfo->getOutputSwizzle() == Swizzle::AAAA();
}

// Returns XP_NONE for EmptyXferProcessor, XP_DST_TEXTURE / XP_FRAMEBUFFER_FETCH for the
// PorterDuff modes, or XP_UNSUPPORTED for XP types no kernel can serve.
int GetXPType(const ProgramInfo* programInfo) {
  auto xp = programInfo->getXferProcessor();
  if (xp == EmptyXferProcessor::GetInstance()) {
    return XP_NONE;
  }
  if (IsProcessor<PorterDuffXferProcessor>(xp)) {
    if (xp->dstTextureView() != nullptr) {
      return XP_DST_TEXTURE;
    }
    return XP_FRAMEBUFFER_FETCH;
  }
  return XP_UNSUPPORTED;
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
  if (fp == nullptr || !IsProcessor<RectEffect>(fp) || fp->numChildProcessors() != 0) {
    return false;
  }
  auto rectEffect = static_cast<const RectEffect*>(fp);
  return rectEffect->isAntiAlias();
}

// Classifies coverage semantics independently of any shader's permutation layout. LocalMask is a
// distinct capability: it needs a local-space varying and sampler and therefore cannot be encoded
// in the shared three-value HAS_COVERAGE dimension.
std::optional<CoverageKind> ClassifyCoverage(const FragmentProcessor* coverageFP) {
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
std::optional<CoverageKind> ClassifyCoverageFP(const ProgramInfo* programInfo) {
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
bool HasDirectAnalyticClipCoverage(const ProgramInfo* programInfo) {
  auto coverage = ClassifyCoverageFP(programInfo);
  return coverage == CoverageKind::AARect || coverage == CoverageKind::RRect;
}

// Kernels whose color comes from the GP Color uniform: a color FP would be silently dropped, so
// the draw may carry no FP at all or only a direct analytic clip evaluated from the runtime clip
// contract uniforms.
bool HasNoProcessorsOrDirectClip(const ProgramInfo* programInfo) {
  return programInfo->numFragmentProcessors() == 0 ||
         (programInfo->numColorFragmentProcessors() == 0 &&
          HasDirectAnalyticClipCoverage(programInfo));
}

// Analytic clip coverage (AA rect or rrect) changes only fragment math, whereas a device-space
// mask adds a sampler and shifts the terminal XP binding. Kernels using the runtime clip contract
// therefore compile only this ABI distinction and carry the clip state through uniforms.
std::optional<int> SharedDeviceMaskValue(const ProgramInfo* programInfo) {
  auto coverage = ClassifyCoverageFP(programInfo);
  if (!coverage || *coverage == CoverageKind::LocalMask) {
    return std::nullopt;
  }
  return *coverage == CoverageKind::DeviceMask ? 1 : 0;
}

int GetGPType(const GeometryProcessor* gp) {
  auto name = gp->name();
  if (name == "DefaultGeometryProcessor") {
    return 0;
  }
  if (name == "QuadPerEdgeAAGeometryProcessor") {
    return 1;
  }
  return -1;
}

// The position-only rect GP forms the texture and noise fill kernels share: a DefaultGP, or a
// QuadPerEdgeAA GP whose coordinate comes from aPosition (a common color, a uv matrix, no
// per-quad subset), since those precompiled vertex stages declare aPosition plus the optional AA
// coverage attribute only.
bool IsPositionOnlyRectGP(const GeometryProcessor* gp) {
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
  return true;
}

// ShaderMode values the shared single-tap tiled_sample.inc handles for the blur tap loop:
// None(0), Clamp(1), RepeatNearestNone(2), RepeatLinearNone(3), MirrorRepeat(6),
// ClampToBorderNearest(7), ClampToBorderLinear(8). Mode 3 shares mode 2's wrap-and-clamp
// coordinate math — the linear-vs-nearest difference is the hardware sampler filter, and each
// blur tap goes through the same tiledMapCoord path the fill kernel's pixel-verified mode-3
// handling uses. The mipmap-repeat modes 4/5 need a 4-tap seam blend per tap, which only the
// tiled fill kernel implements (see TiledFillModeSupported); blur keeps falling back for those.
bool TiledModeSupported(int mode) {
  return mode == 0 || mode == 1 || mode == 2 || mode == 3 || mode == 6 || mode == 7 || mode == 8;
}

// The tiled fill kernel additionally handles the mipmap-repeat modes 4/5 via an inline 4-tap seam
// blend. So it accepts every mode.
bool TiledFillModeSupported(int mode) {
  return TiledModeSupported(mode) || mode == 4 || mode == 5;
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
bool IsChainQuadSubsetServable(const QuadPerEdgeAAGeometryProcessor* quadGP,
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
    if (!IsProcessor<TextureEffect>(leaf)) {
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

// TFTypeToIndex lives in ColorSpaceXformHelper as the single mapping authority: unknown kinds
// return -1 there, which this matcher treats as "cannot ride the AOT path" and the upload path
// reports loudly instead of silently downgrading (see that header for the contract).

// Returns true if fp is one of the four pointwise operators that share the runtime OpType uniform
// (ColorMatrix / Luma / AlphaThreshold / ColorSpaceXform). For ColorSpaceXform the transfer-function
// type must be in the supported 0-3 range; the pipeline flags themselves are runtime uniforms.
bool IsSupportedPointwiseOp(const FragmentProcessor* fp) {
  if (IsProcessor<ColorMatrixFragmentProcessor>(fp) || IsProcessor<LumaFragmentProcessor>(fp) ||
      IsProcessor<AlphaThresholdFragmentProcessor>(fp)) {
    return true;
  }
  if (!IsProcessor<ColorSpaceXformEffect>(fp)) {
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
// The child a blur kernel samples per tap: a plain TextureEffect (inlined texture() call) or a
// TiledTextureEffect, whose shared tiled_sample.inc tiling is applied per tap. The kernels clamp
// plain-child taps to the subset (edge stretch), which is only the Clamp tap semantics: a Decal
// child must return transparent outside its subset — a drop shadow's blurred source is exactly
// that — so a non-Clamp plain child falls back. Tiled children are limited to the ShaderMode values
// tiled_sample.inc implements.
bool IsServableBlurChild(const FragmentProcessor* childFP) {
  if (childFP == nullptr || childFP->numTextureSamplers() == 0) {
    return false;
  }
  if (IsProcessor<TextureEffect>(childFP)) {
    TileMode modeX = TileMode::Clamp;
    TileMode modeY = TileMode::Clamp;
    static_cast<const TextureEffect*>(childFP)->getTileModes(&modeX, &modeY);
    return modeX == TileMode::Clamp && modeY == TileMode::Clamp;
  }
  if (IsProcessor<TiledTextureEffect>(childFP)) {
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

bool ContainsGradientSource(const FragmentProcessor* processor) {
  if (processor == nullptr) {
    return false;
  }
  if (IsProcessor<ClampedGradientEffect>(processor)) {
    return true;
  }
  for (size_t index = 0; index < processor->numChildProcessors(); ++index) {
    if (ContainsGradientSource(processor->childProcessor(index))) {
      return true;
    }
  }
  return false;
}

// Per-family gate of the walk (see MatchPermutationImpl for the switch semantics). A fused-pass
// committed draw's first phase admits only the two single-pass fused kernels — the interpreter and
// the perlin kernel; its second phase admits every other dedicated family (the fused ones already
// rejected the shape and are not retried).
template <typename Family>
static bool FamilyWalkEnabled(bool allowDedicated, bool allowInterpreter, bool committed,
                              bool fusedPhase) {
  constexpr bool isInterpreter = std::is_same_v<Family, PointwiseChainShader>;
  constexpr bool isPerlin = std::is_same_v<Family, PerlinNoiseFillShader>;
  if (committed && fusedPhase) {
    return (isInterpreter && allowInterpreter) || (isPerlin && allowDedicated);
  }
  if (committed) {
    return !isInterpreter && !isPerlin && allowDedicated;
  }
  return isInterpreter ? allowInterpreter : allowDedicated;
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

  // Alpha-only (AAAA, Swizzle::ForWrite(ALPHA_8)) targets: only the kernels that apply the write
  // swizzle at output (via the OutputAlphaSwizzle uniform) may serve the draw; the others assume
  // RGBA output. Which kernels those are is each family's own ServesAlphaOnly declaration. The
  // pointwise kernels carry the swizzle (output_swizzle.inc), so a chain-serving draw onto an
  // alpha-only target — a const-color or gradient mask pass, the largest remaining libpag gap —
  // is served by the same variants as the RGBA case. The gaussian blur kernel applies it too (an
  // alpha-only blur pass writes its mask accumulation through the AAAA swizzle, with the
  // AlphaChild flag carrying the .rrrr readback).
  const bool alphaOnly = programInfo->getOutputSwizzle() == Swizzle::AAAA();

#ifndef NDEBUG
  // Audit pass (debug builds only): ask every family — including the ones the alpha-only gate
  // below skips — and do not stop at the first acceptor. Three invariants, each a silent bug when
  // violated (the dispatch order would decide the winner):
  //   1. at most one family accepts a given draw;
  //   2. a family that does not declare ServesAlphaOnly never accepts an alpha-only draw, which
  //      catches a kernel gaining alpha support without flipping its flag;
  //   3. a Match result lies inside the family's reachable set, which catches Extract producing
  //      an input combination AllInputs does not enumerate — the bundle would lack the variant
  //      and the draw would be dropped at runtime.
  // The extra Match calls are pure shape checks; the thread-local failure reason they may record
  // is saved and restored so the rejection accounting is unaffected. The audit runs once per
  // distinct program shape: repeated prepares of the same shape re-audit nothing.
  {
    static thread_local std::unordered_set<BytesKey, BytesKeyHasher> auditedProgramKeys;
    if (auditedProgramKeys.insert(programInfo->programKeyForDiagnostics()).second) {
      const auto savedFailure = gPendingMatchFailure;
      int acceptors = 0;
#define TGFX_AUDIT_FAMILY(Family)                                                             \
  if (auto auditResult = Family::Match(programInfo)) {                                        \
    if (alphaOnly && !Family::ServesAlphaOnly) {                                              \
      assert(false && #Family " accepts an alpha-only draw; flip its ServesAlphaOnly");       \
    }                                                                                         \
    /* The interpreter is the walk's documented fallback: it may accept a draw a dedicated  \
     * family serves too (that overlap is what the dedicated-kernel toggle A/Bs), so only  \
     * dedicated families count towards uniqueness. */ \
    if (++acceptors > 1 && std::string_view(#Family) != "PointwiseChainShader") {             \
      assert(false && "multiple shader families accept the same draw");                       \
    }                                                                                         \
    if (Family::CachedReachable().count(                                                      \
            {auditResult->vertPermutationIndex, auditResult->fragPermutationIndex}) == 0) {   \
      assert(false && #Family                                                                 \
             " matched a variant outside its reachable set; extend "                          \
             "its AllInputs");                                                                \
    }                                                                                         \
  }
      TGFX_FOR_EACH_SHADER_FAMILY(TGFX_AUDIT_FAMILY)
#undef TGFX_AUDIT_FAMILY
      gPendingMatchFailure = savedFailure;
    }
  }
#endif

  std::optional<PermutationMatchResult> accepted;
  // The walk tries the dedicated families first and the interpreter last (the entry order in
  // AllShaderFamilies.h), and the two diagnostic switches exclude their half of that list:
  // decompositionEnabled gates the interpreter, dedicatedKernelsEnabled gates every dedicated
  // family.
  auto* shaderContext = programInfo->getContext();
  auto* shaderCache = shaderContext != nullptr ? shaderContext->precompiledShaderCache() : nullptr;
  const bool allowDedicated = shaderCache == nullptr || shaderCache->dedicatedKernelsEnabled();
  // Gradients keep the interpreter even with the decomposition switch off (only the extended
  // chain kind serves them), mirroring the required-gradient-lowering the switch always retained.
  bool hasGradient = false;
  for (size_t index = 0; !hasGradient && index < programInfo->numFragmentProcessors(); ++index) {
    hasGradient = ContainsGradientSource(programInfo->getFragmentProcessor(index));
  }
  const bool allowInterpreter =
      shaderCache == nullptr || shaderCache->decompositionEnabled() || hasGradient;
  // A fused-pass-committed draw (its record-time entry planned a single fused pass instead of a
  // plan task) first walks only the two fused kernels — the interpreter and the perlin kernel —
  // so a dedicated family cannot steal the committed shape; when neither serves it, the walk
  // retries the remaining dedicated families in the normal order, which is the strict-fused-then-
  // plain fallback order the retired prepare-time rewrite used.
  const bool committed = programInfo->isFusedPassCommitted();
  bool fusedPhase = committed;
#define TGFX_TRY_FAMILY(Family)                                                             \
  if (!accepted && (!alphaOnly || Family::ServesAlphaOnly) &&                               \
      FamilyWalkEnabled<Family>(allowDedicated, allowInterpreter, committed, fusedPhase)) { \
    if (auto result = Family::Match(programInfo)) {                                         \
      accepted = PermutationMatchResult{#Family, result->vertPermutationIndex,              \
                                        result->fragPermutationIndex};                      \
    }                                                                                       \
  }
  TGFX_FOR_EACH_SHADER_FAMILY(TGFX_TRY_FAMILY)
  if (committed && !accepted) {
    fusedPhase = false;
    TGFX_FOR_EACH_SHADER_FAMILY(TGFX_TRY_FAMILY)
  }
#undef TGFX_TRY_FAMILY
  if (accepted) {
    return accepted;
  }
  return alphaOnly ? Fail(PermutationMatchFailure::NoMatchingFamily) : std::nullopt;
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
    if (IsProcessor<TextureEffect>(fp) && static_cast<const TextureEffect*>(fp)->isYUV()) {
      continue;
    }
    for (size_t i = 0; i < fp->numTextureSamplers(); ++i) {
      addTexture(fp->textureAt(i));
    }
  }
  return required;
}

// The uncached match: the full family walk plus the post-match validations. Both public entries
// funnel through here.
static std::optional<PermutationMatchResult> ComputeMatch(const ProgramInfo* programInfo,
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

std::optional<PermutationMatchResult> MatchPermutation(const ProgramInfo* programInfo,
                                                       PermutationMatchFailure* failure) {
  // The lookup is idempotent: the first caller runs the walk, and every later caller — the program
  // creator's lookup, a prepare-time probe — reuses the cached outcome, including the failure
  // bookkeeping, so a draw the rewrite probes matches once.
  if (programInfo->cachedMatch) {
    if (failure != nullptr) {
      *failure = programInfo->cachedMatch->failure;
    }
    return programInfo->cachedMatch->match;
  }
  auto outcome = PermutationMatchOutcome{};
  outcome.match = ComputeMatch(programInfo, &outcome.failure);
  programInfo->cachedMatch = outcome;
  if (failure != nullptr) {
    *failure = outcome.failure;
  }
  return outcome.match;
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
  }
  return "Unknown";
}

}  // namespace tgfx

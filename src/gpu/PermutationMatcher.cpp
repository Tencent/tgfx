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

#include "gpu/aot/AOTDrawFacts.h"
#include "gpu/aot/KernelTable.h"
#include <skcms.h>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include "ColorSpaceXformHelper.h"
#include "ShaderMatchHelpers.h"
#include "gpu/PermutationMatch.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/ProgramInfo.h"
#include "gpu/Swizzle.h"
#include "gpu/aot/DrawIR.h"
#include "gpu/aot/Program.h"
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

std::nullopt_t Fail(PermutationMatchFailure* failure, PermutationMatchFailure reason) {
  if (*failure == PermutationMatchFailure::None) {
    *failure = reason;
  }
  return std::nullopt;
}

bool RejectsAlphaOnlyTarget(bool servesAlphaOnly, const ProgramInfo* programInfo) {
  return !servesAlphaOnly && programInfo->getOutputSwizzle() == Swizzle::AAAA();
}

// Returns XP_NONE for EmptyXferProcessor, XP_DST_TEXTURE / XP_FRAMEBUFFER_FETCH for the
// PorterDuff modes, or XP_UNSUPPORTED for XP types no kernel can serve.
int GetXPType(const ProgramInfo* programInfo) {
  switch (ClassifyXferForMatch(programInfo->getXferProcessor())) {
    case AOTXferKind::None:
      return XP_NONE;
    case AOTXferKind::DstTexture:
      return XP_DST_TEXTURE;
    case AOTXferKind::FrameBufferFetch:
      return XP_FRAMEBUFFER_FETCH;
    case AOTXferKind::Unsupported:
      break;
  }
  return XP_UNSUPPORTED;
}

AOTXferKind ClassifyXferRecordTime(const XferProcessor* xp) {
  if (xp == nullptr || xp == EmptyXferProcessor::GetInstance()) {
    return AOTXferKind::None;
  }
  if (IsProcessor<PorterDuffXferProcessor>(xp)) {
    // The dst-texture decision was made when the op was recorded: a dst-texture processor holds
    // its proxy, a framebuffer-fetch one holds empty info. Nothing here needs an instantiation.
    return static_cast<const PorterDuffXferProcessor*>(xp)->getDstTextureInfo().textureProxy !=
                   nullptr
               ? AOTXferKind::DstTexture
               : AOTXferKind::FrameBufferFetch;
  }
  return AOTXferKind::Unsupported;
}

AOTXferKind ClassifyXferForMatch(const XferProcessor* xp) {
  if (xp == nullptr || xp == EmptyXferProcessor::GetInstance()) {
    return AOTXferKind::None;
  }
  if (IsProcessor<PorterDuffXferProcessor>(xp)) {
    if (xp->dstTextureView() != nullptr) {
      return AOTXferKind::DstTexture;
    }
    return AOTXferKind::FrameBufferFetch;
  }
  return AOTXferKind::Unsupported;
}

// Determines whether a texture-based coverage child can be served by the local-space mask path
// Reads one coverage term's texture facts the way the local-mask kernel needs them: a plain RGBA
// texture sampled with hardware tiling — no YUV, no alpha-only, no RGBAAA dual-plane, no subset,
// no shader tile math (the hardware-resolved tiled forms lower as Plain too).
static bool IsLocalMaskTexture(const AOTTextureParameters& texture) {
  return texture.samplingKind == AOTTextureSamplingKind::Plain && !texture.isYUV &&
         !texture.isAlphaOnly && !texture.hasRGBAAA && !texture.subset.has_value();
}

// Classifies coverage semantics independently of any shader's permutation layout, reading the
// draw's lowered coverage terms instead of walking coverage FPs. LocalMask is a distinct
// capability: it needs a local-space varying and sampler and therefore cannot be encoded in the
// shared three-value HAS_COVERAGE dimension.
std::optional<CoverageKind> ClassifyCoverageTerm(const AOTCoverageTerm& term) {
  if (term.kind == AOTCoverageTermKind::Analytic) {
    // A single link past the unit. A composed clip chain (more links) stays beyond the
    // single-leaf families, exactly as the FP walk's name check kept it. The link's kind carries
    // the matcher's old distinction: RectEffect's lowering maps its non-AA form onto a
    // zero-radius RRectCoverage node (hard edge, pixel-correct), so a RectCoverage node is AA by
    // construction and an RRectCoverage node covers both the real rrects and that form.
    if (term.graph.nodeCount() != 2 || !term.root.isValid() || term.root.index() != 1) {
      return std::nullopt;
    }
    auto* node = term.graph.nodeAt(term.root);
    if (node == nullptr) {
      return std::nullopt;
    }
    if (node->kind == AOTEffectKind::RectCoverage) {
      return CoverageKind::AARect;
    }
    if (node->kind == AOTEffectKind::RRectCoverage) {
      return CoverageKind::RRect;
    }
    return std::nullopt;
  }
  if (term.kind == AOTCoverageTermKind::DeviceMask) {
    // Alpha-only by construction (the lowering rejects anything else); perspective flows — the
    // chain's compile rejects it, the dedicated families sample it correctly.
    return CoverageKind::DeviceMask;
  }
  if (term.kind == AOTCoverageTermKind::Subtree) {
    // The SrcIn ShaderMaskFilter fingerprint: the coverage unit, the white input the runtime
    // feeds a single-child xfer's child, the plain local texture leaf, and the SrcIn blend that
    // maps the child onto the dst operand. Anything else under a Subtree term is a general
    // coverage subtree no dedicated family evaluates.
    if (term.graph.nodeCount() != 4 || !term.root.isValid() || term.root.index() != 3) {
      return std::nullopt;
    }
    auto* white = term.graph.nodeAt(AOTNodeID(1));
    auto* texture = term.graph.nodeAt(AOTNodeID(2));
    auto* blend = term.graph.nodeAt(term.root);
    if (white == nullptr || texture == nullptr || blend == nullptr ||
        white->kind != AOTEffectKind::GeometryWhiteInput ||
        texture->kind != AOTEffectKind::TextureSource || blend->kind != AOTEffectKind::Blend) {
      return std::nullopt;
    }
    auto* textureParams = std::get_if<AOTTextureParameters>(&texture->parameters);
    auto* blendParams = std::get_if<AOTBlendParameters>(&blend->parameters);
    if (textureParams == nullptr || blendParams == nullptr ||
        blendParams->blendMode != static_cast<int>(BlendMode::SrcIn) ||
        blendParams->childType != 0 || !IsLocalMaskTexture(*textureParams)) {
      return std::nullopt;
    }
    return CoverageKind::LocalMask;
  }
  return std::nullopt;
}

// The local-mask texture leaf of the draw's Subtree coverage term: the same SrcIn ShaderMaskFilter
// fingerprint ClassifyCoverageTerm recognizes. The leaf carries the mask transform's perspective
// fact, so a family that needs an affine mask reads it off the IR.
const AOTTextureParameters* LocalMaskCoverageLeaf(const DrawIR* ir) {
  if (ir == nullptr) {
    return nullptr;
  }
  for (const auto& term : ir->coverage) {
    if (term.kind != AOTCoverageTermKind::Subtree || term.graph.nodeCount() != 4 ||
        !term.root.isValid() || term.root.index() != 3) {
      continue;
    }
    auto* texture = term.graph.nodeAt(AOTNodeID(2));
    auto* blend = term.graph.nodeAt(term.root);
    if (texture == nullptr || blend == nullptr || texture->kind != AOTEffectKind::TextureSource ||
        blend->kind != AOTEffectKind::Blend) {
      continue;
    }
    auto* textureParams = std::get_if<AOTTextureParameters>(&texture->parameters);
    auto* blendParams = std::get_if<AOTBlendParameters>(&blend->parameters);
    if (textureParams == nullptr || blendParams == nullptr ||
        blendParams->blendMode != static_cast<int>(BlendMode::SrcIn) || blendParams->childType != 0 ||
        !IsLocalMaskTexture(*textureParams)) {
      continue;
    }
    return textureParams;
  }
  return nullptr;
}

// The device-space mask leaf of the draw's DeviceMask coverage term, with the sampler facts the
// dedicated families read off the mask processor.
const AOTTextureParameters* DeviceMaskCoverageLeaf(const DrawIR* ir) {
  if (ir == nullptr) {
    return nullptr;
  }
  for (const auto& term : ir->coverage) {
    if (term.kind == AOTCoverageTermKind::DeviceMask) {
      return &term.mask;
    }
  }
  return nullptr;
}

// Classifies the coverage stage of a ProgramInfo carrying at most one coverage FP. The no-coverage
// case is a count (bare draws lower no IR); the rest reads the carried IR's terms. The atlas glyph
// term is the geometry processor's own coverage — the matcher never saw it as a coverage FP, and
// the prepare-time synthesis adds it only for the compile — so it is skipped here.
std::optional<CoverageKind> ClassifyCoverageTerms(const ProgramInfo* programInfo) {
  auto numFP = programInfo->numFragmentProcessors();
  auto numColorFP = programInfo->numColorFragmentProcessors();
  if (numFP == numColorFP) {
    return CoverageKind::None;
  }
  if (numFP != numColorFP + 1) {
    return std::nullopt;
  }
  auto* ir = programInfo->getCarriedIR();
  if (ir == nullptr) {
    return std::nullopt;
  }
  const AOTCoverageTerm* classified = nullptr;
  for (const auto& term : ir->coverage) {
    if (term.kind == AOTCoverageTermKind::Glyph) {
      continue;
    }
    if (classified != nullptr) {
      return std::nullopt;
    }
    classified = &term;
  }
  if (classified == nullptr) {
    return CoverageKind::None;
  }
  return ClassifyCoverageTerm(*classified);
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

AOTGeometryDesc DescribeGeometryProcessor(const GeometryProcessor* gp) {
  AOTGeometryDesc desc = {};
  if (gp == nullptr) {
    return desc;
  }
  auto gpType = GetGPType(gp);
  if (gpType == 0) {
    desc.kind = AOTGeometryKind::Default;
    return desc;
  }
  const auto gpName = gp->name();
  if (gpName == "NonAARRectGeometryProcessor") {
    auto* rrectGP = static_cast<const NonAARRectGeometryProcessor*>(gp);
    desc.kind = AOTGeometryKind::NonAARRect;
    desc.hasCommonColor = rrectGP->hasCommonColor();
    desc.isStroke = rrectGP->isStroke();
    return desc;
  }
  if (gpName == "ComplexNonAARRectGeometryProcessor") {
    auto* rrectGP = static_cast<const ComplexNonAARRectGeometryProcessor*>(gp);
    desc.kind = AOTGeometryKind::ComplexNonAARRect;
    desc.hasCommonColor = rrectGP->hasCommonColor();
    desc.isStroke = rrectGP->isStroke();
    return desc;
  }
  if (gpName == "EllipseGeometryProcessor") {
    auto* ellipseGP = static_cast<const EllipseGeometryProcessor*>(gp);
    desc.kind = AOTGeometryKind::Ellipse;
    desc.hasCommonColor = ellipseGP->hasCommonColor();
    desc.isStroke = ellipseGP->isStroke();
    return desc;
  }
  if (gpName == "ComplexEllipseGeometryProcessor") {
    auto* ellipseGP = static_cast<const ComplexEllipseGeometryProcessor*>(gp);
    desc.kind = AOTGeometryKind::ComplexEllipse;
    desc.hasCommonColor = ellipseGP->hasCommonColor();
    desc.isStroke = ellipseGP->isStroke();
    return desc;
  }
  if (gpName == "HairlineLineGeometryProcessor") {
    desc.kind = AOTGeometryKind::HairlineLine;
    return desc;
  }
  if (gpName == "HairlineQuadGeometryProcessor") {
    desc.kind = AOTGeometryKind::HairlineQuad;
    return desc;
  }
  if (gpName == "MeshGeometryProcessor") {
    desc.kind = AOTGeometryKind::Mesh;
    desc.hasTexCoords = static_cast<const MeshGeometryProcessor*>(gp)->getHasTexCoords();
    return desc;
  }
  if (gpName == "RoundStrokeRectGeometryProcessor") {
    auto* strokeGP = static_cast<const RoundStrokeRectGeometryProcessor*>(gp);
    desc.kind = AOTGeometryKind::RoundStrokeRect;
    desc.hasUVMatrix = strokeGP->hasUVMatrixTransform();
    desc.hasUVPerspective = strokeGP->isUVPerspective();
    return desc;
  }
  if (gpName == "ShapeInstancedGeometryProcessor") {
    desc.kind = AOTGeometryKind::ShapeInstanced;
    desc.hasCoverage =
        static_cast<const ShapeInstancedGeometryProcessor*>(gp)->getAAType() == AAType::Coverage;
    return desc;
  }
  if (gpName == "AtlasTextGeometryProcessor") {
    desc.kind = AOTGeometryKind::AtlasText;
    desc.hasCoverage =
        static_cast<const AtlasTextGeometryProcessor*>(gp)->getAAType() == AAType::Coverage;
    return desc;
  }
  if (gpType != 1) {
    return desc;
  }
  auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
  desc.kind = AOTGeometryKind::QuadPerEdgeAA;
  desc.hasCommonColor = quadGP->hasCommonColor();
  desc.hasUVMatrix = quadGP->hasUVMatrix();
  desc.hasSubset = quadGP->getHasSubset();
  desc.hasUVPerspective = quadGP->getHasUVPerspective();
  return desc;
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

// Matches the fused pointwise-DAG kernel's per-quad subset capability against the compiled program.
// A quad's per-quad subset (SrcRectConstraint::Strict) reaches every strict texture of the
// runtime program as the GP's vTexSubset varying, which the GP transforms with the program's
// first coordinate transform. The HAS_SUBSET chain variant computes it with leaf 0's transform and
// clamps leaf 0 alone, so it serves the draw only when leaf 0 is the one strict texture: a plain
// sampled leaf (the tiled and device-space leaf forms clamp differently) with every other leaf and
// tiling recipe non-strict. A projective subset stays on the runtime route, as for
// QuadTextureFillShader: the precompiled vertex stage transforms the subset corners affinely.
bool IsChainQuadSubsetServable(const QuadPerEdgeAAGeometryProcessor* quadGP,
                               const AOTProgram& program) {
  if (program.samplerChildCount == 0 || quadGP->getHasUVPerspective()) {
    return false;
  }
  for (const auto& tiled : program.tiledLeaves) {
    if (tiled.leafIndex == 0 || tiled.recipe.strict) {
      return false;
    }
  }
  for (const auto& deviceLeaf : program.deviceLeaves) {
    if (deviceLeaf.leafIndex == 0) {
      return false;
    }
  }
  for (size_t index = 0; index < program.samplerChildCount; ++index) {
    const auto& binding = program.bindings[index];
    if (binding.kind != AOTBinding::Kind::Leaf) {
      // A LUT or padding child samples non-strict: it can never be the strict leaf 0 the variant
      // computes the subset with, and behind leaf 0 it is exactly what the variant needs.
      if (index == 0) {
        return false;
      }
      continue;
    }
    bool strict = binding.args.constraint == SrcRectConstraint::Strict;
    if (strict != (index == 0) || (index == 0 && binding.uvMatrix.hasPerspective())) {
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

bool SupportedFillKernelSteps(const ColorSpaceXformSteps* steps) {
  if (steps == nullptr) {
    return false;
  }
  if (steps->flags.linearize) {
    int srcIdx = TFTypeToIndex(gfx::skcms_TransferFunction_getType(
        reinterpret_cast<const gfx::skcms_TransferFunction*>(&steps->srcTransferFunction)));
    if (srcIdx < 0) {
      return false;
    }
  }
  if (steps->flags.encode) {
    int dstIdx = TFTypeToIndex(gfx::skcms_TransferFunction_getType(
        reinterpret_cast<const gfx::skcms_TransferFunction*>(&steps->dstTransferFunctionInverse)));
    if (dstIdx < 0) {
      return false;
    }
  }
  return true;
}

// The IR form of a blur kernel's servable child (the old FP-walk IsServableBlurChild's
// replacement): the opaque node's input must be a single texture leaf whose per-tap sampling the
// kernels can express. A plain leaf must be Clamp — the kernels clamp plain-child taps to the
// subset (edge stretch), which is only the Clamp tap semantics: a Decal child must return
// transparent outside its subset, a drop shadow's blurred source is exactly that. A tiled leaf
// must be non-perspective and limited to the ShaderMode values tiled_sample.inc implements.
static bool IsServableBlurChildIR(const AOTTextureParameters& child) {
  if (child.samplingKind == AOTTextureSamplingKind::Plain) {
    return child.samplerState.tileModeX == TileMode::Clamp &&
           child.samplerState.tileModeY == TileMode::Clamp;
  }
  if (child.samplingKind == AOTTextureSamplingKind::Tiled) {
    if (child.hasPerspective) {
      return false;
    }
    if (!child.tiledRecipe.has_value()) {
      return false;
    }
    return TiledModeSupported(static_cast<int>(child.tiledRecipe->shaderModeX)) &&
           TiledModeSupported(static_cast<int>(child.tiledRecipe->shaderModeY));
  }
  return false;
}

// Reads the carried IR's color graph for an opaque-kernel draw (P4.1's barrier node): the color
// root must be the named kernel's node. The blur kernels require their input to be a single
// servable texture leaf (returned alongside); the refraction sits directly on the geometry color,
// with its variant dimensions on the node. Nullopt when the info carries no IR or the shape does
// not fit — the family walks no processor tree anymore, the node IS the contract.
std::optional<OpaqueKernelIRShape> ReadOpaqueKernelIRShape(const DrawIR* ir, int kernelKind,
                                                           bool requiresTextureChild) {
  if (ir == nullptr) {
    return std::nullopt;
  }
  const auto& graph = ir->color;
  const auto* root = graph.nodeAt(graph.root());
  if (root == nullptr || root->kind != AOTEffectKind::OpaqueKernel || root->inputs.size() != 1) {
    return std::nullopt;
  }
  auto* kernel = std::get_if<AOTOpaqueKernelParameters>(&root->parameters);
  if (kernel == nullptr || kernel->kernelKind != kernelKind) {
    return std::nullopt;
  }
  OpaqueKernelIRShape shape = {};
  shape.kernel = kernel;
  if (!requiresTextureChild) {
    return shape;
  }
  const auto* child = graph.nodeAt(root->inputs[0]);
  if (child == nullptr || child->kind != AOTEffectKind::TextureSource) {
    return std::nullopt;
  }
  auto* texture = std::get_if<AOTTextureParameters>(&child->parameters);
  if (texture == nullptr || !IsServableBlurChildIR(*texture)) {
    return std::nullopt;
  }
  shape.childTexture = texture;
  return shape;
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

// Per-family gate of the walk (see MatchPermutationImpl for the switch semantics): the two
// diagnostic switches exclude their half of the family list, decompositionEnabled gating the
// interpreter and dedicatedKernelsEnabled every dedicated family.
template <typename Family>
static bool FamilyWalkEnabled(bool allowDedicated, bool allowInterpreter) {
  return std::is_same_v<Family, PointwiseChainShader> ? allowInterpreter : allowDedicated;
}

static std::optional<PermutationMatchResult> MatchPermutationImpl(
    const ProgramInfo* programInfo, PermutationMatchFailure* failure) {
  // OpenGL is served from the bundle profile matching the context (desktop GL or the GLES
  // family: GLES, WebGL, SwiftShader). The precompiled kernels only declare 2D samplers. Desktop GL
  // rectangle textures are copied into 2D textures where they enter (ProxyProvider::
  // wrapExternalTexture, CGLHardwareTexture, and the dst copy of a rectangle render target), so a
  // rectangle sampler only reaches this gate through an unconverted view. External (OES) textures
  // (Android SurfaceTexture and YUV hardware buffers) are not converted yet: with a bundle loaded
  // there is no runtime route, so their draws are dropped and counted as droppedDraws.
  if (programInfo->backend() == Backend::OpenGL &&
      (!programInfo->glProfileMatchesCache() || !programInfo->samplersAre2D())) {
    return Fail(failure, PermutationMatchFailure::GlGateRejected);
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
  // The extra Match calls are pure shape checks, each writing its rejection into a local slot,
  // so the walk's own failure accounting is unaffected. The audit runs once per distinct program
  // shape: repeated prepares of the same shape re-audit nothing.
  {
    static thread_local std::unordered_set<BytesKey, BytesKeyHasher> auditedProgramKeys;
    if (auditedProgramKeys.insert(programInfo->programKeyForDiagnostics()).second) {
      int acceptors = 0;
#define TGFX_AUDIT_FAMILY(Family)                                                               \
  {                                                                                             \
    PermutationMatchFailure auditFailure = PermutationMatchFailure::None;                       \
    if (auto auditResult = Family::Match(programInfo, &auditFailure)) {                         \
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
    }                                                                                           \
  }
      TGFX_FOR_EACH_SHADER_FAMILY(TGFX_AUDIT_FAMILY)
#undef TGFX_AUDIT_FAMILY
    }
  }
#endif

  // The two diagnostic switches keep the retired walk's semantics: dedicatedKernelsEnabled gates
  // every dedicated family (the whole executor table), decompositionEnabled the interpreter.
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
  // The dedicated families dispatch through the executor table: the rows, in the families' walk
  // order, each one the family's own ExtractFacts pipeline, with one facts extraction replacing
  // every family re-deriving the same facts from the processor tree. The gates keep the retired
  // walk's semantics — alpha-only rejection is a row predicate (the ExtractFacts bodies), and
  // dedicatedKernelsEnabled gates the whole table.
  if (allowDedicated) {
    if (auto table = KernelTableLookup(ExtractAOTDrawFacts(programInfo), failure)) {
      return PermutationMatchResult{table->row->family, table->variant.vertPermutationIndex,
                                    table->variant.fragPermutationIndex};
    }
  }
  // The interpreter stays the walk's documented fallback, tried last.
  if (allowInterpreter && (!alphaOnly || PointwiseChainShader::ServesAlphaOnly)) {
    if (auto result = PointwiseChainShader::Match(programInfo, failure)) {
      return PermutationMatchResult{"PointwiseChainShader", result->vertPermutationIndex,
                                    result->fragPermutationIndex};
    }
  }
  return alphaOnly ? Fail(failure, PermutationMatchFailure::NoMatchingFamily) : std::nullopt;
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
  // Only RGBA and alpha-only (AAAA, Swizzle::ForWrite(ALPHA_8)) render targets are supported. Any
  // other write swizzle would need a mapping the precompiled shaders do not implement.
  auto outputSwizzle = programInfo->getOutputSwizzle();
  if (outputSwizzle != Swizzle::RGBA() && outputSwizzle != Swizzle::AAAA()) {
    if (failure != nullptr) {
      *failure = PermutationMatchFailure::UnsupportedOutputSwizzle;
    }
    return std::nullopt;
  }
  auto matchFailure = PermutationMatchFailure::None;
  auto result = MatchPermutationImpl(programInfo, &matchFailure);
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
  // No rejecting site recorded a reason: the generic no-matching-rule verdict. (A successful
  // match above reports None.)
  if (!result && matchFailure == PermutationMatchFailure::None) {
    matchFailure = PermutationMatchFailure::NoMatchingRule;
  }
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

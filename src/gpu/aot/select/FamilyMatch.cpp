/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/select/FamilyMatch.h"
#include <array>
#include <cstdlib>
#include "core/utils/Log.h"
#include "gpu/YUVFormat.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"
#include "gpu/resources/YUVTextureView.h"
#include "gpu/shaders/ChainInputs.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/FamilyDomains.h"
#include "gpu/shaders/contract/DrawFacts.h"

namespace tgfx {

/// The table indices the match rules address their families by. Each is a constant expression, so
/// a name that drifts from the family table fails the build here instead of silently matching
/// another family's row at runtime.
constexpr uint16_t SolidColorIndex = FamilyIndexOf("SolidColor");
constexpr uint16_t MaskFillIndex = FamilyIndexOf("MaskFill");
constexpr uint16_t TiledTextureFillIndex = FamilyIndexOf("TiledTextureFill");
constexpr uint16_t QuadColorIndex = FamilyIndexOf("QuadColor");
constexpr uint16_t RRectIndex = FamilyIndexOf("RRect");
constexpr uint16_t MeshIndex = FamilyIndexOf("Mesh");
constexpr uint16_t ShapeInstancedIndex = FamilyIndexOf("ShapeInstanced");
constexpr uint16_t ComplexRRectIndex = FamilyIndexOf("ComplexRRect");
constexpr uint16_t HairlineLineIndex = FamilyIndexOf("HairlineLine");
constexpr uint16_t HairlineQuadIndex = FamilyIndexOf("HairlineQuad");
constexpr uint16_t EllipseIndex = FamilyIndexOf("Ellipse");
constexpr uint16_t ComplexEllipseIndex = FamilyIndexOf("ComplexEllipse");
constexpr uint16_t RoundStrokeRectIndex = FamilyIndexOf("RoundStrokeRect");
constexpr uint16_t QuadTextureIndex = FamilyIndexOf("QuadTexture");
constexpr uint16_t YUVTextureIndex = FamilyIndexOf("YUVTexture");
constexpr uint16_t AtlasTextIndex = FamilyIndexOf("AtlasText");
constexpr uint16_t ShapeInstancedTextureCoverageIndex =
    FamilyIndexOf("ShapeInstancedTextureCoverage");
constexpr uint16_t GaussianBlurIndex = FamilyIndexOf("GaussianBlur");
constexpr uint16_t GlassUDFTentBlurIndex = FamilyIndexOf("GlassUDFTentBlur");
constexpr uint16_t GlassRefractionIndex = FamilyIndexOf("GlassRefraction");
constexpr uint16_t GlassRefractionQuadIndex = FamilyIndexOf("GlassRefractionQuad");
constexpr uint16_t ChainIndex = FamilyIndexOf("Chain");

/// Encodes the matched family's variant: the match rule supplies the geometry choice and the
/// free dim values in flattened declaration order, and EncodeVariant fills the pinned values and
/// composes the ordinal by the enumeration's own radix rule. False means the draw stays
/// unmatched (the value range, the free-value count or the allowed filter rejected it).
bool EncodeMatch(uint16_t family, uint8_t geometry, const std::vector<uint8_t>& freeValues,
                 Variant* variant) {
  auto encoded = EncodeVariant(family, geometry, freeValues);
  if (!encoded.has_value()) {
    return false;
  }
  *variant = *encoded;
  return true;
}

namespace {

/// The SolidColor family's rule, carried over from the former SolidColorFillShader: the
/// DefaultGP's rect fill with no fragment processor, or one direct analytic clip (an AA rect or
/// rounded rect) riding the runtime clip contract. Any xfer, alpha-only targets included.
/// Slot layout: Frame(0), RectGeometry(1), AnalyticClip(2), Xfer(3: HAS_XP). No free dims in the
/// geometry or the body, so the ordinal is HAS_XP alone.
bool MatchSolidColor(const ProgramInfo* /*info*/, const DrawShape& shape,
                     FamilyMatchResult* result) {
  if (shape.geometry.kind != GeometryKind::Default) {
    return false;
  }
  const bool noProcessorsOrDirectClip =
      shape.fragmentCount == 0 || (shape.colorFPCount == 0 && (shape.coverage == Coverage::AARect ||
                                                               shape.coverage == Coverage::RRect));
  if (!noProcessorsOrDirectClip || shape.xpType() < XP_NONE) {
    return false;
  }
  const uint8_t hasXp = static_cast<uint8_t>(shape.xpType());
  return EncodeMatch(SolidColorIndex, 0, {hasXp}, &result->variant);
}

/// The MaskFill family's rule, carried over from the former MaskFillShader: the DefaultGP's rect
/// fill whose sole fragment processor is the rasterized alpha-only shape mask. The mask's
/// sampler facts come off the IR's sole coverage term, so the acceptance reads the carried IR.
/// Slot layout: Frame(0), RectGeometry(1), MaskTexture(2), Xfer(3: HAS_XP).
bool MatchMaskFill(const ProgramInfo* /*info*/, const DrawShape& shape, FamilyMatchResult* result) {
  if (shape.geometry.kind != GeometryKind::Default) {
    return false;
  }
  if (shape.colorFPCount != 0 || shape.fragmentCount != 1) {
    return false;
  }
  const auto* mask = shape.ir != nullptr && shape.ir->coverage.size() == 1
                         ? BareTextureCoverageLeaf(shape.ir->coverage[0])
                         : nullptr;
  if (mask == nullptr || mask->samplingKind != AOTTextureSamplingKind::Plain ||
      !mask->isAlphaOnly || mask->subset.has_value() || mask->isYUV || mask->hasRGBAAA ||
      mask->textureProxy == nullptr || mask->textureProxy->getTextureView() == nullptr) {
    return false;
  }
  if (shape.xpType() < XP_NONE) {
    return false;
  }
  const uint8_t hasXp = static_cast<uint8_t>(shape.xpType());
  if (!EncodeMatch(MaskFillIndex, 0, {hasXp}, &result->variant)) {
    return false;
  }
  result->maskLeaf = mask;
  return true;
}

/// The TiledTextureFill family's rule, carried over from the former TiledTextureFillShader: a
/// position-only rect or quad whose sole color processor is the IR's tiled texture leaf (no
/// operators above it). The legacy QuadTextureFillShader still wins the Plain-leaf Quad draws it
/// accepts (the legacy dedicated table runs first); this family serves the DefaultGP form and the
/// Quad draws with a Tiled leaf. FULL and PRESENT are pinned to 1, so the flattened dim order
/// stays [FULL=1, PRESENT=1, HAS_XP] and the ordinal is geometry + 2 * HAS_XP.
bool MatchTiledTextureFill(const ProgramInfo* /*info*/, const DrawShape& shape,
                           FamilyMatchResult* result) {
  if (!shape.positionOnlyRect()) {
    return false;
  }
  if (shape.fragmentCount != 1 || shape.xpType() < XP_NONE || shape.ir == nullptr) {
    return false;
  }
  AOTChainBuilder::AOTTextureTailShape tail = {};
  if (!AOTChainBuilder::CollectTextureTail(shape.ir->color, &tail) || !tail.ops.empty()) {
    return false;
  }
  const auto* leaf = tail.leaf;
  if (leaf->samplingKind != AOTTextureSamplingKind::Tiled || leaf->hasPerspective ||
      !leaf->tiledRecipe.has_value()) {
    return false;
  }
  if (!TiledFillModeSupported(static_cast<int>(leaf->tiledRecipe->shaderModeX)) ||
      !TiledFillModeSupported(static_cast<int>(leaf->tiledRecipe->shaderModeY))) {
    return false;
  }
  const uint8_t geometry = shape.geometry.kind == GeometryKind::Default ? 0 : 1;
  const uint8_t hasXp = static_cast<uint8_t>(shape.xpType());
  // The pinned dims (FULL and PRESENT at 1) are filled by EncodeVariant from the declaration;
  // hand-rolling them at zero made PRESENT read as "module absent" and skip every tiled uniform
  // (the c8c9c9e6d bug this helper exists to prevent).
  if (!EncodeMatch(TiledTextureFillIndex, geometry, {hasXp}, &result->variant)) {
    return false;
  }
  result->tiledLeaf = leaf;
  return true;
}

/// The QuadColor family's rule, carried over from the former QuadColorFillShader: the
/// QuadPerEdgeAA rect with no fragment processor, or one coverage processor (an analytic rect or
/// rounded-rect clip, or a device-space mask). Slot layout: Frame(0), QuadColorGeometry(1),
/// DeviceMask(2: PRESENT), AnalyticClip(3), Xfer(4: HAS_XP). The ordinal is PRESENT + 2 * HAS_XP.
bool MatchQuadColor(const ProgramInfo* /*info*/, const DrawShape& shape,
                    FamilyMatchResult* result) {
  if (shape.geometry.kind != GeometryKind::QuadPerEdgeAA) {
    return false;
  }
  uint8_t present = 0;
  if (shape.fragmentCount != 0) {
    if (shape.colorFPCount != 0 || !shape.coverage.has_value()) {
      return false;
    }
    if (*shape.coverage == Coverage::DeviceMask) {
      present = 1;
    } else if (*shape.coverage != Coverage::AARect && *shape.coverage != Coverage::RRect) {
      return false;
    }
  }
  if (shape.xpType() < XP_NONE) {
    return false;
  }
  const uint8_t hasXp = static_cast<uint8_t>(shape.xpType());
  return EncodeMatch(QuadColorIndex, 0, {present, hasXp}, &result->variant);
}

/// The RRect family's rule, carried over from the former NonAARRectFillShader: the non-AA
/// rounded rect, at most one fragment (a sole analytic clip riding the runtime contract, or the
/// single-tap tiled fill), never alpha-only, any xfer.
bool MatchRRect(const ProgramInfo* /*info*/, const DrawShape& shape, FamilyMatchResult* result) {
  if (shape.geometry.kind != GeometryKind::NonAARRect || shape.alphaOnlyTarget) {
    return false;
  }
  if (shape.fragmentCount > 1) {
    return false;
  }
  const bool clipOnly = shape.colorFPCount == 0 &&
                        (shape.coverage == Coverage::AARect || shape.coverage == Coverage::RRect);
  const AOTTextureParameters* tiledLeaf = nullptr;
  if (shape.fragmentCount == 1 && !clipOnly) {
    if (shape.ir == nullptr) {
      return false;
    }
    AOTChainBuilder::AOTTextureTailShape tail = {};
    if (!AOTChainBuilder::CollectTextureTail(shape.ir->color, &tail) || !tail.ops.empty()) {
      return false;
    }
    tiledLeaf = tail.leaf;
    if (tiledLeaf->samplingKind != AOTTextureSamplingKind::Tiled || tiledLeaf->hasPerspective ||
        !tiledLeaf->tiledRecipe.has_value()) {
      return false;
    }
    if (!TiledModeSupported(static_cast<int>(tiledLeaf->tiledRecipe->shaderModeX)) ||
        !TiledModeSupported(static_cast<int>(tiledLeaf->tiledRecipe->shaderModeY))) {
      return false;
    }
    // The textured form never combines with stroke (the family's allowed rule).
    if (shape.geometry.isStroke) {
      return false;
    }
  }
  if (shape.xpType() < XP_NONE) {
    return false;
  }
  // Slot layout: Frame(0), RRectGeometry(1: STROKE), TiledTexture(2: FULL pinned, PRESENT),
  // AnalyticClip(3), Xfer(4: HAS_XP). The ordinal encodes [STROKE, PRESENT, HAS_XP].
  const uint8_t stroke = shape.geometry.isStroke ? 1 : 0;
  const uint8_t present = tiledLeaf != nullptr ? 1 : 0;
  const uint8_t hasXp = static_cast<uint8_t>(shape.xpType());
  // The free values in flattened order: [STROKE, PRESENT, HAS_XP] (FULL is pinned to 0).
  if (!EncodeMatch(RRectIndex, 0, {stroke, present, hasXp}, &result->variant)) {
    return false;
  }
  result->tiledLeaf = tiledLeaf;
  return true;
}

/// The gradient form's color graph: the geometry color lowered as the input, then the single
/// GradientSource over it. Its parameters carry the layout branch, the boundary colors and the
/// colorizer endpoints the Gradient module writes.
const AOTGradientParameters* SoleGradientLeaf(const AOTEffectGraph& graph) {
  if (graph.nodeCount() != 2 || !graph.root().isValid() || graph.root().index() != 1) {
    return nullptr;
  }
  auto* node = graph.nodeAt(graph.root());
  if (node == nullptr || node->kind != AOTEffectKind::GradientSource) {
    return nullptr;
  }
  return std::get_if<AOTGradientParameters>(&node->parameters);
}

/// The Mesh family's rule, carried over from the former MeshFillShader: the mesh geometry with no
/// fragment processor and any xfer. The flattened dims are the geometry's TEX_COORDS then the
/// Xfer's HAS_XP, so the ordinal is TEX_COORDS + 2 * HAS_XP.
bool MatchMesh(const ProgramInfo* /*info*/, const DrawShape& shape, FamilyMatchResult* result) {
  if (shape.geometry.kind != GeometryKind::Mesh || shape.fragmentCount != 0 ||
      shape.xpType() < XP_NONE) {
    return false;
  }
  const uint8_t texCoords = shape.geometry.hasTexCoords ? 1 : 0;
  const uint8_t hasXp = static_cast<uint8_t>(shape.xpType());
  return EncodeMatch(MeshIndex, 0, {texCoords, hasXp}, &result->variant);
}

/// The ShapeInstanced family's rule, carried over from the former ShapeInstancedFillShader: the
/// instanced path-triangle geometry with no fragment processor and any xfer. VERTEX_COLOR stays
/// at 0 (the reserved stride slot is never read; the allowed filter drops its 1 value), so the
/// free values are [0, HAS_XP] and the ordinal is 2 * HAS_XP. The hand-rolled ordinal this rule
/// carried before EncodeVariant was HAS_XP alone — an encoding outside the family's reachable
/// set for every blended xfer.
bool MatchShapeInstanced(const ProgramInfo* /*info*/, const DrawShape& shape,
                         FamilyMatchResult* result) {
  if (shape.geometry.kind != GeometryKind::ShapeInstanced || shape.fragmentCount != 0 ||
      shape.xpType() < XP_NONE) {
    return false;
  }
  const uint8_t hasXp = static_cast<uint8_t>(shape.xpType());
  return EncodeMatch(ShapeInstancedIndex, 0, {0, hasXp}, &result->variant);
}

/// The ShapeInstancedTextureCoverage family's rule, carried over from the former
/// ShapeInstancedTextureCoverageShader: the instanced path triangles in their non-AA form (no AA
/// coverage channel), the color either the instance color alone or one single-interval clamped
/// gradient over it, and the coverage a plain non-YUV texture optionally accompanied by a bare
/// analytic clip (the clip rides the runtime contract uniforms, so both forms select the same
/// variant). The legacy kernel served XP_NONE only. The flattened dims are [VERTEX_COLOR,
/// GRADIENT, HAS_XP] with the first and last pinned to 0, so the ordinal is 2 * GRADIENT.
bool MatchShapeInstancedTextureCoverage(const ProgramInfo* /*info*/, const DrawShape& shape,
                                        FamilyMatchResult* result) {
  if (shape.geometry.kind != GeometryKind::ShapeInstanced || shape.geometry.hasCoverage ||
      shape.xpType() != XP_NONE || shape.ir == nullptr) {
    return false;
  }
  const AOTGradientParameters* gradient = nullptr;
  if (shape.colorFPCount == 1) {
    gradient = SoleGradientLeaf(shape.ir->color);
    if (gradient == nullptr || gradient->colorizerKind != 0 || gradient->hasPerspective) {
      return false;
    }
  } else if (shape.colorFPCount != 0) {
    return false;
  }
  const AOTTextureParameters* coverage = nullptr;
  if (shape.fragmentCount == shape.colorFPCount + 1) {
    // The lone term is the texture leaf: record-time, or the rasterized shape's prepare-grown
    // mask the lowering appended at prepare.
    coverage =
        shape.ir->coverage.size() == 1 ? BareTextureCoverageLeaf(shape.ir->coverage[0]) : nullptr;
  } else if (shape.fragmentCount == shape.colorFPCount + 2) {
    // The texture-plus-clip form: two single terms in either order, one of them the analytic clip.
    if (shape.ir->coverage.size() != 2) {
      return false;
    }
    const auto first = ClassifyCoverageTerm(shape.ir->coverage[0]);
    const auto second = ClassifyCoverageTerm(shape.ir->coverage[1]);
    const bool firstIsClip = first && (*first == Coverage::AARect || *first == Coverage::RRect);
    const bool secondIsClip = second && (*second == Coverage::AARect || *second == Coverage::RRect);
    if (firstIsClip == secondIsClip) {
      return false;
    }
    coverage = BareTextureCoverageLeaf(shape.ir->coverage[firstIsClip ? 1 : 0]);
  } else {
    return false;
  }
  if (coverage == nullptr || coverage->samplingKind != AOTTextureSamplingKind::Plain ||
      coverage->isYUV || coverage->hasRGBAAA || coverage->textureProxy == nullptr ||
      coverage->textureProxy->getTextureView() == nullptr) {
    return false;
  }
  const uint8_t gradientValue = gradient != nullptr ? 1 : 0;
  // The free values in flattened order: [VERTEX_COLOR = 0, GRADIENT, HAS_XP = 0] (the allowed
  // filter drops both non-zero ends).
  if (!EncodeMatch(ShapeInstancedTextureCoverageIndex, 0, {0, gradientValue, 0},
                   &result->variant)) {
    return false;
  }
  result->maskLeaf = coverage;
  result->gradient = gradient;
  return true;
}

/// The GaussianBlur family's rule, carried over from the former GaussianBlur1DShader.
bool MatchGaussianBlur(const ProgramInfo* /*info*/, const DrawShape& shape,
                       FamilyMatchResult* result) {
  if (shape.geometry.kind != GeometryKind::Default &&
      shape.geometry.kind != GeometryKind::QuadPerEdgeAA) {
    return false;
  }
  if (shape.colorFPCount != 1) {
    return false;
  }
  if (!shape.coverage.has_value() || shape.coverage == Coverage::LocalMask) {
    return false;
  }
  if (shape.xpType() < XP_NONE) {
    return false;
  }
  auto irShape = ReadOpaqueKernelIRShape(shape.ir, AOTOpaqueKernel::GaussianBlur1D, true);
  if (!irShape) {
    return false;
  }
  if (irShape->kernel->sigma < 1 || irShape->kernel->sigma > 10) {
    return false;
  }
  const uint8_t hasXp = static_cast<uint8_t>(shape.xpType());
  if (!EncodeMatch(GaussianBlurIndex, 0, {hasXp}, &result->variant)) {
    return false;
  }
  result->blurKernel = irShape->kernel;
  result->blurChild = irShape->childTexture;
  return true;
}

/// The GlassUDFTentBlur family's rule, carried over from the former GlassUDFTentBlurShader: the
/// position-only rect providers with one color processor, a classifiable coverage, and the carried
/// IR's tent-blur node over one servable texture child; the fixed loop bound caps the radius.
bool MatchGlassUDFTentBlur(const ProgramInfo* /*info*/, const DrawShape& shape,
                           FamilyMatchResult* result) {
  if (shape.geometry.kind != GeometryKind::Default &&
      shape.geometry.kind != GeometryKind::QuadPerEdgeAA) {
    return false;
  }
  if (shape.colorFPCount != 1) {
    return false;
  }
  if (!shape.coverage.has_value() || shape.coverage == Coverage::LocalMask) {
    return false;
  }
  if (shape.xpType() < XP_NONE) {
    return false;
  }
  auto irShape = ReadOpaqueKernelIRShape(shape.ir, AOTOpaqueKernel::GlassUDFTentBlur, true);
  if (!irShape || irShape->kernel->maxRadius > 64) {
    return false;
  }
  const uint8_t hasXp = static_cast<uint8_t>(shape.xpType());
  if (!EncodeMatch(GlassUDFTentBlurIndex, 0, {hasXp}, &result->variant)) {
    return false;
  }
  result->blurKernel = irShape->kernel;
  result->blurChild = irShape->childTexture;
  return true;
}

/// The glass families' shared rule, carried over from the former GlassRefractionShader: one color
/// processor whose carried IR roots at the refraction opaque-kernel node with a known geometry
/// kind and an affine background coordinate, never alpha-only, any xfer. The legacy GP_KIND
/// dimension becomes the two families' geometries: the ellipse form requires the common color
/// (the refraction output replaces the color entirely, so a per-vertex color would quantize
/// through the UByte4 attribute for nothing), the quad form requires the uv-matrix layout (the
/// quad vertex hook feeds the raw position to the background transform; a subset-carrying or
/// projective quad keeps the fallback route). Only the quad family carries the local shape mask
/// (the former runtime HasLocalMask branch): the sole coverage processor classifies as LocalMask
/// with an affine mask transform. Slot layout, both: Frame(0), geometry(1), GlassRefraction(2:
/// DISPERSION, SHAPE), [GlassLocalMask(3),] AnalyticClip, Xfer. The flattened dims are
/// [DISPERSION, SHAPE, HAS_XP], so the ordinal is DISPERSION + 2 * SHAPE + 6 * HAS_XP — 18
/// variants per family, the legacy kernel's 36 across the two.
bool MatchGlassRefraction(const ProgramInfo* /*info*/, const DrawShape& shape, GeometryKind kind,
                          uint16_t family, FamilyMatchResult* result) {
  if (shape.geometry.kind != kind || shape.alphaOnlyTarget) {
    return false;
  }
  if (kind == GeometryKind::Ellipse) {
    if (!shape.geometry.hasCommonColor) {
      return false;
    }
  } else {
    if (!shape.geometry.hasCommonColor || !shape.geometry.hasUVMatrix || shape.geometry.hasSubset ||
        shape.geometry.hasUVPerspective) {
      return false;
    }
  }
  if (shape.colorFPCount != 1 || shape.xpType() < XP_NONE) {
    return false;
  }
  const AOTTextureParameters* mask = nullptr;
  if (shape.fragmentCount != 1) {
    // The quad form's optional local shape mask (a SrcIn ShaderMaskFilter over a
    // hardware-tiled RGBA texture), the only coverage the legacy kernel took.
    mask = LocalMaskCoverageLeaf(shape.ir);
    if (kind != GeometryKind::QuadPerEdgeAA || shape.fragmentCount != 2 ||
        !shape.coverage.has_value() || shape.coverage != Coverage::LocalMask || mask == nullptr ||
        mask->hasPerspective) {
      return false;
    }
  }
  auto irShape = ReadOpaqueKernelIRShape(shape.ir, AOTOpaqueKernel::GlassRefraction,
                                         /*requiresTextureChild=*/false);
  if (!irShape || !irShape->kernel->geometryKind.has_value() || irShape->kernel->hasPerspective) {
    return false;
  }
  const uint8_t dispersion = irShape->kernel->dispersionOn ? 1 : 0;
  const uint8_t shapeKind = static_cast<uint8_t>(*irShape->kernel->geometryKind);
  const uint8_t hasXp = static_cast<uint8_t>(shape.xpType());
  if (!EncodeMatch(family, 0, {dispersion, shapeKind, hasXp}, &result->variant)) {
    return false;
  }
  result->glassKernel = irShape->kernel;
  result->maskLeaf = mask;
  return true;
}

/// The shared glass rule's two table entries: the legacy kernel's GP_KIND dimension is the two
/// families' geometries, so each family row gets its own one-line entry.
bool MatchGlassRefractionEllipse(const ProgramInfo* /*info*/, const DrawShape& shape,
                                 FamilyMatchResult* result) {
  return MatchGlassRefraction(/*info=*/nullptr, shape, GeometryKind::Ellipse, GlassRefractionIndex,
                              result);
}

bool MatchGlassRefractionQuad(const ProgramInfo* /*info*/, const DrawShape& shape,
                              FamilyMatchResult* result) {
  return MatchGlassRefraction(/*info=*/nullptr, shape, GeometryKind::QuadPerEdgeAA,
                              GlassRefractionQuadIndex, result);
}

/// The ComplexRRect family's rule, carried over from the former ComplexNonAARRectFillShader: the
/// per-corner-radius rounded rect with no fragment processor and any xfer. STROKE is the geometry's
/// own flag, so the ordinal is STROKE + 2 * HAS_XP.
bool MatchComplexRRect(const ProgramInfo* /*info*/, const DrawShape& shape,
                       FamilyMatchResult* result) {
  if (shape.geometry.kind != GeometryKind::ComplexNonAARRect || shape.fragmentCount != 0 ||
      shape.xpType() < XP_NONE) {
    return false;
  }
  const uint8_t stroke = shape.geometry.isStroke ? 1 : 0;
  const uint8_t hasXp = static_cast<uint8_t>(shape.xpType());
  return EncodeMatch(ComplexRRectIndex, 0, {stroke, hasXp}, &result->variant);
}

/// The hairline families' shared rule, carried over from the former HairlineLineShader and
/// HairlineQuadShader: the hairline geometry with no fragment processor, or one that is a bare
/// analytic rect/rounded-rect clip (no color processors). Only HAS_XP varies, so the ordinal is
/// HAS_XP alone.
bool MatchHairline(const ProgramInfo* /*info*/, const DrawShape& shape, GeometryKind kind,
                   uint16_t family, FamilyMatchResult* result) {
  if (shape.geometry.kind != kind) {
    return false;
  }
  const bool directClip = shape.colorFPCount == 0 &&
                          (shape.coverage == Coverage::AARect || shape.coverage == Coverage::RRect);
  if (shape.fragmentCount != 0 && !directClip) {
    return false;
  }
  if (shape.xpType() < XP_NONE) {
    return false;
  }
  const uint8_t hasXp = static_cast<uint8_t>(shape.xpType());
  return EncodeMatch(family, 0, {hasXp}, &result->variant);
}

/// The shared hairline rule's two table entries, one per family row.
bool MatchHairlineLine(const ProgramInfo* /*info*/, const DrawShape& shape,
                       FamilyMatchResult* result) {
  return MatchHairline(/*info=*/nullptr, shape, GeometryKind::HairlineLine, HairlineLineIndex,
                       result);
}

bool MatchHairlineQuad(const ProgramInfo* /*info*/, const DrawShape& shape,
                       FamilyMatchResult* result) {
  return MatchHairline(/*info=*/nullptr, shape, GeometryKind::HairlineQuad, HairlineQuadIndex,
                       result);
}

/// The Ellipse family's rule, carried over from the former EllipseFillShader: the ellipse geometry
/// with no color processor, a coverage that is anything but a local mask, and any xfer. DeviceMask
/// carries PRESENT, so the ordinal is PRESENT + 2 * HAS_XP.
bool MatchEllipse(const ProgramInfo* /*info*/, const DrawShape& shape, FamilyMatchResult* result) {
  if (shape.geometry.kind != GeometryKind::Ellipse || shape.colorFPCount != 0 ||
      !shape.coverage.has_value() || *shape.coverage == Coverage::LocalMask ||
      shape.xpType() < XP_NONE) {
    return false;
  }
  const uint8_t present = *shape.coverage == Coverage::DeviceMask ? 1 : 0;
  const uint8_t hasXp = static_cast<uint8_t>(shape.xpType());
  return EncodeMatch(EllipseIndex, 0, {present, hasXp}, &result->variant);
}

/// The ComplexEllipse family's rule, carried over from the former ComplexEllipseFillShader: the
/// complex-ellipse geometry with no color processor, a coverage that is anything but a local mask,
/// and any xfer. The flattened dims are STROKE, PRESENT, HAS_XP, so the ordinal is
/// STROKE + 2 * PRESENT + 4 * HAS_XP.
bool MatchComplexEllipse(const ProgramInfo* /*info*/, const DrawShape& shape,
                         FamilyMatchResult* result) {
  if (shape.geometry.kind != GeometryKind::ComplexEllipse || shape.colorFPCount != 0 ||
      !shape.coverage.has_value() || *shape.coverage == Coverage::LocalMask ||
      shape.xpType() < XP_NONE) {
    return false;
  }
  const uint8_t stroke = shape.geometry.isStroke ? 1 : 0;
  const uint8_t present = *shape.coverage == Coverage::DeviceMask ? 1 : 0;
  const uint8_t hasXp = static_cast<uint8_t>(shape.xpType());
  return EncodeMatch(ComplexEllipseIndex, 0, {stroke, present, hasXp}, &result->variant);
}

/// The RoundStrokeRect family's rule, carried over from the former RoundStrokeRectFillShader: the
/// stroked rounded-rect geometry with no fragment processor, or one bare analytic rect/rrect clip,
/// rejecting perspective uv matrices. UV_MATRIX and HAS_XP flatten in that order, so the ordinal
/// is UV_MATRIX + 2 * HAS_XP.
bool MatchRoundStrokeRect(const ProgramInfo* /*info*/, const DrawShape& shape,
                          FamilyMatchResult* result) {
  if (shape.geometry.kind != GeometryKind::RoundStrokeRect) {
    return false;
  }
  const bool directClip = shape.colorFPCount == 0 &&
                          (shape.coverage == Coverage::AARect || shape.coverage == Coverage::RRect);
  if (shape.fragmentCount != 0 && !directClip) {
    return false;
  }
  if (shape.geometry.hasUVPerspective || shape.xpType() < XP_NONE) {
    return false;
  }
  const uint8_t uvMatrix = shape.geometry.hasUVMatrix ? 1 : 0;
  const uint8_t hasXp = static_cast<uint8_t>(shape.xpType());
  return EncodeMatch(RoundStrokeRectIndex, 0, {uvMatrix, hasXp}, &result->variant);
}

/// The QuadTexture family's rule, carried over from the former QuadTextureFillShader: the
/// QuadPerEdgeAA geometry with exactly one Plain (non-YUV) texture leaf and no operators above it,
/// an optional bare analytic clip, device mask or local-space mask, and any xfer. The flattened
/// dims are [UV_COORD, SUBSET, SUBSET, PRESENT, HAS_XP], so the ordinal is
/// UV_COORD + 2*SUBSET + 4*SUBSET + 8*PRESENT + 16*HAS_XP.
bool MatchQuadTexture(const ProgramInfo* /*info*/, const DrawShape& shape,
                      FamilyMatchResult* result) {
  if (shape.geometry.kind != GeometryKind::QuadPerEdgeAA || shape.colorFPCount != 1 ||
      shape.xpType() < XP_NONE || shape.ir == nullptr) {
    return false;
  }
  uint8_t present = 0;
  if (shape.fragmentCount != 1) {
    switch (*shape.coverage) {
      case Coverage::AARect:
      case Coverage::RRect:
      case Coverage::DeviceMask:
        break;
      case Coverage::LocalMask:
        present = 1;
        break;
      default:
        return false;
    }
  }
  AOTChainBuilder::AOTTextureTailShape tail = {};
  if (!AOTChainBuilder::CollectTextureTail(shape.ir->color, &tail) || !tail.ops.empty() ||
      tail.leaf->isYUV || tail.leaf->samplingKind != AOTTextureSamplingKind::Plain) {
    return false;
  }
  if (shape.geometry.hasSubset && (shape.geometry.hasUVPerspective || tail.leaf->hasPerspective)) {
    return false;
  }
  const uint8_t uvCoord = shape.geometry.hasUVMatrix ? 0 : 1;
  const uint8_t subset = shape.geometry.hasSubset ? 1 : 0;
  const uint8_t hasXp = static_cast<uint8_t>(shape.xpType());
  // The free values in flattened order: [UV_COORD, SUBSET (geometry), SUBSET (texture), PRESENT,
  // HAS_XP] — the geometry's and the texture's SUBSET move together (the allowed rule).
  if (!EncodeMatch(QuadTextureIndex, 0, {uvCoord, subset, subset, present, hasXp},
                   &result->variant)) {
    return false;
  }
  result->tiledLeaf = tail.leaf;
  return true;
}

/// The YUVTexture family's rule, the simplified former YUVTextureFillShader: the QuadPerEdgeAA
/// geometry with exactly one bare YUV leaf (no operators above it, no coverage processors) and any
/// xfer. Color-graded YUV draws stay on the legacy kernel until the flatten fallback lands. The
/// flattened dims are [UV_COORD, SUBSET=0, FORMAT, HAS_XP], so the ordinal is
/// UV_COORD + 4*FORMAT + 8*HAS_XP.
bool MatchYUVTexture(const ProgramInfo* info, const DrawShape& shape, FamilyMatchResult* result) {
  // The acceptance is the legacy kernel's, verbatim: the QuadPerEdgeAA quad without a subset,
  // the color graph is the multi-plane source with at most the tail's operators above it, and the
  // staleness guard keeps the tree and the graph in agreement.
  if (shape.geometry.kind != GeometryKind::QuadPerEdgeAA || shape.geometry.hasSubset ||
      shape.colorFPCount < 1 || shape.colorFPCount != shape.fragmentCount ||
      shape.xpType() < XP_NONE || shape.ir == nullptr) {
    return false;
  }
  // The legacy shell declared ServesAlphaOnly = false: the YUV kernel has no write swizzle, so an
  // alpha-only target keeps the runtime path.
  if (RejectsAlphaOnlyTarget(/*servesAlphaOnly=*/false, info)) {
    return false;
  }
  AOTChainBuilder::AOTTextureTailShape tail = {};
  if (!AOTChainBuilder::CollectYUVTail(shape.ir->color, &tail) ||
      shape.colorFPCount > 1 + tail.ops.size()) {
    return false;
  }
  // The tail's slot budget is the compiled program's: the same bound the legacy kernel enforced
  // through the shared capacity, so a shape it refused stays refused.
  if (info == nullptr || AOTChainBuilder::ResolveYUVProgram(info) == nullptr) {
    return false;
  }
  int format = tail.leaf->yuvFormat;
  if (format < 0) {
    auto textureView =
        tail.leaf->textureProxy != nullptr ? tail.leaf->textureProxy->getTextureView() : nullptr;
    if (textureView == nullptr || !textureView->isYUV()) {
      return false;
    }
    auto* yuvView = static_cast<const YUVTextureView*>(textureView.get());
    if (yuvView->yuvFormat() == YUVFormat::I420) {
      format = 0;
    } else if (yuvView->yuvFormat() == YUVFormat::NV12) {
      format = 1;
    } else {
      return false;
    }
  }
  const uint8_t uvCoord = shape.geometry.hasUVMatrix ? 0 : 1;
  const uint8_t hasXp = static_cast<uint8_t>(shape.xpType());
  // The writer's data source: the multi-plane leaf the tail is built over (the same field the
  // chain family carries its leaf in).
  result->tiledLeaf = tail.leaf;
  // The free values in flattened order: [UV_COORD, SUBSET = 0, FORMAT, HAS_XP] — SUBSET is free in
  // the geometry but the allowed filter keeps its 0 half only (the YUV path never carries one).
  return EncodeMatch(YUVTextureIndex, 0, {uvCoord, 0, static_cast<uint8_t>(format), hasXp},
                     &result->variant);
}

/// The AtlasText family's rule, carried over from the former AtlasTextFillShader: the atlas
/// geometry kind is the contract — no color processors, at most one coverage processor (a bare
/// analytic clip or an alpha-only affine device mask), and any xfer. HAS_COVERAGE and HAS_XP
/// flatten in that order, so the ordinal is HAS_COVERAGE + 2 * HAS_XP.
bool MatchAtlasText(const ProgramInfo* /*info*/, const DrawShape& shape,
                    FamilyMatchResult* result) {
  if (shape.geometry.kind != GeometryKind::AtlasText || shape.colorFPCount != 0 ||
      shape.fragmentCount > 1 || shape.xpType() < XP_NONE) {
    return false;
  }
  if (shape.fragmentCount == 1) {
    const bool directClip =
        *shape.coverage == Coverage::AARect || *shape.coverage == Coverage::RRect;
    if (!directClip) {
      const auto* mask = DeviceMaskCoverageLeaf(shape.ir);
      if (mask == nullptr || !mask->isAlphaOnly || mask->hasPerspective) {
        return false;
      }
    }
  }
  const uint8_t hasCoverage = shape.geometry.hasCoverage ? 1 : 0;
  const uint8_t hasXp = static_cast<uint8_t>(shape.xpType());
  return EncodeMatch(AtlasTextIndex, 0, {hasCoverage, hasXp}, &result->variant);
}

/// The chain family's rule: the legacy interpreter's Accept restated over the declared dims. The
/// acceptance is the carried IR's compilability (the chain builder's slot budget and op support),
/// not a shape predicate, so the rule compiles the draw's program exactly where the legacy Accept
/// did — and derives every dimension from the compiled program rather than guessing from the
/// shape, which is what kept the old matcher from accepting shapes its writer could not serve.
bool MatchChain(const ProgramInfo* info, const DrawShape& /*shape*/, FamilyMatchResult* result) {
  if (info == nullptr) {
    // The exclusivity test matches shapes without a program; the interpreter's acceptance needs
    // the compiled IR.
    return false;
  }
  const auto* program = AOTChainBuilder::ResolveChainProgram(info);
  if (program == nullptr) {
    return false;
  }
  auto variant = ChainInputs::MatchVariant(*program, info);
  if (!variant.has_value()) {
    return false;
  }
  result->variant = *variant;
  return true;
}

}  // namespace

/// The one match rule per family, addressed by the family index. The switch keys off the
/// FamilyIndexOf constants, so the table cannot drift from the family table's order: a reorder
/// re-keys the switch automatically, and a duplicate name would collide two case labels.
constexpr FamilyMatchFunction MatcherFor(uint16_t index) {
  switch (index) {
    case SolidColorIndex:
      return MatchSolidColor;
    case MaskFillIndex:
      return MatchMaskFill;
    case TiledTextureFillIndex:
      return MatchTiledTextureFill;
    case QuadColorIndex:
      return MatchQuadColor;
    case RRectIndex:
      return MatchRRect;
    case MeshIndex:
      return MatchMesh;
    case ShapeInstancedIndex:
      return MatchShapeInstanced;
    case ComplexRRectIndex:
      return MatchComplexRRect;
    case HairlineLineIndex:
      return MatchHairlineLine;
    case HairlineQuadIndex:
      return MatchHairlineQuad;
    case EllipseIndex:
      return MatchEllipse;
    case ComplexEllipseIndex:
      return MatchComplexEllipse;
    case RoundStrokeRectIndex:
      return MatchRoundStrokeRect;
    case QuadTextureIndex:
      return MatchQuadTexture;
    case YUVTextureIndex:
      return MatchYUVTexture;
    case AtlasTextIndex:
      return MatchAtlasText;
    case ShapeInstancedTextureCoverageIndex:
      return MatchShapeInstancedTextureCoverage;
    case GaussianBlurIndex:
      return MatchGaussianBlur;
    case GlassUDFTentBlurIndex:
      return MatchGlassUDFTentBlur;
    case GlassRefractionIndex:
      return MatchGlassRefractionEllipse;
    case GlassRefractionQuadIndex:
      return MatchGlassRefractionQuad;
    case ChainIndex:
      return MatchChain;
  }
  return nullptr;
}

/// A family table row without a match rule is a build failure: the family would silently never
/// match, dropping every one of its draws onto the fallback route.
constexpr bool EveryFamilyHasARule() {
  for (uint16_t index = 0; index < FamilyTableCount(); ++index) {
    if (MatcherFor(index) == nullptr) {
      return false;
    }
  }
  return true;
}

static_assert(EveryFamilyHasARule(), "every family table row needs a match rule in MatcherFor");

std::array<FamilyMatchFunction, FamilyTableCount()> BuildMatcherTable() {
  std::array<FamilyMatchFunction, FamilyTableCount()> table = {};
  for (size_t index = 0; index < table.size(); ++index) {
    table[index] = MatcherFor(static_cast<uint16_t>(index));
  }
  return table;
}

const FamilyMatchFunction* FamilyMatchers() {
  static const std::array<FamilyMatchFunction, FamilyTableCount()> matchers = BuildMatcherTable();
  return matchers.data();
}

std::optional<FamilyMatchResult> MatchChainProgram(const ProgramInfo* info) {
  if (info == nullptr) {
    return std::nullopt;
  }
  const auto shape = ExtractDrawShape(info);
  FamilyMatchResult result = {};
  if (!MatchChain(info, shape, &result)) {
    return std::nullopt;
  }
  return result;
}

namespace {

#if DEBUG
// The mutual-exclusion tripwire: the curated rules are pairwise disjoint by contract, so a second
// acceptor means two predicates overlap and the walk order would silently pick the winner. Debug
// builds walk the rows after the first acceptor and report any second one; release never pays for
// the walk.
void ReportFamilyConflicts(const ProgramInfo* info, const DrawShape& shape,
                           uint16_t acceptedIndex) {
  const FamilyMatchFunction* matchers = FamilyMatchers();
  for (uint16_t index = acceptedIndex + 1; index < FamilyCount(); ++index) {
    if (Families()[index].fallback) {
      continue;
    }
    FamilyMatchResult result = {};
    if (!matchers[index](info, shape, &result)) {
      continue;
    }
    LOGE("[family-conflict] the %s and %s rules both accept one draw; narrow one of them",
         Families()[acceptedIndex].name, Families()[index].name);
    DEBUG_ASSERT(false);
  }
}
#endif

}  // namespace

std::optional<FamilyMatchResult> MatchAssembledFamily(const ProgramInfo* info,
                                                      const DrawShape& shape) {
  if (FamilyCount() == 0) {
    return std::nullopt;
  }
  const FamilyMatchFunction* matchers = FamilyMatchers();
  // Phase one: the curated shape rules, pairwise disjoint by contract, so the first acceptor is
  // the only one. The walk stops there in every build; only the conflict tripwire reads the rest,
  // and it exists in debug builds alone.
  for (uint16_t index = 0; index < FamilyCount(); ++index) {
    if (Families()[index].fallback) {
      continue;
    }
    FamilyMatchResult result = {};
    if (matchers[index](info, shape, &result)) {
#if DEBUG
      ReportFamilyConflicts(info, shape, index);
#endif
      return result;
    }
  }
  // Phase two: the fallback rows (the interpreter), consulted only when no curated rule accepted.
  // Their acceptance is the carried IR's compilability, a superset of the shape rules, so they
  // are excluded from the disjointness contract by design.
  for (uint16_t index = 0; index < FamilyCount(); ++index) {
    if (!Families()[index].fallback) {
      continue;
    }
    FamilyMatchResult result = {};
    if (matchers[index](info, shape, &result)) {
      return result;
    }
  }
  return std::nullopt;
}

}  // namespace tgfx

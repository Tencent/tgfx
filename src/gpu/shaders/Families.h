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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/Family.h"
#include "gpu/shaders/modules/AnalyticClip.h"
#include "gpu/shaders/modules/AtlasTextGeometry.h"
#include "gpu/shaders/modules/BlurSourceGeometry.h"
#include "gpu/shaders/modules/ComplexEllipseGeometry.h"
#include "gpu/shaders/modules/ComplexRRectGeometry.h"
#include "gpu/shaders/modules/CoverageTexture.h"
#include "gpu/shaders/modules/DeviceMask.h"
#include "gpu/shaders/modules/DeviceMaskRuntime.h"
#include "gpu/shaders/modules/EllipseGeometry.h"
#include "gpu/shaders/modules/GaussianBlur.h"
#include "gpu/shaders/modules/Gradient.h"
#include "gpu/shaders/modules/HairlineLineGeometry.h"
#include "gpu/shaders/modules/HairlineQuadGeometry.h"
#include "gpu/shaders/modules/LocalMask.h"
#include "gpu/shaders/modules/MaskTexture.h"
#include "gpu/shaders/modules/MeshGeometry.h"
#include "gpu/shaders/modules/QuadColorGeometry.h"
#include "gpu/shaders/modules/QuadGeometry.h"
#include "gpu/shaders/modules/QuadTextureGeometry.h"
#include "gpu/shaders/modules/RRectGeometry.h"
#include "gpu/shaders/modules/RectGeometry.h"
#include "gpu/shaders/modules/RoundStrokeRectGeometry.h"
#include "gpu/shaders/modules/ShapeInstancedGeometry.h"
#include "gpu/shaders/modules/Texture.h"
#include "gpu/shaders/modules/TiledTexture.h"
#include "gpu/shaders/modules/YUVTexture.h"

namespace tgfx {

/// The SolidColor family (the former SolidColorFillShader): the DefaultGP's rect fill with the
/// analytic clip riding the runtime contract. Slot layout: Frame(0), RectGeometry(1),
/// AnalyticClip(2), Xfer(3).
inline constexpr const Module* RectGeometries[] = {&RectGeometry};

inline constexpr FamilyModule SolidColorBody[] = {&AnalyticClip};

/// The MaskFill family (the former MaskFillShader): the DefaultGP's rect fill whose coverage is a
/// rasterized alpha-only mask texture. Slot layout: Frame(0), RectGeometry(1), MaskTexture(2),
/// Xfer(3).
inline constexpr FamilyModule MaskFillBody[] = {&MaskTexture};

/// The TiledTextureFill family (the former TiledTextureFillShader): a rect or quad filled with a
/// nine-mode tiled texture. The legacy QuadTextureFillShader still wins the Plain-leaf Quad draws
/// it accepts (the legacy dedicated table runs first); the declared family serves the DefaultGP
/// form and the Quad draws with a Tiled leaf, which the legacy family never accepted. FULL is
/// pinned to 1 (the full nine-mode recipe), PRESENT to 1 (the texture is the family's color
/// source). Slot layout: Frame(0), geometry(1), TiledTexture(2: FULL pinned, PRESENT pinned),
/// Xfer(3).
inline constexpr const Module* RectQuadGeometries[] = {&RectGeometry, &QuadGeometry};

inline constexpr int8_t TiledTextureFillPins[] = {1, 1};

inline constexpr FamilyModule TiledTextureFillBody[] = {{&TiledTexture, TiledTextureFillPins}};

/// The QuadColor family (the former QuadColorFillShader): the QuadPerEdgeAA rect filled with the
/// per-vertex color (or the exact Color uniform), an optional device-space mask and the analytic
/// clip. Slot layout: Frame(0), QuadColorGeometry(1), DeviceMask(2: PRESENT), AnalyticClip(3),
/// Xfer(4).
inline constexpr const Module* QuadColorGeometries[] = {&QuadColorGeometry};

inline constexpr FamilyModule QuadColorBody[] = {&DeviceMask, &AnalyticClip};

/// The RRect family (the former NonAARRectFillShader): a non-AA rounded rect, optionally filled
/// with a single-tap tiled texture, with the analytic clip riding the runtime contract. The
/// textured form never combines with stroke (the old family's `allowed` rule carried over); FULL
/// is pinned to 0 because this family serves the single-tap recipe only.
inline constexpr const Module* RRectGeometries[] = {&RRectGeometry};

/// Dim pins for the TiledTexture slot: FULL fixed to 0, PRESENT free.
inline constexpr int8_t RRectTiledTexturePins[] = {0, -1};

inline constexpr FamilyModule RRectBody[] = {{&TiledTexture, RRectTiledTexturePins}, &AnalyticClip};

/// Slot layout: Frame(0), RRectGeometry(1), TiledTexture(2), AnalyticClip(3), Xfer(4).
bool RRectVariantAllowed(const Variant& variant, const Family& family);

/// The Mesh family (the former MeshFillShader): the triangle mesh with no fragment processor. The
/// top-level color comes from the per-vertex varying or the exact Color uniform; the family carried
/// no clip step in the old declaration, so the body stays empty. Slot layout: Frame(0),
/// MeshGeometry(1: TEX_COORDS), Xfer(2).
inline constexpr const Module* MeshGeometries[] = {&MeshGeometry};

/// The ShapeInstanced family (the former ShapeInstancedFillShader): the instanced path triangles
/// with no fragment processor. VERTEX_COLOR reserves the geometry's stride-filler color slot and
/// is filtered out of the enumeration, so the instance attributes keep their physical slots.
/// Slot layout: Frame(0), ShapeInstancedGeometry(1), Xfer(2).
inline constexpr const Module* ShapeInstancedGeometries[] = {&ShapeInstancedGeometry};

/// Slot layout: Frame(0), ShapeInstancedGeometry(1), Xfer(2).
bool ShapeInstancedVariantAllowed(const Variant& variant, const Family& family);

/// The family table, in match priority order. The declaration-driven families are checked before
/// the legacy kernel table; among themselves the dedicated rules are disjoint.
/// The ComplexRRect family (the former ComplexNonAARRectFillShader): a per-corner-radius rounded
/// rect with no fragment processor. STROKE selects the stroke form (it carries the stroke width
/// attribute and varying). Slot layout: Frame(0), ComplexRRectGeometry(1: STROKE), Xfer(2).
inline constexpr const Module* ComplexRRectGeometries[] = {&ComplexRRectGeometry};

/// The HairlineLine family (the former HairlineLineShader): a thin line whose coverage is the edge
/// distance's absolute value times the draw coverage, times the analytic clip. Slot layout:
/// Frame(0), HairlineLineGeometry(1), Xfer(2).
inline constexpr const Module* HairlineLineGeometries[] = {&HairlineLineGeometry};

/// The HairlineQuad family (the former HairlineQuadShader): the Loop-Blinn hairline quad, same
/// slot layout.
inline constexpr const Module* HairlineQuadGeometries[] = {&HairlineQuadGeometry};

/// The Ellipse family (the former EllipseFillShader): an ellipse whose coverage is the analytic
/// edge SDF, with an optional device-space mask (the old FILL_DEVICE_MASK_ALWAYS form, so the mask
/// is a compile-time dimension) and the analytic clip. Slot layout: Frame(0), EllipseGeometry(1),
/// DeviceMask(2: PRESENT), AnalyticClip(3), Xfer(4).
inline constexpr const Module* EllipseGeometries[] = {&EllipseGeometry};

inline constexpr FamilyModule EllipseBody[] = {&DeviceMask, &AnalyticClip};

/// The ComplexEllipse family (the former ComplexEllipseFillShader): the analytic complex-ellipse
/// coverage (fill and stroke forms), the shared device-space mask and the analytic clip. Slot
/// layout: Frame(0), ComplexEllipseGeometry(1: STROKE), DeviceMask(2: PRESENT), AnalyticClip(3),
/// Xfer(4).
inline constexpr const Module* ComplexEllipseGeometries[] = {&ComplexEllipseGeometry};

inline constexpr FamilyModule ComplexEllipseBody[] = {&DeviceMask, &AnalyticClip};

/// The RoundStrokeRect family (the former RoundStrokeRectFillShader): a stroked rounded rect whose
/// coverage is the normalized-offset ellipse SDF, times the analytic clip. Slot layout: Frame(0),
/// RoundStrokeRectGeometry(1: UV_MATRIX), AnalyticClip(2), Xfer(3).
inline constexpr const Module* RoundStrokeRectGeometries[] = {&RoundStrokeRectGeometry};

inline constexpr FamilyModule RoundStrokeRectBody[] = {&AnalyticClip};

/// The array is the single source of truth for both the table pointer and its count: keeping them
/// apart is how a family silently drops out of matching (or the count runs past the array) after
/// a reformat.
/// The QuadTexture family (the former QuadTextureFillShader): a textured quad with an optional
/// local-space mask, the runtime device mask and the analytic clip. The geometry and the Texture
/// module both carry HAS_SUBSET, and the product rule keeps them in lockstep. Slot layout:
/// Frame(0), QuadTextureGeometry(1: UV_COORD, SUBSET), Texture(2: SUBSET), LocalMask(3: PRESENT),
/// DeviceMaskRuntime(4), AnalyticClip(5), Xfer(6).
inline constexpr const Module* QuadTextureGeometries[] = {&QuadTextureGeometry};

inline constexpr FamilyModule QuadTextureBody[] = {&Texture, &DeviceMaskRuntime, &LocalMask,
                                                   &AnalyticClip};

/// Slot layout: Frame(0), QuadTextureGeometry(1), Texture(2), LocalMask(3), DeviceMaskRuntime(4),
/// AnalyticClip(5), Xfer(6).
bool QuadTextureVariantAllowed(const Variant& variant, const Family& family);

inline constexpr Family QuadTextureFamily = {
    "QuadTexture", QuadTextureGeometries, 1, QuadTextureBody, 4, QuadTextureVariantAllowed};

/// The YUVTexture family (the simplified former YUVTextureFillShader): the bare three-plane
/// source with the analytic clip. Color-graded YUV draws keep the legacy kernel until the flatten
/// fallback lands; the geometry's HAS_SUBSET stays at 0 (the YUV path never carries one).
inline constexpr FamilyModule YUVTextureBody[] = {&YUVTexture, &AnalyticClip};

/// Slot layout: Frame(0), QuadTextureGeometry(1), YUVTexture(2: FORMAT), AnalyticClip(3), Xfer(4).
bool YUVTextureVariantAllowed(const Variant& variant, const Family& family);

inline constexpr Family YUVTextureFamily = {
    "YUVTexture", QuadTextureGeometries, 1, YUVTextureBody, 2, YUVTextureVariantAllowed};

/// The AtlasText family (the former AtlasTextFillShader): the glyph quad with the runtime device
/// mask and the analytic clip. HAS_COVERAGE selects whether the geometry carries the per-vertex
/// coverage channel. Slot layout: Frame(0), AtlasTextGeometry(1: HAS_COVERAGE),
/// DeviceMaskRuntime(2), AnalyticClip(3), Xfer(4).
inline constexpr const Module* AtlasTextGeometries[] = {&AtlasTextGeometry};

inline constexpr FamilyModule AtlasTextBody[] = {&DeviceMaskRuntime, &AnalyticClip};

/// The ShapeInstancedTextureCoverage family (the former ShapeInstancedTextureCoverageShader): the
/// instanced path triangles in their non-AA form, whose coverage is a plain texture instead of the
/// geometry's per-vertex channel (the batch's coverage slot is a stride filler, so the geometry
/// keeps the same VERTEX_COLOR = 0 hole ShapeInstanced uses), with the optional single-interval
/// gradient over the instance color and the analytic clip. The geometry's coord matrix is the
/// coverage texture's transform — the coverage terms trail the color graph, so it is the last one.
/// The legacy kernel served the no-xfer case only, so HAS_XP stays pinned to XP_NONE. Slot layout:
/// Frame(0), ShapeInstancedGeometry(1), Gradient(2: PRESENT), CoverageTexture(3), AnalyticClip(4),
/// Xfer(5: HAS_XP).
inline constexpr const Module* ShapeInstancedTextureCoverageGeometries[] = {
    &ShapeInstancedGeometry};

inline constexpr FamilyModule ShapeInstancedTextureCoverageBody[] = {&Gradient, &CoverageTexture,
                                                                     &AnalyticClip};

/// Slot layout: Frame(0), ShapeInstancedGeometry(1), Gradient(2), CoverageTexture(3),
/// AnalyticClip(4), Xfer(5).
bool ShapeInstancedTextureCoverageVariantAllowed(const Variant& variant, const Family& family);

/// The GaussianBlur family (the former GaussianBlur1DShader).
inline constexpr const Module* GaussianBlurGeometries[] = {&BlurSourceGeometry};

inline constexpr FamilyModule GaussianBlurBody[] = {&GaussianBlur, &DeviceMaskRuntime,
                                                    &AnalyticClip};

/// The family table is the single definition site: a family exists by appearing here and nowhere
/// else, so "defined but not registered" cannot happen — adding a row is the only way to add a
/// family, and the match rules address the rows by position.
inline constexpr Family FamilyTableStorage[] = {
    {"SolidColor", RectGeometries, 1, SolidColorBody, 1},
    {"MaskFill", RectGeometries, 1, MaskFillBody, 1},
    {"TiledTextureFill", RectQuadGeometries, 2, TiledTextureFillBody, 1},
    {"QuadColor", QuadColorGeometries, 1, QuadColorBody, 2},
    {"RRect", RRectGeometries, 1, RRectBody, 2, RRectVariantAllowed},
    {"Mesh", MeshGeometries, 1, nullptr, 0},
    {"ShapeInstanced", ShapeInstancedGeometries, 1, nullptr, 0, ShapeInstancedVariantAllowed},
    {"ComplexRRect", ComplexRRectGeometries, 1, nullptr, 0},
    {"HairlineLine", HairlineLineGeometries, 1, nullptr, 0},
    {"HairlineQuad", HairlineQuadGeometries, 1, nullptr, 0},
    {"Ellipse", EllipseGeometries, 1, EllipseBody, 2},
    {"ComplexEllipse", ComplexEllipseGeometries, 1, ComplexEllipseBody, 2},
    {"RoundStrokeRect", RoundStrokeRectGeometries, 1, RoundStrokeRectBody, 1},
    {"QuadTexture", QuadTextureGeometries, 1, QuadTextureBody, 4, QuadTextureVariantAllowed},
    {"YUVTexture", QuadTextureGeometries, 1, YUVTextureBody, 2, YUVTextureVariantAllowed},
    {"AtlasText", AtlasTextGeometries, 1, AtlasTextBody, 2},
    {"ShapeInstancedTextureCoverage", ShapeInstancedTextureCoverageGeometries, 1,
     ShapeInstancedTextureCoverageBody, 3, ShapeInstancedTextureCoverageVariantAllowed},
    {"GaussianBlur", GaussianBlurGeometries, 1, GaussianBlurBody, 3},
};

inline const Family* FamilyTable() {
  return FamilyTableStorage;
}

inline constexpr size_t FamilyTableCount() {
  return sizeof(FamilyTableStorage) / sizeof(FamilyTableStorage[0]);
}

}  // namespace tgfx

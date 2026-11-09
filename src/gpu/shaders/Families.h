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
#include "gpu/shaders/modules/MaskTexture.h"
#include "gpu/shaders/modules/QuadGeometry.h"
#include "gpu/shaders/modules/RRectGeometry.h"
#include "gpu/shaders/modules/RectGeometry.h"
#include "gpu/shaders/modules/TiledTexture.h"

namespace tgfx {

/// The SolidColor family (the former SolidColorFillShader): the DefaultGP's rect fill with the
/// analytic clip riding the runtime contract. Slot layout: Frame(0), RectGeometry(1),
/// AnalyticClip(2), Xfer(3).
inline constexpr const Module* RectGeometries[] = {&RectGeometry};

inline constexpr FamilyModule SolidColorBody[] = {&AnalyticClip};

inline constexpr Family SolidColorFamily = {"SolidColor", RectGeometries, 1, SolidColorBody, 1};

/// The MaskFill family (the former MaskFillShader): the DefaultGP's rect fill whose coverage is a
/// rasterized alpha-only mask texture. Slot layout: Frame(0), RectGeometry(1), MaskTexture(2),
/// Xfer(3).
inline constexpr FamilyModule MaskFillBody[] = {&MaskTexture};

inline constexpr Family MaskFillFamily = {"MaskFill", RectGeometries, 1, MaskFillBody, 1};

/// The TiledTextureFill family (the former TiledTextureFillShader): a DefaultGP rect filled with a
/// nine-mode tiled texture. The quad geometry stays with the legacy QuadTextureFillShader, which
/// the old table consulted first for QuadPerEdgeAA draws; the quad form migrates with that family.
/// FULL is pinned to 1 (the full nine-mode recipe), PRESENT to 1 (the texture is the family's
/// color source). Slot layout: Frame(0), RectGeometry(1), TiledTexture(2: FULL pinned, PRESENT
/// pinned), Xfer(3).
inline constexpr int8_t TiledTextureFillPins[] = {1, 1};

inline constexpr FamilyModule TiledTextureFillBody[] = {{&TiledTexture, TiledTextureFillPins}};

inline constexpr Family TiledTextureFillFamily = {"TiledTextureFill", RectGeometries, 1,
                                                  TiledTextureFillBody, 1};

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

inline constexpr Family RRectFamily = {"RRect", RRectGeometries,    1, RRectBody,
                                       2,       RRectVariantAllowed};

/// The family table, in match priority order. The declaration-driven families are checked before
/// the legacy kernel table; among themselves the dedicated rules are disjoint.
inline const Family* FamilyTable() {
  static const Family families[] = {SolidColorFamily, MaskFillFamily, TiledTextureFillFamily,
                                    RRectFamily};
  return families;
}

inline constexpr size_t FamilyTableCount() {
  return 4;
}

}  // namespace tgfx

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
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>

namespace tgfx {

/**
 * The draw's geometry kind as the matchers see it: one value per processor class the precompiled
 * kernels serve, and Other for everything else.
 */
enum class GeometryKind : uint8_t {
  Other = 0,
  Default,
  QuadPerEdgeAA,
  // The rounded-rect pairs (RRectDrawOp): the non-AA forms at AAType::None and the AA forms, each
  // in a plain and a per-corner-radii variant.
  NonAARRect,
  ComplexNonAARRect,
  Ellipse,
  ComplexEllipse,
  // The hairline line/quad pair (HairlineLineOp / HairlineQuadOp) and the triangle mesh
  // (MeshDrawOp).
  HairlineLine,
  HairlineQuad,
  Mesh,
  // The round-capped/joined stroke rect (RectDrawOp's round line join) and the instanced shape
  // batch (ShapeInstancedDrawOp; two families share this processor).
  RoundStrokeRect,
  ShapeInstanced,
  // The glyph atlas geometry (AtlasTextOp).
  AtlasText,
};

}  // namespace tgfx

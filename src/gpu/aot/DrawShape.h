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
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <optional>
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/shaders/ShaderPermutation.h"

namespace tgfx {

class ProgramInfo;
struct DrawIR;

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

/**
 * The geometry facts a draw's matcher rules read, as plain data. Described off the made geometry
 * processor at match time (DescribeGeometryProcessor) — the matching runs at prepare, where the
 * processor itself is the truth, so there is one description and no record-time copy.
 */
struct AOTGeometryDesc {
  GeometryKind kind = GeometryKind::Other;
  // QuadPerEdgeAA only: the vertex stage's layout facts the matchers read.
  bool hasCommonColor = false;
  bool hasUVMatrix = false;
  bool hasSubset = false;
  bool hasUVPerspective = false;
  // The rounded-rect kinds only: the stroke form (the processors' isStroke).
  bool isStroke = false;
  // Mesh only: whether the vertex attributes carry texture coordinates.
  bool hasTexCoords = false;
  // AtlasText only: whether the processor carries the AA coverage attribute (AAType::Coverage).
  bool hasCoverage = false;
};

/**
 * The xfer classification the matcher rules read (the XP_* vocabulary's plain-data twin): how
 * the destination color reaches the fragment shader. Classified off the made xfer processor at
 * match time — the dst-texture decision was made when the op was recorded (a dst-texture
 * processor holds its proxy, a framebuffer-fetch one empty info), so the processor's own dst
 * info is exact.
 */
enum class XferKind : uint8_t {
  Unsupported = 0,
  None,
  DstTexture,
  FrameBufferFetch,
};

/**
 * A draw's matcher facts, extracted once per match: the geometry and xfer described off the made
 * processors (the matching runs at prepare, where they are the truth), the coverage
 * classification off the carried IR's terms, the target's alpha-only flag, and the live
 * processor-list sizes. The executor-table rows are predicates over this — one extraction
 * replaces every family re-deriving the same facts from the processor tree.
 *
 * The carried IR is the only expression source: record-time lowering makes it total, and the
 * prepare-grown coverage is appended to its terms. Facts that exist only at instance time are
 * read off the made processors (geometry, xfer) or off the IR node's own proxy (a pending
 * multi-plane upload's layout).
 */
struct DrawShape {
  const DrawIR* ir = nullptr;
  AOTGeometryDesc geometry = {};
  XferKind xfer = XferKind::Unsupported;
  bool alphaOnlyTarget = false;
  size_t colorFPCount = 0;
  size_t fragmentCount = 0;
  /// The ShaderTextureRead bits the draw's sampled textures need (see ShaderTextureRead),
  /// computed once at extraction so a row's declared capability can be gated against it.
  uint32_t requiredTextureReads = 0;
  /// The draw's classified coverage (the one ClassifyCoverageTerms computes): nullopt when the
  /// terms do not classify. The rows' predicates derive from it — a direct analytic clip is
  /// AARect/RRect, the shared device-mask ABI bit is DeviceMask.
  std::optional<Coverage> coverage = std::nullopt;

  /// The position-only rect form the texture and noise fill kernels share: a DefaultGP, or a
  /// QuadPerEdgeAA whose coordinate comes from aPosition (a common color, a uv matrix, no
  /// per-quad subset).
  bool positionOnlyRect() const {
    return geometry.kind == GeometryKind::Default ||
           (geometry.kind == GeometryKind::QuadPerEdgeAA && geometry.hasCommonColor &&
            geometry.hasUVMatrix && !geometry.hasSubset);
  }

  /// The XP_* vocabulary value (the fill families' shared HAS_XP axis).
  int xpType() const {
    switch (xfer) {
      case XferKind::None:
        return XP_NONE;
      case XferKind::DstTexture:
        return XP_DST_TEXTURE;
      case XferKind::FrameBufferFetch:
        return XP_FRAMEBUFFER_FETCH;
      case XferKind::Unsupported:
        break;
    }
    return XP_UNSUPPORTED;
  }
};

DrawShape ExtractDrawShape(const ProgramInfo* programInfo);

}  // namespace tgfx

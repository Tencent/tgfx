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

#include "QuadsVertexProvider.h"
#include <optional>
#include "core/utils/ColorHelper.h"
#include "core/utils/MathExtra.h"
#include "core/utils/VecUtils.h"

namespace tgfx {

// Tolerance for distance comparison.
static constexpr float DIST_TOLERANCE = 1e-2f;
static constexpr float INV_DIST_TOLERANCE = 1.0f / DIST_TOLERANCE;
// Threshold for division-by-zero prevention.
static constexpr float DENOM_TOLERANCE = 1e-9f;
// Offset distance for edge anti-aliasing.
static constexpr float AA_OFFSET = 0.5f;
// Threshold for degenerate vertex angle detection (interior angle < 26° or > 154°).
static constexpr float COS_THETA_THRESHOLD = 0.9f;

/**
 * 4 vertices coordinates in SoA layout.
 * Vertex layout:
 *   0 ←-- 2
 *   ↓     ↑
 *   1 --→ 3
 */
struct Vertices4 {
  Vec4 xs = {};
  Vec4 ys = {};
};

/**
 * 4 edges geometric data in SoA layout.
 * Edge[i] is the edge starting from vertex[i].
 * Edge and vertex layout:
 *   0 ←-- 2
 *   ↓     ↑
 *   1 --→ 3
 */
struct EdgeDatas {
  // Normalized edge vector X components.
  Vec4 dxs = {};
  // Normalized edge vector Y components.
  Vec4 dys = {};
  // Inverse edge lengths (inf for zero-length edges).
  Vec4 invLengths = {};
  // Cosine of interior angle at vertex[i].
  Vec4 cosThetas = {};
  // Inverse of sin(interior angle) at vertex[i].
  Vec4 invSinThetas = {};
};

/**
 * 4 edge equations in SoA layout.
 * Equation form: ax + by + c = 0, where (a, b) is the inward-pointing normalized normal.
 * Edge[i] is the edge starting from vertex[i].
 * Edge layout:
 *   0 ←-- 2
 *   ↓     ↑
 *   1 --→ 3
 */
struct EdgeEquations {
  // Normal X components.
  Vec4 as = {};
  // Normal Y components.
  Vec4 bs = {};
  // Signed distances from origin to edge.
  Vec4 cs = {};
};

/**
 * Shuffle each component to its clockwise neighbor.
 */
static inline Vec4 NextCW(const Vec4& v) {
  return VecUtils::Shuffle<2, 0, 3, 1>(v);
}

/**
 * Shuffle each component to its counter-clockwise neighbor.
 */
static inline Vec4 NextCCW(const Vec4& v) {
  return VecUtils::Shuffle<1, 3, 0, 2>(v);
}

/**
 * Shuffle each component to its diagonal counterpart.
 */
static inline Vec4 NextDiag(const Vec4& v) {
  return VecUtils::Shuffle<3, 2, 1, 0>(v);
}

/**
 * Convert Quad to Vertices4 SoA layout.
 */
static inline Vertices4 ToVertices4(const Quad& quad) {
  return {Vec4(quad.point(0).x, quad.point(1).x, quad.point(2).x, quad.point(3).x),
          Vec4(quad.point(0).y, quad.point(1).y, quad.point(2).y, quad.point(3).y)};
}

/**
 * Transform Vertices4 by matrix.
 */
static inline Vertices4 TransformVertices4(const Vertices4& coord, const Matrix& matrix) {
  Point points[4] = {{coord.xs[0], coord.ys[0]},
                     {coord.xs[1], coord.ys[1]},
                     {coord.xs[2], coord.ys[2]},
                     {coord.xs[3], coord.ys[3]}};
  matrix.mapPoints(points, 4);
  return {Vec4(points[0].x, points[1].x, points[2].x, points[3].x),
          Vec4(points[0].y, points[1].y, points[2].y, points[3].y)};
}

/**
 * Correct zero-length edges: use diagonal edge direction with flipped sign.
 */
static inline void CorrectBadEdges(const Vec4& badMask, Vec4& dxs, Vec4& dys) {
  if (VecUtils::Any(badMask)) {
    dxs = VecUtils::IfThenElse(badMask, -NextDiag(dxs), dxs);
    dys = VecUtils::IfThenElse(badMask, -NextDiag(dys), dys);
  }
}

/**
 * Correct bad coordinates: use CCW next vertex as fallback.
 */
static inline void CorrectBadCoords(const Vec4& badMask, Vec4& xs, Vec4& ys) {
  if (VecUtils::Any(badMask)) {
    xs = VecUtils::IfThenElse(badMask, NextCCW(xs), xs);
    ys = VecUtils::IfThenElse(badMask, NextCCW(ys), ys);
  }
}

/**
 * Handle triangle degeneration.
 * Assumes quad is rectangle-like with vertex 0 at top-left, so E0/E3 are vertical and E1/E2 are
 * horizontal.
 * When computed vertices cross non-adjacent edges, replace them with diagonal edge intersection
 * points.
 * When opposite edges are nearly parallel or barely crossed, their intersection may fall far
 * outside the quad, so use the midpoint of the edge containing the vertex as a fallback.
 * @param offsetEdgeEqs Offset edge equations with adjusted cs values.
 * @param edgeDistsV Signed distances from each vertex to its non-adjacent vertical edge.
 * @param edgeDistsH Signed distances from each vertex to its non-adjacent horizontal edge.
 * @param pxs X coordinates of vertices to correct (in/out).
 * @param pys Y coordinates of vertices to correct (in/out).
 */
static inline void CorrectTriangleDegeneration(const EdgeEquations& offsetEdgeEqs,
                                               const Vec4& edgeDistsV, const Vec4& edgeDistsH,
                                               Vec4& pxs, Vec4& pys) {
  // Intersection of two lines a*x + b*y + c = 0:
  // denom = a0*b1 - b0*a1, x = (b0*c1 - c0*b1) / denom, y = (c0*a1 - a0*c1) / denom
  // Compute intersection of edges E0 and E3.
  const float denomV =
      offsetEdgeEqs.as[0] * offsetEdgeEqs.bs[3] - offsetEdgeEqs.bs[0] * offsetEdgeEqs.as[3];
  const float xV =
      (offsetEdgeEqs.bs[0] * offsetEdgeEqs.cs[3] - offsetEdgeEqs.cs[0] * offsetEdgeEqs.bs[3]) /
      denomV;
  const float yV =
      (offsetEdgeEqs.cs[0] * offsetEdgeEqs.as[3] - offsetEdgeEqs.as[0] * offsetEdgeEqs.cs[3]) /
      denomV;
  const bool validV = std::abs(denomV) > DENOM_TOLERANCE;

  // Compute intersection of edges E1 and E2.
  const float denomH =
      offsetEdgeEqs.as[1] * offsetEdgeEqs.bs[2] - offsetEdgeEqs.bs[1] * offsetEdgeEqs.as[2];
  const float xH =
      (offsetEdgeEqs.bs[1] * offsetEdgeEqs.cs[2] - offsetEdgeEqs.cs[1] * offsetEdgeEqs.bs[2]) /
      denomH;
  const float yH =
      (offsetEdgeEqs.cs[1] * offsetEdgeEqs.as[2] - offsetEdgeEqs.as[1] * offsetEdgeEqs.cs[2]) /
      denomH;
  const bool validH = std::abs(denomH) > DENOM_TOLERANCE;

  auto avgXs = 0.5f * (VecUtils::Shuffle<0, 1, 0, 2>(pxs) + VecUtils::Shuffle<2, 3, 1, 3>(pxs));
  auto avgYs = 0.5f * (VecUtils::Shuffle<0, 1, 0, 2>(pys) + VecUtils::Shuffle<2, 3, 1, 3>(pys));

  auto overEdgeV = VecUtils::LessThan(edgeDistsV, DIST_TOLERANCE);
  auto overEdgeH = VecUtils::LessThan(edgeDistsH, DIST_TOLERANCE);

  for (int i = 0; i < 4; ++i) {
    if (edgeDistsV[i] < -DIST_TOLERANCE && validV) {
      // Vertex clearly crossed vertical opposite edge, use intersection point.
      pxs[i] = xV;
      pys[i] = yV;
    } else if (!FloatNearlyZero(overEdgeV[i])) {
      // Fallback: use midpoint when edges nearly parallel or barely crossed.
      pxs[i] = avgXs[i % 2];
      pys[i] = avgYs[i % 2];
    } else if (edgeDistsH[i] < -DIST_TOLERANCE && validH) {
      // Vertex clearly crossed horizontal opposite edge, use intersection point.
      pxs[i] = xH;
      pys[i] = yH;
    } else if (!FloatNearlyZero(overEdgeH[i])) {
      // Fallback: use midpoint when edges nearly parallel or barely crossed.
      pxs[i] = avgXs[i / 2 + 2];
      pys[i] = avgYs[i / 2 + 2];
    }
  }
}

/**
 * Compute edge data from vertices.
 */
static inline EdgeDatas ComputeEdgeDatas(const Vertices4& vertices) {
  EdgeDatas ed = {};

  // Compute edge vectors (edge[i] from vertex[i] to vertex[NEXT_CCW[i]]).
  const auto rawDxs = NextCCW(vertices.xs) - vertices.xs;
  const auto rawDys = NextCCW(vertices.ys) - vertices.ys;

  // Note: Zero-length edges produce inf here. Do NOT replace inf with 0 — downstream code
  // relies on `invLengths >= INV_DIST_TOLERANCE` to detect and handle zero-length edges.
  ed.invLengths = 1.0f / VecUtils::Sqrt(rawDxs * rawDxs + rawDys * rawDys);
  ed.dxs = rawDxs * ed.invLengths;
  ed.dys = rawDys * ed.invLengths;

  // cosTheta = dot(current_edge, next_cw_edge).
  ed.cosThetas = ed.dxs * NextCW(ed.dxs) + ed.dys * NextCW(ed.dys);
  ed.invSinThetas = 1.0f / VecUtils::Sqrt(Vec4(1.0f) - ed.cosThetas * ed.cosThetas);

  return ed;
}

/**
 * Compute edge equations from vertices and edge data.
 */
static inline EdgeEquations ComputeEdgeEquations(const Vertices4& vertices,
                                                 const EdgeDatas& edgeDatas) {
  auto dxs = edgeDatas.dxs;
  auto dys = edgeDatas.dys;

  // Correct zero-length edges.
  const auto badMask = VecUtils::GreaterEqual(edgeDatas.invLengths, INV_DIST_TOLERANCE);
  CorrectBadEdges(badMask, dxs, dys);

  // For edge equation ax + by + c = 0, assuming normal is (a, b) = (dy, -dx).
  // c is the signed distance from the origin to the edge along the normal.
  const auto cs = dxs * vertices.ys - dys * vertices.xs;

  // Compute signed distance from next_cw vertex to edge to ensure normals point inward.
  if (const auto test = dys * NextCW(vertices.xs) - dxs * NextCW(vertices.ys) + cs;
      VecUtils::Any(VecUtils::LessThan(test, -DIST_TOLERANCE))) {
    return {-dys, dxs, -cs};
  } else {
    return {dys, -dxs, cs};
  }
}

/**
 * Check if quad will degenerate when computing AA vertices.
 * Rectangle quads never degenerate since all angles are 90 degrees.
 */
static inline bool IsAADegenerate(const EdgeDatas& edgeDatas, bool isRect) {
  if (isRect) {
    return false;
  }
  const auto badLength = VecUtils::GreaterEqual(edgeDatas.invLengths, INV_DIST_TOLERANCE);
  const auto badAngle =
      VecUtils::GreaterEqual(VecUtils::Abs(edgeDatas.cosThetas), COS_THETA_THRESHOLD);
  return VecUtils::Any(VecUtils::Or(badLength, badAngle));
}

/**
 * Check if all 4 vertices are collinear (form a line).
 */
static inline bool IsCollinear(const Vertices4& vertices, const EdgeEquations& edgeEqs) {
  // For each edge equation, check if all 4 vertices lie on it.
  // If any edge has all vertices within tolerance, the quad is collinear.
  for (int i = 0; i < 4; ++i) {
    if (auto dists = vertices.xs * edgeEqs.as[i] + vertices.ys * edgeEqs.bs[i] + edgeEqs.cs[i];
        VecUtils::All(VecUtils::LessThan(VecUtils::Abs(dists), DIST_TOLERANCE))) {
      return true;
    }
  }
  return false;
}

/**
 * Compute AA vertices for non-degenerate quads.
 */
static inline void ComputeAAVertices(const Vertices4& vertices, const Vec4& edgeOffset,
                                     const EdgeDatas& edgeDatas, Vertices4& outInset,
                                     Vertices4& outOutset) {
  // For vertex[i], compute miter displacement from two adjacent edges using 1/sin(θ).
  // fromOutsets: contribution from outgoing edge (edge[i], starting at vertex[i])
  // toOutsets: contribution from incoming edge (edge[NEXT_CW[i]], ending at vertex[i])
  const auto fromOutsets = edgeDatas.invSinThetas * edgeOffset;
  const auto toOutsets = edgeDatas.invSinThetas * NextCW(edgeOffset);

  // Outgoing edge contribution applies along incoming edge direction (positive).
  // Incoming edge contribution applies along outgoing edge direction (negative).
  const auto outsetXs = fromOutsets * NextCW(edgeDatas.dxs) - toOutsets * edgeDatas.dxs;
  const auto outsetYs = fromOutsets * NextCW(edgeDatas.dys) - toOutsets * edgeDatas.dys;

  outInset = {vertices.xs - outsetXs, vertices.ys - outsetYs};
  outOutset = {vertices.xs + outsetXs, vertices.ys + outsetYs};
}

/**
 * Compute offset quad vertices using edge equation intersection (degenerate path).
 */
static inline Vertices4 OffsetQuadByIntersect(const EdgeEquations& edgeEqs,
                                              const Vec4& edgeOffset) {
  const auto offsetEdgeEqs = EdgeEquations{edgeEqs.as, edgeEqs.bs, edgeEqs.cs + edgeOffset};

  // Intersection of two lines a*x + b*y + c = 0:
  // denom = a0*b1 - b0*a1, x = (b0*c1 - c0*b1) / denom, y = (c0*a1 - a0*c1) / denom
  const auto denoms =
      offsetEdgeEqs.as * NextCW(offsetEdgeEqs.bs) - offsetEdgeEqs.bs * NextCW(offsetEdgeEqs.as);
  auto pxs =
      (offsetEdgeEqs.bs * NextCW(offsetEdgeEqs.cs) - offsetEdgeEqs.cs * NextCW(offsetEdgeEqs.bs)) /
      denoms;
  auto pys =
      (offsetEdgeEqs.cs * NextCW(offsetEdgeEqs.as) - offsetEdgeEqs.as * NextCW(offsetEdgeEqs.cs)) /
      denoms;

  const auto badMask = VecUtils::LessThan(VecUtils::Abs(denoms), DENOM_TOLERANCE);
  CorrectBadCoords(badMask, pxs, pys);

  // Assumes quad is rectangle-like with vertex 0 at top-left, so E0/E3 are vertical and E1/E2 are
  // horizontal. Calculate signed distances from each vertex to its non-adjacent edges.
  const auto edgeDistsV = VecUtils::Shuffle<3, 3, 0, 0>(offsetEdgeEqs.as) * pxs +
                          VecUtils::Shuffle<3, 3, 0, 0>(offsetEdgeEqs.bs) * pys +
                          VecUtils::Shuffle<3, 3, 0, 0>(offsetEdgeEqs.cs);
  const auto edgeDistsH = VecUtils::Shuffle<1, 2, 1, 2>(offsetEdgeEqs.as) * pxs +
                          VecUtils::Shuffle<1, 2, 1, 2>(offsetEdgeEqs.bs) * pys +
                          VecUtils::Shuffle<1, 2, 1, 2>(offsetEdgeEqs.cs);

  // Check if offset vertices are beyond their non-adjacent edges.
  const auto overEdgeV = VecUtils::LessThan(edgeDistsV, DIST_TOLERANCE);
  const auto overEdgeH = VecUtils::LessThan(edgeDistsH, DIST_TOLERANCE);
  const auto overBoth = VecUtils::And(overEdgeV, overEdgeH);
  const auto overAny = VecUtils::Or(overEdgeV, overEdgeH);

  // No vertex beyond any edge, offset result is still a quad.
  if (!VecUtils::Any(overAny)) {
    return {pxs, pys};
  }

  // Any vertex beyond both opposite edges means degeneration to a point.
  // All vertices beyond any opposite edge means degeneration to a line.
  if (VecUtils::Any(overBoth) || VecUtils::All(overAny)) {
    // TODO: Handle point/line degeneration properly.
    return {pxs, pys};
  }

  // Degeneration to triangle.
  CorrectTriangleDegeneration(offsetEdgeEqs, edgeDistsV, edgeDistsH, pxs, pys);

  return {pxs, pys};
}

/**
 * Compute AA vertices for degenerate quads.
 */
static inline void ComputeAAVerticesDegenerate(const Vertices4& vertices, const Vec4& edgeOffset,
                                               const EdgeEquations& edgeEqs, Vertices4& outInset,
                                               Vertices4& outOutset) {
  if (IsCollinear(vertices, edgeEqs)) {
    outInset = vertices;
    outOutset = vertices;
    return;
  }
  outInset = OffsetQuadByIntersect(edgeEqs, -edgeOffset);
  outOutset = OffsetQuadByIntersect(edgeEqs, edgeOffset);
}

/**
 * Writes a quad's vertices to the output buffer in Z-order.
 * @param vertices Output buffer for all vertex data.
 * @param index Current write position in vertices, updated after writing.
 * @param coord The 4 vertices of the quad in Z-order.
 * @param coverage Coverage value for AA (1.0 for inner, 0.0 for outer).
 * @param uvCoord Optional UV coordinates for texture mapping.
 * @param color Optional compressed color value to write.
 */
static inline void WriteQuadVertices(float* vertices, size_t& index, const Vertices4& coord,
                                     float coverage, const std::optional<Vertices4>& uvCoord,
                                     const std::optional<float>& color) {
  for (int i = 0; i < 4; ++i) {
    vertices[index++] = coord.xs[i];
    vertices[index++] = coord.ys[i];
    vertices[index++] = coverage;
    if (uvCoord) {
      vertices[index++] = uvCoord->xs[i];
      vertices[index++] = uvCoord->ys[i];
    }
    if (color) {
      vertices[index++] = *color;
    }
  }
}

/**
 * NonAAQuadsVertexProvider generates vertices for non-AA quad rendering.
 */
class NonAAQuadsVertexProvider : public QuadsVertexProvider {
 public:
  NonAAQuadsVertexProvider(PlacementArray<QuadRecord>&& quads, AAType aaType, bool hasColor,
                           bool hasUVCoord, std::shared_ptr<BlockAllocator> reference,
                           std::shared_ptr<ColorSpace> dstColorSpace)
      : QuadsVertexProvider(std::move(quads), aaType, hasColor, hasUVCoord, std::move(reference),
                            std::move(dstColorSpace)) {
  }

  size_t vertexCount() const override {
    // Each quad = 4 vertices, vertex data: position(2) + [uv(2)] + [color(1)]
    size_t perVertexCount = 2;
    if (_hasUVCoord) {
      perVertexCount += 2;
    }
    if (_hasColor) {
      perVertexCount += 1;
    }
    return quads.size() * 4 * perVertexCount;
  }

  void getVertices(float* vertices) const override {
    size_t index = 0;
    for (size_t i = 0; i < quads.size(); ++i) {
      auto& record = quads[i];
      float compressedColor = 0.f;
      if (_hasColor) {
        const auto uintColor = ToUintPMColor(record->color, nullptr);
        compressedColor = *reinterpret_cast<const float*>(&uintColor);
      }
      auto transformedQuad = record->quad;
      transformedQuad.transform(record->matrix);
      for (size_t j = 0; j < 4; ++j) {
        const auto& p = transformedQuad.point(j);
        vertices[index++] = p.x;
        vertices[index++] = p.y;
        if (_hasUVCoord) {
          const auto& uv = record->quad.point(j);
          vertices[index++] = uv.x;
          vertices[index++] = uv.y;
        }
        if (_hasColor) {
          vertices[index++] = compressedColor;
        }
      }
    }
  }
};

/**
 * AAQuadsVertexProvider generates vertices for per-edge AA quad rendering.
 */
class AAQuadsVertexProvider : public QuadsVertexProvider {
 public:
  AAQuadsVertexProvider(PlacementArray<QuadRecord>&& quads, AAType aaType, bool hasColor,
                        bool hasUVCoord, std::shared_ptr<BlockAllocator> reference,
                        std::shared_ptr<ColorSpace> dstColorSpace)
      : QuadsVertexProvider(std::move(quads), aaType, hasColor, hasUVCoord, std::move(reference),
                            std::move(dstColorSpace)) {
  }

  size_t vertexCount() const override {
    // Each AA quad = 8 vertices (4 inner + 4 outer)
    // Vertex data: position(2) + coverage(1) + [uv(2)] + [color(1)]
    size_t perVertexCount = 3;
    if (_hasUVCoord) {
      perVertexCount += 2;
    }
    if (_hasColor) {
      perVertexCount += 1;
    }
    return quads.size() * 8 * perVertexCount;
  }

  void getVertices(float* vertices) const override {
    size_t index = 0;
    for (size_t i = 0; i < quads.size(); ++i) {
      writeAAQuadVertices(vertices, index, *quads[i]);
    }
  }

 private:
  void writeAAQuadVertices(float* vertices, size_t& index, const QuadRecord& record) const {
    auto transformedQuad = record.quad;
    transformedQuad.transform(record.matrix);
    auto transformedVertices = ToVertices4(transformedQuad);

    auto edgeDatas = ComputeEdgeDatas(transformedVertices);
    auto aaFlags = record.aaFlags;
    auto edgeOffset = Vec4((aaFlags & QUAD_AA_FLAG_EDGE_0) ? AA_OFFSET : 0.0f,
                           (aaFlags & QUAD_AA_FLAG_EDGE_1) ? AA_OFFSET : 0.0f,
                           (aaFlags & QUAD_AA_FLAG_EDGE_2) ? AA_OFFSET : 0.0f,
                           (aaFlags & QUAD_AA_FLAG_EDGE_3) ? AA_OFFSET : 0.0f);

    Vertices4 insetCoord = {};
    Vertices4 outsetCoord = {};

    if (IsAADegenerate(edgeDatas, transformedQuad.isRect())) {
      auto edgeEqs = ComputeEdgeEquations(transformedVertices, edgeDatas);
      ComputeAAVerticesDegenerate(transformedVertices, edgeOffset, edgeEqs, insetCoord,
                                  outsetCoord);
    } else {
      ComputeAAVertices(transformedVertices, edgeOffset, edgeDatas, insetCoord, outsetCoord);
    }

    std::optional<Vertices4> insetUV = std::nullopt;
    std::optional<Vertices4> outsetUV = std::nullopt;
    if (_hasUVCoord) {
      auto invMatrix = record.matrix;
      if (invMatrix.invert(&invMatrix)) {
        insetUV = TransformVertices4(insetCoord, invMatrix);
        outsetUV = TransformVertices4(outsetCoord, invMatrix);
      } else {
        // Fallback to original quad coordinates if matrix is not invertible.
        DEBUG_ASSERT(false);
        insetUV = ToVertices4(record.quad);
        outsetUV = ToVertices4(record.quad);
      }
    }

    std::optional<float> color = std::nullopt;
    if (_hasColor) {
      const auto uintColor = ToUintPMColor(record.color, nullptr);
      color = *reinterpret_cast<const float*>(&uintColor);
    }

    WriteQuadVertices(vertices, index, insetCoord, 1.0f, insetUV, color);
    WriteQuadVertices(vertices, index, outsetCoord, 0.0f, outsetUV, color);
  }
};

PlacementPtr<QuadsVertexProvider> QuadsVertexProvider::MakeFrom(BlockAllocator* allocator,
                                                                const Rect& rect, AAType aaType,
                                                                const Color& color) {
  auto quad = Quad::MakeFrom(rect);
  auto record = allocator->make<QuadRecord>(quad, QUAD_AA_FLAG_ALL, color);
  std::vector<PlacementPtr<QuadRecord>> quads;
  quads.push_back(std::move(record));
  return MakeFrom(allocator, std::move(quads), aaType);
}

PlacementPtr<QuadsVertexProvider> QuadsVertexProvider::MakeFrom(
    BlockAllocator* allocator, std::vector<PlacementPtr<QuadRecord>>&& quads, AAType aaType,
    std::shared_ptr<ColorSpace> dstColorSpace) {
  if (quads.empty()) {
    return nullptr;
  }
  auto hasColor = false;
  auto hasUVCoord = false;
  const auto& firstColor = quads.front()->color;
  const auto& firstMatrix = quads.front()->matrix;
  for (size_t i = 1; i < quads.size() && !(hasColor && hasUVCoord); ++i) {
    hasColor = hasColor || quads[i]->color != firstColor;
    hasUVCoord = hasUVCoord || quads[i]->matrix != firstMatrix;
  }
  auto quadArray = allocator->makeArray(std::move(quads));
  if (aaType == AAType::Coverage) {
    return allocator->make<AAQuadsVertexProvider>(std::move(quadArray), aaType, hasColor,
                                                  hasUVCoord, allocator->addReference(),
                                                  std::move(dstColorSpace));
  }
  return allocator->make<NonAAQuadsVertexProvider>(std::move(quadArray), aaType, hasColor,
                                                   hasUVCoord, allocator->addReference(),
                                                   std::move(dstColorSpace));
}

QuadsVertexProvider::QuadsVertexProvider(PlacementArray<QuadRecord>&& quads, AAType aaType,
                                         bool hasColor, bool hasUVCoord,
                                         std::shared_ptr<BlockAllocator> reference,
                                         std::shared_ptr<ColorSpace> dstColorSpace)
    : VertexProvider(std::move(reference)), quads(std::move(quads)), _dstColorSpace(std::move(dstColorSpace)),
      _aaType(aaType), _hasColor(hasColor), _hasUVCoord(hasUVCoord) {
}

}  // namespace tgfx

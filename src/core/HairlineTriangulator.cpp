/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "HairlineTriangulator.h"
#include <cstddef>
#include "core/utils/PathUtils.h"
#include "core/utils/PointUtils.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Point.h"

namespace tgfx {

namespace {

// One pixel length for hairline cap expansion.
constexpr float PIXEL_LENGTH = 1.0f;
// Half pixel offset for AA edge rendering.
constexpr float HALF_PIXEL_LENGTH = PIXEL_LENGTH * 0.5f;

struct LineVertex {
  Point pos;
  float coverage;
};

struct ConicData {
  float KML[3] = {};
};

struct PaddingData {
  float bogus[4];
};

struct BezierVertex {
  Point pos;
  union {
    ConicData conic;
    Point quadCoord;
    PaddingData padding;
  };
  BezierVertex() : quadCoord() {
  }
};

// Quadratics are rendered as 5-sided polygons to bound the AA stroke around the center curve.
// The polygon is expanded so that the 1-pixel wide area around the curve is inside the poly.
//
// Control points layout (a, b, c are original points; a0, a1, b0, c0, c1 are expanded vertices):
//
//              b0
//              b
//
//     a0              c0
//      a            c
//       a1       c1
//
// Rendered as three triangles: (a0,a1,b0), (b0,c1,c0), (a1,c1,b0)
constexpr uint16_t QUAD_INDEX_BUFFER_PATTERN[] = {0, 1, 2, 2, 4, 3, 1, 4, 2};

constexpr int QUAD_NUM_INDICES = std::size(QUAD_INDEX_BUFFER_PATTERN);
constexpr int QUAD_NUM_VERTICES = 5;

std::vector<uint32_t> GetQuadsIndexBuffer(size_t numQuads) {
  std::vector<uint32_t> indices;
  indices.reserve(numQuads * QUAD_NUM_INDICES);

  for (uint32_t i = 0; i < numQuads; ++i) {
    uint32_t baseVertex = i * QUAD_NUM_VERTICES;
    for (auto idx : QUAD_INDEX_BUFFER_PATTERN) {
      indices.push_back(baseVertex + idx);
    }
  }

  return indices;
}

// Each line segment is rendered with 6 vertices for AA effect:
// - p0, p1: Inner vertices with alpha = 1 (on the line)
// - p2, p3, p4, p5: Outer vertices with alpha = 0 (offset 1 pixel perpendicular, 0.5 pixel parallel)
//
// Lines are rendered as:
//      *______________*
//      |\            /|
//      | \          / |
//      |  *--------*  |
//      | /          \ |
//      */____________\*
//
// Vertex layout:
//   p4                  p5
//        p0         p1
//   p2                  p3
//
// Rendered as six triangles (18 indices):
constexpr uint16_t LINE_INDEX_BUFFER_PATTERN[] = {0, 1, 3, 0, 3, 2, 0, 4, 5,
                                                  0, 5, 1, 0, 2, 4, 1, 5, 3};

constexpr int LINE_NUM_INDICES = std::size(LINE_INDEX_BUFFER_PATTERN);
constexpr int LINE_NUM_VERTICES = 6;

std::vector<uint32_t> GetLinesIndexBuffer(size_t numLines) {
  std::vector<uint32_t> indices;
  indices.reserve(numLines * LINE_NUM_INDICES);

  for (uint32_t i = 0; i < numLines; ++i) {
    uint32_t baseVertex = i * LINE_NUM_VERTICES;
    for (auto index : LINE_INDEX_BUFFER_PATTERN) {
      indices.push_back(baseVertex + index);
    }
  }

  return indices;
}

// Takes 178th time of logf on Z600 / VC2010
// Note: Caller must ensure x is a positive finite number.
int GetFloatExp(float x) {
  if (x <= 0 || !std::isfinite(x)) {
    return 0;
  }
  int exponent = 0;
  std::frexp(x, &exponent);
  return exponent - 1;
}

// Returns true if quad/conic is degenerate or close to it (should be approximated with lines).
// Returns false if the quad/conic is valid and should be rendered as a curve.
// If valid, distanceSquared is set to the squared distance from control point to the line.
bool IsDegenQuadOrConic(const Point p[3], float* distanceSquared) {
  constexpr float DegenerateToLineTol = PathUtils::DefaultTolerance;
  constexpr float DegenerateToLineTolSqd = DegenerateToLineTol * DegenerateToLineTol;

  if (PointUtils::DistanceSquared(p[0], p[1]) < DegenerateToLineTolSqd ||
      PointUtils::DistanceSquared(p[1], p[2]) < DegenerateToLineTolSqd) {
    return true;
  }

  *distanceSquared = PointUtils::DistanceToLineBetweenSquared(p[1], p[0], p[2]);
  if (*distanceSquared < DegenerateToLineTolSqd) {
    return true;
  }

  if (PointUtils::DistanceToLineBetweenSquared(p[2], p[1], p[0]) < DegenerateToLineTolSqd) {
    return true;
  }
  return false;
}

// we subdivide the quads to avoid huge overfill
// if it returns -1 then should be drawn as lines
int NumQuadSubdivs(const Point points[3]) {
  float dsqd;
  if (IsDegenQuadOrConic(points, &dsqd)) {
    return -1;
  }

  // tolerance of triangle height in pixels
  // tuned on windows  Quadro FX 380 / Z600
  // trade off of fill vs cpu time on verts
  // maybe different when do this using gpu (geo or tess shaders)
  constexpr float SUBDIV_TOLERANCE = 175.f;

  if (dsqd <= SUBDIV_TOLERANCE * SUBDIV_TOLERANCE) {
    return 0;
  } else {
    constexpr int MAX_SUB = 4;
    // subdividing the quad reduces d by 4. so we want x = log4(d/tol)
    // = log4(d*d/tol*tol)/2
    // = log2(d*d/tol*tol)

    // +1 since we're ignoring the mantissa contribution.
    int log = GetFloatExp(dsqd / (SUBDIV_TOLERANCE * SUBDIV_TOLERANCE)) + 1;
    log = std::min(std::max(0, log), MAX_SUB);
    return log;
  }
}

struct PathDecomposerContext {
  const Matrix& matrix;
  float capLength;
  std::vector<Point>& lines;
  std::vector<Point>& quads;
  std::vector<int>& quadSubdivCnts;
  uint32_t& totalQuadCount;
  int& verbsInContour;
  bool& seenZeroLengthVerb;
  Point& zeroVerbPt;
};

bool IsZeroLengthLine(const Point& p0, const Point& p1) {
  constexpr float POINT_EPSILON_SQD = 1e-12f;
  return PointUtils::DistanceSquared(p0, p1) < POINT_EPSILON_SQD;
}

void AddChoppedQuad(PathDecomposerContext& ctx, const Point devPts[3], bool isContourStart) {
  int subdiv = NumQuadSubdivs(devPts);
  DEBUG_ASSERT(subdiv >= -1);
  if (-1 == subdiv) {
    auto index = ctx.lines.size();
    ctx.lines.push_back(devPts[0]);
    ctx.lines.push_back(devPts[1]);
    ctx.lines.push_back(devPts[1]);
    ctx.lines.push_back(devPts[2]);
    if (isContourStart && IsZeroLengthLine(ctx.lines[index], ctx.lines[index + 1]) &&
        IsZeroLengthLine(ctx.lines[index + 2], ctx.lines[index + 3])) {
      ctx.seenZeroLengthVerb = true;
      ctx.zeroVerbPt = ctx.lines[index];
    }
  } else {
    ctx.quads.push_back(devPts[0]);
    ctx.quads.push_back(devPts[1]);
    ctx.quads.push_back(devPts[2]);
    ctx.quadSubdivCnts.push_back(subdiv);
    ctx.totalQuadCount += 1 << subdiv;
  }
}

void AddSrcChoppedQuad(PathDecomposerContext& ctx, const Point srcSpaceQuadPts[3],
                       bool isContourStart) {
  Point devPts[3];
  ctx.matrix.mapPoints(devPts, srcSpaceQuadPts, 3);
  AddChoppedQuad(ctx, devPts, isContourStart);
}

void ProcessPathVerb(PathVerb verb, const Point points[4], void* userData) {
  auto* ctx = static_cast<PathDecomposerContext*>(userData);
  switch (verb) {
    case PathVerb::Move: {
      if (ctx->seenZeroLengthVerb && ctx->verbsInContour == 1 && ctx->capLength > 0) {
        ctx->lines.emplace_back(ctx->zeroVerbPt.x - ctx->capLength, ctx->zeroVerbPt.y);
        ctx->lines.emplace_back(ctx->zeroVerbPt.x - ctx->capLength, ctx->zeroVerbPt.y);
      }
      ctx->verbsInContour = 0;
      ctx->seenZeroLengthVerb = false;
      break;
    }
    case PathVerb::Line: {
      Point devPoints[2];
      ctx->matrix.mapPoints(devPoints, points, 2);
      ctx->lines.push_back(devPoints[0]);
      ctx->lines.push_back(devPoints[1]);
      if (ctx->verbsInContour == 0 && IsZeroLengthLine(devPoints[0], devPoints[1])) {
        ctx->seenZeroLengthVerb = true;
        ctx->zeroVerbPt = devPoints[0];
      }
      ctx->verbsInContour++;
      break;
    }
    case PathVerb::Quad: {
      Point choppedPts[5];
      int n = PathUtils::ChopQuadAtMaxCurvature(points, choppedPts);
      for (int i = 0; i < n; ++i) {
        AddSrcChoppedQuad(*ctx, choppedPts + (i * 2), !ctx->verbsInContour && 0 == i);
      }
      ctx->verbsInContour++;
      break;
    }
    case PathVerb::Cubic: {
      Point devPts[4];
      ctx->matrix.mapPoints(devPts, points, 4);
      auto quadPoints = PathUtils::ConvertCubicToQuads(devPts, 1.f);
      for (uint32_t i = 0; i < quadPoints.size(); i += 3) {
        AddChoppedQuad(*ctx, &quadPoints[i], !ctx->verbsInContour && 0 == i);
      }
      ctx->verbsInContour++;
      break;
    }
    case PathVerb::Close: {
      if (ctx->capLength > 0) {
        if (ctx->seenZeroLengthVerb && ctx->verbsInContour == 1) {
          ctx->lines.emplace_back(ctx->zeroVerbPt.x - ctx->capLength, ctx->zeroVerbPt.y);
          ctx->lines.emplace_back(ctx->zeroVerbPt.x - ctx->capLength, ctx->zeroVerbPt.y);
        } else if (ctx->verbsInContour == 0) {
          Point devPts[2];
          ctx->matrix.mapPoints(devPts, points, 1);
          devPts[1] = devPts[0];
          ctx->lines.emplace_back(devPts[0].x - ctx->capLength, devPts[0].y);
          ctx->lines.emplace_back(devPts[1].x - ctx->capLength, devPts[1].y);
        }
      }
      break;
    }
  }
}

/**
 * Generates the lines and quads to be rendered. Lines are always recorded in
 * device space. We will do a device space bloat to account for the 1pixel
 * thickness.
 * Quads are recorded in device space unless m contains
 * perspective, then in they are in src space. We do this because we will
 * subdivide large quads to reduce over-fill. This subdivision has to be
 * performed before applying the perspective matrix.
 */
uint32_t GatherLinesAndQuads(const Path& path, const Matrix& matrix, float capLength,
                             std::vector<Point>& lines, std::vector<Point>& quads,
                             std::vector<int>& quadSubdivCnts) {
  uint32_t totalQuadCount = 0;
  int verbsInContour = 0;
  bool seenZeroLengthVerb = false;
  Point zeroVerbPt;

  PathDecomposerContext ctx = {matrix,         capLength,          lines,
                               quads,          quadSubdivCnts,     totalQuadCount,
                               verbsInContour, seenZeroLengthVerb, zeroVerbPt};

  path.decompose(ProcessPathVerb, &ctx);

  if (seenZeroLengthVerb && verbsInContour == 1 && capLength > 0) {
    lines.emplace_back(zeroVerbPt.x - capLength, zeroVerbPt.y);
    lines.emplace_back(zeroVerbPt.x + capLength, zeroVerbPt.y);
  }
  return totalQuadCount;
}

void AddLine(const Point p[2], LineVertex** vert) {
  const auto a = p[0];
  const auto b = p[1];

  auto ortho = Point::Zero();
  auto vec = b - a;
  float lengthSqd = PointUtils::LengthSquared(vec);
  if (PointUtils::SetLength(vec, HALF_PIXEL_LENGTH)) {
    // Create a vector orthogonal to 'vec'. The factor of 2.0 compensates for the fact that
    // 'vec' has been normalized to HALF_PIXEL_LENGTH, so we need to scale by 2 to get a
    // full pixel perpendicular offset.
    ortho.x = 2.0f * vec.y;
    ortho.y = -2.0f * vec.x;

    // For lines shorter than a pixel, modulate coverage by the actual length to ensure
    // correct antialiasing for subpixel movements. For normal lines, use full coverage.
    float coverage = (lengthSqd < 1.0f) ? std::sqrt(lengthSqd) : 1.0f;
    Point innerOffset = vec;

    // The inner vertices are inset half a pixel along the line direction.
    // For short lines, both inner vertices converge toward the center.
    (*vert)[0].pos = (lengthSqd < 1.0f) ? (b - innerOffset) : (a + innerOffset);
    (*vert)[0].coverage = coverage;
    (*vert)[1].pos = (lengthSqd < 1.0f) ? (a + innerOffset) : (b - innerOffset);
    (*vert)[1].coverage = coverage;

    // The outer vertices are outset half a pixel along the line and a full pixel orthogonally.
    (*vert)[2].pos = a - vec + ortho;
    (*vert)[2].coverage = 0;
    (*vert)[3].pos = b + vec + ortho;
    (*vert)[3].coverage = 0;
    (*vert)[4].pos = a - vec - ortho;
    (*vert)[4].coverage = 0;
    (*vert)[5].pos = b + vec - ortho;
    (*vert)[5].coverage = 0;
  } else {
    // just make it degenerate and likely offscreen
    for (int i = 0; i < LINE_NUM_VERTICES; ++i) {
      (*vert)[i].pos.set(0.f, 0.f);
    }
  }
  *vert += LINE_NUM_VERTICES;
}

Point IntersectLines(const Point& pointA, const Point& normA, const Point& pointB,
                     const Point& normB) {
  auto result = Point::Zero();

  auto lineAW = -Point::DotProduct(normA, pointA);
  auto lineBW = -Point::DotProduct(normB, pointB);
  float wInv = (normA.x * normB.y) - (normA.y * normB.x);
  wInv = 1.0f / wInv;
  if (!std::isfinite(wInv)) {
    // lines are parallel, pick the point in between
    result = (pointA + pointB) * 0.5f;
  } else {
    result.x = normA.y * lineBW - lineAW * normB.y;
    result.x *= wInv;

    result.y = lineAW * normB.x - normA.x * lineBW;
    result.y *= wInv;
  }
  return result;
}

bool BloatQuad(const Point qpts[3], BezierVertex verts[QUAD_NUM_VERTICES]) {
  // original quad is specified by tri a,b,c
  Point a = qpts[0];
  Point b = qpts[1];
  Point c = qpts[2];

  // make a new poly where we replace a and c by a 1-pixel wide edges orthog
  // to edges ab and bc:
  //
  //   before       |        after
  //                |              b0
  //         b      |
  //                |
  //                |     a0            c0
  // a         c    |        a1       c1
  //
  // edges a0->b0 and b0->c0 are parallel to original edges a->b and b->c,
  // respectively.
  BezierVertex& a0 = verts[0];
  BezierVertex& a1 = verts[1];
  BezierVertex& b0 = verts[2];
  BezierVertex& c0 = verts[3];
  BezierVertex& c1 = verts[4];

  auto ab = b - a;
  auto ac = c - a;
  auto cb = b - c;

  // After the transform (or due to floating point math) we might have a line,
  // try to do something reasonable
  bool abNormalized = ab.normalize();
  bool cbNormalized = cb.normalize();
  if (!abNormalized && !cbNormalized) {
    return false;
  }
  if (!abNormalized) {
    ab = cb;
  }
  if (!cbNormalized) {
    cb = ab;
  }

  auto abN = PointUtils::MakeOrthogonal(ab, PointUtils::Side::Left);
  if (Point::DotProduct(abN, ac) > 0) {
    abN.x = -abN.x;
    abN.y = -abN.y;
  }
  auto cbN = PointUtils::MakeOrthogonal(cb, PointUtils::Side::Left);
  if (Point::DotProduct(cbN, ac) < 0) {
    cbN.x = -cbN.x;
    cbN.y = -cbN.y;
  }
  a0.pos = a + abN;
  a1.pos = a - abN;
  c0.pos = c + cbN;
  c1.pos = c - cbN;
  b0.pos = IntersectLines(a0.pos, abN, c0.pos, cbN);
  return true;
}

void SetUvQuad(const Point qpts[3], BezierVertex verts[QUAD_NUM_VERTICES]) {
  // this should be in the src space, not dev coords, when we have perspective
  QuadUVMatrix DevToUV(qpts);
  DevToUV.apply(verts, QUAD_NUM_VERTICES, sizeof(BezierVertex), sizeof(Point));
}

void AddQuad(const Point points[3], int subdiv, BezierVertex** vert) {
  DEBUG_ASSERT(subdiv >= 0);
  // temporary vertex storage to avoid reading the vertex buffer
  BezierVertex outVerts[QUAD_NUM_VERTICES] = {};

  // storage for the chopped quad
  // pts 0,1,2 are the first quad, and 2,3,4 the second quad
  // start off with our original curve in the second quad slot
  Point choppedQuadPoints[5] = {Point::Zero(), Point::Zero(), points[0], points[1], points[2]};

  int stepCount = 1 << subdiv;
  while (stepCount > 1) {
    // The general idea is:
    // * chop the quad using pts 2,3,4 as the input
    // * write out verts using pts 0,1,2
    // * now 2,3,4 is the remainder of the curve, chop again until all subdivisions are done
    float h = 1.f / static_cast<float>(stepCount);
    PathUtils::ChopQuadAt(&choppedQuadPoints[2], choppedQuadPoints, h);

    if (BloatQuad(choppedQuadPoints, outVerts)) {
      SetUvQuad(choppedQuadPoints, outVerts);
      memcpy(*vert, outVerts, QUAD_NUM_VERTICES * sizeof(BezierVertex));
      *vert += QUAD_NUM_VERTICES;
    }
    --stepCount;
  }

  // finish up, write out the final quad
  if (BloatQuad(&choppedQuadPoints[2], outVerts)) {
    SetUvQuad(&choppedQuadPoints[2], outVerts);
    memcpy(*vert, outVerts, QUAD_NUM_VERTICES * sizeof(BezierVertex));
    *vert += QUAD_NUM_VERTICES;
  }
}

}  // namespace

HairlineTriangulator::HairlineTriangulator(std::shared_ptr<Shape> shape, bool hasCap)
    : shape(std::move(shape)), hasCap(hasCap) {
}

std::shared_ptr<HairlineBuffer> HairlineTriangulator::getData() const {
  auto path = shape->getPath();

  // Reserve space based on empirical testing. For typical paths, 128 points provide a good
  // balance between avoiding frequent reallocations and not over-allocating memory. This is
  // a reasonable starting point since most common UI shapes (rounded rects, icons, etc.)
  // generate fewer than 64 line segments and 32 quads.
  std::vector<Point> lines;
  lines.reserve(128);
  std::vector<Point> quads;
  quads.reserve(128);
  std::vector<int> quadSubdivs;
  float capLength = hasCap ? PIXEL_LENGTH : 0.0f;
  auto quadCount = GatherLinesAndQuads(path, Matrix::I(), capLength, lines, quads, quadSubdivs);

  auto lineCount = lines.size() / 2;
  constexpr int MAX_LINES = INT32_MAX / LINE_NUM_VERTICES;
  constexpr int MAX_QUADS = INT32_MAX / QUAD_NUM_VERTICES;
  if (lineCount > MAX_LINES || quadCount > MAX_QUADS) {
    return {};
  }

  std::shared_ptr<Data> lineVerticesData = nullptr;
  std::shared_ptr<Data> lineIndicesData = nullptr;
  if (lineCount > 0) {
    std::vector<uint32_t> lineIndices = GetLinesIndexBuffer(lineCount);
    auto lineVertexTotalSize = lineCount * LINE_NUM_VERTICES * sizeof(LineVertex);
    auto lineVertexBuffer = std::unique_ptr<uint8_t[]>(new uint8_t[lineVertexTotalSize]);
    auto vertPtr = reinterpret_cast<LineVertex*>(lineVertexBuffer.get());
    for (size_t i = 0; i < lineCount; ++i) {
      AddLine(&lines[2 * i], &vertPtr);
    }
    lineVerticesData = Data::MakeAdopted(lineVertexBuffer.release(), lineVertexTotalSize);
    lineIndicesData = Data::MakeWithCopy(lineIndices.data(), lineIndices.size() * sizeof(uint32_t));
  }

  std::shared_ptr<Data> quadVerticesData = nullptr;
  std::shared_ptr<Data> quadIndicesData = nullptr;
  if (quadCount > 0) {
    std::vector<uint32_t> quadIndices = GetQuadsIndexBuffer(quadCount);
    auto quadVertexTotalSize = static_cast<size_t>(quadCount) * QUAD_NUM_VERTICES * sizeof(BezierVertex);
    auto quadVertexBuffer = std::unique_ptr<uint8_t[]>(new uint8_t[quadVertexTotalSize]);
    auto vertPointer = reinterpret_cast<BezierVertex*>(quadVertexBuffer.get());
    for (size_t i = 0; i < quads.size() / 3; ++i) {
      AddQuad(&quads[3 * i], quadSubdivs[i], &vertPointer);
    }
    quadVerticesData = Data::MakeAdopted(quadVertexBuffer.release(), quadVertexTotalSize);
    quadIndicesData = Data::MakeWithCopy(quadIndices.data(), quadIndices.size() * sizeof(uint32_t));
  }
  return std::make_shared<HairlineBuffer>(std::move(lineVerticesData), std::move(lineIndicesData),
                                          std::move(quadVerticesData), std::move(quadIndicesData));
}

}  // namespace tgfx
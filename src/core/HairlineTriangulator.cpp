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
#include "core/NoConicsPathIterator.h"
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
  BezierVertex() : pos(), quadCoord() {
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
constexpr int QUAD_NUM_VERTICES = 5;

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
constexpr int LINE_NUM_VERTICES = 6;

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
  constexpr float DEGENERATE_TO_LINE_TOL = PathUtils::DefaultTolerance;
  constexpr float DEGENERATE_TO_LINE_TOL_SQD = DEGENERATE_TO_LINE_TOL * DEGENERATE_TO_LINE_TOL;

  if (PointUtils::DistanceSquared(p[0], p[1]) < DEGENERATE_TO_LINE_TOL_SQD ||
      PointUtils::DistanceSquared(p[1], p[2]) < DEGENERATE_TO_LINE_TOL_SQD) {
    return true;
  }

  *distanceSquared = PointUtils::DistanceToLineBetweenSquared(p[1], p[0], p[2]);
  if (*distanceSquared < DEGENERATE_TO_LINE_TOL_SQD) {
    return true;
  }

  if (PointUtils::DistanceToLineBetweenSquared(p[2], p[1], p[0]) < DEGENERATE_TO_LINE_TOL_SQD) {
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

/**
 * PathDecomposer decomposes a Path into lines and quadratic bezier curves for hairline rendering.
 *
 * Lines are always recorded in device space. Quads are recorded in device space unless the matrix
 * contains perspective, in which case they are in source space. This is because large quads need
 * to be subdivided to reduce over-fill, and this subdivision must be performed before applying
 * the perspective matrix.
 */
class PathDecomposer {
 public:
  PathDecomposer(const Matrix& matrix, float capLength)
      : matrix_(matrix), capLength_(capLength) {
    // Reserve space based on empirical testing. For typical paths, 128 points provide a good
    // balance between avoiding frequent reallocations and not over-allocating memory.
    lines_.reserve(128);
    quads_.reserve(128);
  }

  uint32_t decompose(const Path& path) {
    NoConicsPathIterator iterator(path);
    for (const auto& segment : iterator) {
      switch (segment.verb) {
        case PathVerb::Move:
          processMove();
          break;
        case PathVerb::Line:
          processLine(segment.points);
          break;
        case PathVerb::Quad:
          processQuad(segment.points);
          break;
        case PathVerb::Cubic:
          processCubic(segment.points);
          break;
        case PathVerb::Close:
          processClose(segment.points);
          break;
        default:
          break;
      }
    }
    finalizeLastContour();
    return totalQuadCount_;
  }

  std::vector<Point>& lines() {
    return lines_;
  }
  std::vector<Point>& quads() {
    return quads_;
  }
  std::vector<int>& quadSubdivCounts() {
    return quadSubdivCounts_;
  }

 private:
  static constexpr float POINT_EPSILON_SQD = 1e-12f;

  static bool isZeroLengthLine(const Point& p0, const Point& p1) {
    return PointUtils::DistanceSquared(p0, p1) < POINT_EPSILON_SQD;
  }

  void addChoppedQuad(const Point devPts[3], bool isContourStart) {
    int subdiv = NumQuadSubdivs(devPts);
    DEBUG_ASSERT(subdiv >= -1);
    if (subdiv == -1) {
      auto index = lines_.size();
      lines_.push_back(devPts[0]);
      lines_.push_back(devPts[1]);
      lines_.push_back(devPts[1]);
      lines_.push_back(devPts[2]);
      if (isContourStart && isZeroLengthLine(lines_[index], lines_[index + 1]) &&
          isZeroLengthLine(lines_[index + 2], lines_[index + 3])) {
        seenZeroLengthVerb_ = true;
        zeroVerbPt_ = lines_[index];
      }
    } else {
      quads_.push_back(devPts[0]);
      quads_.push_back(devPts[1]);
      quads_.push_back(devPts[2]);
      quadSubdivCounts_.push_back(subdiv);
      totalQuadCount_ += 1 << subdiv;
    }
  }

  void addSrcChoppedQuad(const Point srcSpaceQuadPts[3], bool isContourStart) {
    Point devPts[3];
    matrix_.mapPoints(devPts, srcSpaceQuadPts, 3);
    addChoppedQuad(devPts, isContourStart);
  }

  void addZeroLengthCap() {
    if (capLength_ > 0 && seenZeroLengthVerb_ && verbsInContour_ == 1) {
      lines_.emplace_back(zeroVerbPt_.x - capLength_, zeroVerbPt_.y);
      lines_.emplace_back(zeroVerbPt_.x + capLength_, zeroVerbPt_.y);
    }
  }

  void finalizeLastContour() {
    addZeroLengthCap();
  }

  void processMove() {
    addZeroLengthCap();
    verbsInContour_ = 0;
    seenZeroLengthVerb_ = false;
  }

  void processLine(const Point points[2]) {
    Point devPoints[2];
    matrix_.mapPoints(devPoints, points, 2);
    lines_.push_back(devPoints[0]);
    lines_.push_back(devPoints[1]);
    if (verbsInContour_ == 0 && isZeroLengthLine(devPoints[0], devPoints[1])) {
      seenZeroLengthVerb_ = true;
      zeroVerbPt_ = devPoints[0];
    }
    verbsInContour_++;
  }

  void processQuad(const Point points[3]) {
    Point choppedPts[5];
    int n = PathUtils::ChopQuadAtMaxCurvature(points, choppedPts);
    for (int i = 0; i < n; ++i) {
      addSrcChoppedQuad(choppedPts + (i * 2), verbsInContour_ == 0 && i == 0);
    }
    verbsInContour_++;
  }

  void processCubic(const Point points[4]) {
    Point devPts[4];
    matrix_.mapPoints(devPts, points, 4);
    auto quadPoints = PathUtils::ConvertCubicToQuads(devPts, 1.f);
    for (size_t i = 0; i < quadPoints.size(); i += 3) {
      addChoppedQuad(&quadPoints[i], verbsInContour_ == 0 && i == 0);
    }
    verbsInContour_++;
  }

  void processClose(const Point points[1]) {
    if (capLength_ > 0) {
      if (seenZeroLengthVerb_ && verbsInContour_ == 1) {
        lines_.emplace_back(zeroVerbPt_.x - capLength_, zeroVerbPt_.y);
        lines_.emplace_back(zeroVerbPt_.x + capLength_, zeroVerbPt_.y);
      } else if (verbsInContour_ == 0) {
        Point devPt;
        matrix_.mapPoints(&devPt, points, 1);
        lines_.emplace_back(devPt.x - capLength_, devPt.y);
        lines_.emplace_back(devPt.x + capLength_, devPt.y);
      }
    }
  }

  // Configuration (immutable after construction)
  const Matrix& matrix_;
  float capLength_;

  // Output buffers
  std::vector<Point> lines_;
  std::vector<Point> quads_;
  std::vector<int> quadSubdivCounts_;

  // Decomposition state
  uint32_t totalQuadCount_ = 0;
  int verbsInContour_ = 0;
  bool seenZeroLengthVerb_ = false;
  Point zeroVerbPt_;
};

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

  float capLength = hasCap ? PIXEL_LENGTH : 0.0f;
  PathDecomposer decomposer(Matrix::I(), capLength);
  auto quadCount = decomposer.decompose(path);

  auto& lines = decomposer.lines();
  auto& quads = decomposer.quads();
  auto& quadSubdivs = decomposer.quadSubdivCounts();

  auto lineCount = lines.size() / 2;
  constexpr int MAX_LINES = INT32_MAX / LINE_NUM_VERTICES;
  constexpr int MAX_QUADS = INT32_MAX / QUAD_NUM_VERTICES;
  if (lineCount > MAX_LINES || quadCount > MAX_QUADS) {
    return {};
  }

  std::shared_ptr<Data> lineVerticesData = nullptr;
  if (lineCount > 0) {
    auto lineVertexTotalSize = lineCount * LINE_NUM_VERTICES * sizeof(LineVertex);
    auto lineVertexBuffer = std::unique_ptr<uint8_t[]>(new uint8_t[lineVertexTotalSize]);
    auto vertPtr = reinterpret_cast<LineVertex*>(lineVertexBuffer.get());
    for (size_t i = 0; i < lineCount; ++i) {
      AddLine(&lines[2 * i], &vertPtr);
    }
    lineVerticesData = Data::MakeAdopted(lineVertexBuffer.release(), lineVertexTotalSize);
  }

  std::shared_ptr<Data> quadVerticesData = nullptr;
  if (quadCount > 0) {
    auto quadVertexTotalSize =
        static_cast<size_t>(quadCount) * QUAD_NUM_VERTICES * sizeof(BezierVertex);
    auto quadVertexBuffer = std::unique_ptr<uint8_t[]>(new uint8_t[quadVertexTotalSize]);
    auto vertPointer = reinterpret_cast<BezierVertex*>(quadVertexBuffer.get());
    for (size_t i = 0; i < quads.size() / 3; ++i) {
      AddQuad(&quads[3 * i], quadSubdivs[i], &vertPointer);
    }
    quadVerticesData = Data::MakeAdopted(quadVertexBuffer.release(), quadVertexTotalSize);
  }
  return std::make_shared<HairlineBuffer>(std::move(lineVerticesData), std::move(quadVerticesData));
}

}  // namespace tgfx
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

#include "ShapeBezierRasterizer.h"
#include <vector>
#include "core/NoConicsPathIterator.h"
#include "core/utils/PathUtils.h"
#include "tgfx/core/Point.h"

namespace tgfx {

namespace {

// Each vertex = position (Float2) + klm (Float3) = 5 floats = 20 bytes.
// The third KLM component is reserved for the cubic Loop-Blinn extension and is always zero
// in the current quadratic-only implementation.
struct Vertex {
  float x;
  float y;
  float u;
  float v;
  float w;  // reserved for cubic KLM extension; always 0 for quadratic path segments
};

constexpr float kFillU = 1.0f;
constexpr float kFillV = 1.0f;

// Appends one ABO "fill" triangle. O is the shared fan origin; the UV (1, 1) channel makes
// the fragment test `u*u - v > 0` evaluate to zero, which means the fragment survives and
// contributes to the stencil fill. Mirrors the line triangle in demo/vertexBuffer.js.
void EmitFillTriangle(std::vector<Vertex>& out, const Point& a, const Point& b, const Point& o) {
  out.push_back({a.x, a.y, kFillU, kFillV, 0.0f});
  out.push_back({b.x, b.y, kFillU, kFillV, 0.0f});
  out.push_back({o.x, o.y, kFillU, kFillV, 0.0f});
}

// Appends the two triangles for a quadratic bezier segment in the style of
// demo/vertexBuffer.js: ABC is the curve triangle with canonical UVs (0,0)/(0.5,0)/(1,1), and
// ACO closes the fan back to the origin with fill UVs.
void EmitQuadTriangles(std::vector<Vertex>& out, const Point& a, const Point& b, const Point& c,
                       const Point& o) {
  out.push_back({a.x, a.y, 0.0f, 0.0f, 0.0f});
  out.push_back({b.x, b.y, 0.5f, 0.0f, 0.0f});
  out.push_back({c.x, c.y, 1.0f, 1.0f, 0.0f});
  EmitFillTriangle(out, a, c, o);
}

// PathDecomposer walks the shape's path and emits the bezier-rasterizer vertex stream
// directly into the caller-provided output buffer. Conics are expanded into quads through
// NoConicsPathIterator. Cubics are converted to a sequence of quads via
// PathUtils::ConvertCubicToQuads. Degenerate quads (collinear control points) are demoted
// to a straight line so each emitted primitive uses the cheapest vertex layout possible.
//
// This fuses the previously-separate decompose and emit stages: instead of buffering
// `lines_`/`quads_` and replaying them into vertices afterwards, every line/quad is
// translated to its 3- or 6-vertex contribution as soon as it is recognised. That removes
// one round of per-control-point push_back hops and a pair of intermediate std::vector
// allocations.
class PathDecomposer {
 public:
  PathDecomposer(std::vector<Vertex>* vertices, const Point& origin)
      : vertices_(vertices), origin_(origin) {
  }

  void decompose(const Path& path) {
    NoConicsPathIterator iterator(path);
    for (const auto& segment : iterator) {
      switch (segment.verb) {
        case PathVerb::Move:
          processMove(segment.points);
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
          processClose();
          break;
        default:
          break;
      }
    }
  }

 private:
  static constexpr float kPointEpsilonSquared = 1e-12f;

  static bool IsZeroLengthLine(const Point& a, const Point& b) {
    auto dx = a.x - b.x;
    auto dy = a.y - b.y;
    return dx * dx + dy * dy < kPointEpsilonSquared;
  }

  // Returns true if the three control points of a quadratic bezier are (near) collinear, in
  // which case rendering it as a straight line from A to C is exact.
  static bool IsQuadDegenerate(const Point pts[3]) {
    auto ab = Point::Make(pts[1].x - pts[0].x, pts[1].y - pts[0].y);
    auto ac = Point::Make(pts[2].x - pts[0].x, pts[2].y - pts[0].y);
    auto cross = ab.x * ac.y - ab.y * ac.x;
    return cross * cross < kPointEpsilonSquared;
  }

  void addLine(const Point& a, const Point& b) {
    if (IsZeroLengthLine(a, b)) {
      return;
    }
    EmitFillTriangle(*vertices_, a, b, origin_);
  }

  void addQuad(const Point pts[3]) {
    if (IsQuadDegenerate(pts)) {
      addLine(pts[0], pts[2]);
      return;
    }
    EmitQuadTriangles(*vertices_, pts[0], pts[1], pts[2], origin_);
  }

  void processMove(const Point points[1]) {
    startPoint_ = points[0];
    currentPoint_ = points[0];
    contourOpen_ = true;
  }

  void processLine(const Point points[2]) {
    addLine(points[0], points[1]);
    currentPoint_ = points[1];
  }

  void processQuad(const Point points[3]) {
    // Chop at the max-curvature parameter to avoid self-intersecting sub-curves; the fragment
    // test `u*u - v > 0` is undefined on self-intersecting quadratics.
    Point chopped[5];
    int count = PathUtils::ChopQuadAtMaxCurvature(points, chopped);
    for (int i = 0; i < count; ++i) {
      addQuad(chopped + i * 2);
    }
    currentPoint_ = points[2];
  }

  void processCubic(const Point points[4]) {
    // Port of HairlineTriangulator's cubic handling: lower cubics into a sequence of quads.
    // tolerance=1.0 matches the hairline path; tighter tolerance produces more quads.
    cubicQuads_.clear();
    PathUtils::ConvertCubicToQuads(points, 1.0f, &cubicQuads_);
    for (size_t i = 0; i + 2 < cubicQuads_.size(); i += 3) {
      addQuad(&cubicQuads_[i]);
    }
    currentPoint_ = points[3];
  }

  void processClose() {
    if (contourOpen_) {
      addLine(currentPoint_, startPoint_);
      currentPoint_ = startPoint_;
    }
    contourOpen_ = false;
  }

  std::vector<Vertex>* vertices_ = nullptr;
  Point origin_ = {0.0f, 0.0f};
  // Scratch buffer reused across processCubic calls to avoid repeated heap allocations.
  std::vector<Point> cubicQuads_ = {};
  Point startPoint_ = {0.0f, 0.0f};
  Point currentPoint_ = {0.0f, 0.0f};
  bool contourOpen_ = false;
};

}  // namespace

ShapeBezierRasterizer::ShapeBezierRasterizer(std::shared_ptr<Shape> shape)
    : shape(std::move(shape)) {
}

bool ShapeBezierRasterizer::asyncSupport() const {
#if defined(TGFX_BUILD_FOR_WEB) && !defined(TGFX_USE_FREETYPE)
  return false;
#else
  return true;
#endif
}

std::shared_ptr<ShapeBezierBuffer> ShapeBezierRasterizer::getData() const {
  if (shape == nullptr) {
    return nullptr;
  }
  auto path = shape->getPath();
  if (path.isEmpty()) {
    return nullptr;
  }

  // Fan origin O: the centre of the path's bounding box. Any point in the plane produces the
  // same final stencil result (the ACO/ABO triangles carry the neutral fill UV (1,1) so they
  // contribute uniformly), but picking the centre keeps the fill triangles small and reduces
  // fragment-shader invocations — matching the optimisation used in demo/vertexBuffer.js.
  // Computed before decomposition so the decomposer can emit vertices directly to the
  // output buffer in a single pass.
  auto bounds = shape->getBounds();
  if (bounds.isEmpty()) {
    return nullptr;
  }
  Point origin = {(bounds.left + bounds.right) * 0.5f, (bounds.top + bounds.bottom) * 0.5f};

  // Each verb (line/quad/cubic/close) typically emits 3 to ~24 vertices after the various
  // subdivision steps; 12 is a deliberate underestimate that covers line/close exactly and
  // catches most quad cases without one chop. Cubic-heavy paths may still trigger a single
  // capacity grow, which is much cheaper than allocating a worst-case (~48 verts/verb)
  // buffer up front.
  std::vector<Vertex> vertices;
  vertices.reserve(static_cast<size_t>(path.countVerbs()) * 12);

  PathDecomposer decomposer(&vertices, origin);
  decomposer.decompose(path);
  if (vertices.empty()) {
    return nullptr;
  }

  auto data = Data::MakeWithCopy(vertices.data(), vertices.size() * sizeof(Vertex));
  if (data == nullptr) {
    return nullptr;
  }
  return std::make_shared<ShapeBezierBuffer>(std::move(data), vertices.size());
}
}  // namespace tgfx

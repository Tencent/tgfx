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

#include "StencilCoverPathTessellator.h"
#include <vector>
#include "core/NoConicsPathIterator.h"
#include "core/utils/PathUtils.h"
#include "tgfx/core/Point.h"

namespace tgfx {

namespace {

// Each vertex = position (Float2) + klm (Float3) = 5 floats = 20 bytes. The KLM channels
// carry the Loop-Blinn implicit-curve coefficients (k, l, m). For the quadratic-only path
// implemented here, the fragment shader tests `k*k - l > 0 ⇒ outside` and m is unused — it
// is reserved for the cubic Loop-Blinn extension and is always zero in this build.
struct Vertex {
  float x;
  float y;
  float u;  // k
  float v;  // l
  float w;  // m — reserved for cubic Loop-Blinn extension; always 0 for the quadratic test
};

constexpr float FILL_U = 1.0f;
constexpr float FILL_V = 1.0f;

// Appends a fan-fill triangle from `a` to `b` back to the shared fan origin `o`. All three
// vertices get the "neutral fill" KLM (1, 1, _), so the fragment test `k*k - l > 0` reduces
// to `1 - 1 > 0 ⇒ false` everywhere on the triangle and **no** pixel is discarded — i.e. the
// triangle contributes a uniform fill region to the stencil pass. Callers supply their own
// pair of perimeter points (a line segment's endpoints, or the chord that closes a quad's
// fan), while the third vertex is always the fan origin. Mirrors the line triangle in
// demo/vertexBuffer.js.
void EmitFillTriangle(std::vector<Vertex>& out, const Point& a, const Point& b, const Point& o) {
  out.push_back({a.x, a.y, FILL_U, FILL_V, 0.0f});
  out.push_back({b.x, b.y, FILL_U, FILL_V, 0.0f});
  out.push_back({o.x, o.y, FILL_U, FILL_V, 0.0f});
}

// Appends the two triangles for a quadratic bezier segment.
//
// Triangle ABC is the "curve triangle" enclosing the bezier from A (start) via B (control)
// to C (end). The KLM values (0,0,_) / (0.5,0,_) / (1,1,_) at A/B/C are the canonical
// quadratic mapping from Loop-Blinn 2005 (§3.1): when these three values are linearly
// interpolated across the triangle by the GPU, the locus `k*k = l` traces exactly the
// quadratic bezier curve AB-C in screen space. The fragment shader's test `k*k - l > 0`
// therefore discards every pixel sitting on the convex side of the curve, leaving the
// concave (interior) half of the triangle filled in the stencil buffer.
//
// Triangle ACO ("closing fan triangle") is the rest of the fan from the curve chord AC back
// to the shared origin O. It carries the neutral fill KLM (1, 1, _) so every pixel in it
// passes the test and contributes uniformly to the stencil counter.
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
  static constexpr float POINT_EPSILON_SQUARED = 1e-12f;

  static bool IsZeroLengthLine(const Point& a, const Point& b) {
    auto dx = a.x - b.x;
    auto dy = a.y - b.y;
    return dx * dx + dy * dy < POINT_EPSILON_SQUARED;
  }

  // Returns true if the three control points of a quadratic bezier are (near) collinear, in
  // which case rendering it as a straight line from A to C is exact.
  static bool IsQuadDegenerate(const Point pts[3]) {
    auto ab = Point::Make(pts[1].x - pts[0].x, pts[1].y - pts[0].y);
    auto ac = Point::Make(pts[2].x - pts[0].x, pts[2].y - pts[0].y);
    auto cross = ab.x * ac.y - ab.y * ac.x;
    return cross * cross < POINT_EPSILON_SQUARED;
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

StencilCoverPathTessellator::StencilCoverPathTessellator(std::shared_ptr<Shape> shape)
    : shape(std::move(shape)) {
}

bool StencilCoverPathTessellator::asyncSupport() const {
#if defined(TGFX_BUILD_FOR_WEB) && !defined(TGFX_USE_FREETYPE)
  return false;
#else
  return true;
#endif
}

std::shared_ptr<StencilCoverVertexData> StencilCoverPathTessellator::getData() const {
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

  // Reserve a verb-aware estimate: line/close emit 3 vertices each, quads at most 12 (a
  // max-curvature chop produces up to two sub-quads of 6 vertices), cubics expand into a
  // sequence of quads via ConvertCubicToQuads and can emit considerably more. 16 covers the
  // average mixed-path case in one allocation; cubic-heavy paths still only pay one growth
  // step, which is cheaper than allocating a worst-case (~48 verts/verb) buffer up front.
  std::vector<Vertex> vertices;
  vertices.reserve(static_cast<size_t>(path.countVerbs()) * 16);

  PathDecomposer decomposer(&vertices, origin);
  decomposer.decompose(path);
  if (vertices.empty()) {
    return nullptr;
  }

  auto data = Data::MakeWithCopy(vertices.data(), vertices.size() * sizeof(Vertex));
  if (data == nullptr) {
    return nullptr;
  }
  return std::make_shared<StencilCoverVertexData>(std::move(data), vertices.size());
}
}  // namespace tgfx

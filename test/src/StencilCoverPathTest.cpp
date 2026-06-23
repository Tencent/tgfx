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

// Consolidated tests for the stencil-and-cover render path. Cases are grouped by module
// using a `<Module>_` prefix on the test name:
//   - Caps_*                    : GPUFeatures::stencilCoverPathSupported (M2)
//   - Rasterizer_*              : StencilCoverPathTessellator CPU geometry builder (M3)
//   - Proxy_*                   : StencilCoverPathProxy + factory (M4)
//   - DrawOp_*                  : StencilCoverPathDrawOp construction (M6)
//   - Dispatch_*                : OpsCompositor dispatch + end-to-end (M7 + demo integration)

#include "core/StencilCoverPathTessellator.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ops/StencilCoverPathDrawOp.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/ImageInfo.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/GPUFeatures.h"
#include "utils/TestUtils.h"

namespace tgfx {

// RAII guard that overrides GPUFeatures::stencilCoverPathSupported for the lifetime of an
// instance. The previous value is captured on construction and restored on destruction, so a
// test that hits a fatal Google Test ASSERT mid-body still puts the flag back instead of
// leaking a polluted value into subsequent tests (which would otherwise break the deterministic
// caps-default expectations of Caps_BackendReportsExpectedSupport).
struct ScopedStencilCoverCaps {
  GPUFeatures* features;
  bool original;
  ScopedStencilCoverCaps(Context* context, bool value)
      : features(const_cast<GPUFeatures*>(context->gpu()->features())),
        original(features->stencilCoverPathSupported) {
    features->stencilCoverPathSupported = value;
  }
  ~ScopedStencilCoverCaps() {
    features->stencilCoverPathSupported = original;
  }
};

// ====================================================================================
// Caps (M2)
// ====================================================================================

TGFX_TEST(StencilCoverPathTest, Caps_FieldDefaultsToFalse) {
  GPUFeatures features = {};
  EXPECT_FALSE(features.stencilCoverPathSupported);
}

TGFX_TEST(StencilCoverPathTest, Caps_BackendReportsExpectedSupport) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto features = context->gpu()->features();
  ASSERT_TRUE(features != nullptr);
  // Verify the stencil-and-cover capability is reported per backend. Each backend is
  // listed explicitly so a regression on one backend cannot be hidden by an "any of the
  // three" disjunction. New backends added in the future hit the default branch and fail
  // until they declare an expectation here, since GPUFeatures::stencilCoverPathSupported
  // defaults to false.
  auto backend = context->gpu()->info()->backend;
  switch (backend) {
    case Backend::Metal:
      EXPECT_TRUE(features->stencilCoverPathSupported);
      break;
    case Backend::OpenGL:
      EXPECT_TRUE(features->stencilCoverPathSupported);
      break;
    case Backend::Vulkan:
      EXPECT_TRUE(features->stencilCoverPathSupported);
      break;
    default:
      ADD_FAILURE() << "Unhandled backend in Caps_BackendReportsExpectedSupport; declare "
                       "the expected stencilCoverPathSupported value for this backend.";
      break;
  }
}

// ====================================================================================
// StencilCoverPathTessellator (M3)
// ====================================================================================

TGFX_TEST(StencilCoverPathTest, Rasterizer_AsyncSupportMatchesPlatform) {
  Path path;
  path.addRect(Rect::MakeWH(100, 100));
  StencilCoverPathTessellator rasterizer(Shape::MakeFrom(path));
#if defined(TGFX_BUILD_FOR_WEB) && !defined(TGFX_USE_FREETYPE)
  EXPECT_FALSE(rasterizer.asyncSupport());
#else
  EXPECT_TRUE(rasterizer.asyncSupport());
#endif
}

TGFX_TEST(StencilCoverPathTest, Rasterizer_EmptyPathReturnsNull) {
  Path emptyPath;
  StencilCoverPathTessellator rasterizer(Shape::MakeFrom(emptyPath));
  EXPECT_TRUE(rasterizer.getData() == nullptr);
}

TGFX_TEST(StencilCoverPathTest, Rasterizer_SimpleRectReturnsBoundsTriangles) {
  Path path;
  path.addRect(Rect::MakeXYWH(10, 20, 100, 80));
  StencilCoverPathTessellator rasterizer(Shape::MakeFrom(path));
  // Structural invariants the rasterizer must satisfy regardless of how the path is
  // decomposed (lines, quads with collinear demotion, or cubics expanded to quads): the
  // buffer is non-empty, the vertex count is positive, and the byte size matches
  // vertexCount * 20 bytes (position Float2 + klm Float3 = 5 floats per vertex). The exact
  // vertex count depends on the algorithm and must not be asserted here.
  auto buffer = rasterizer.getData();
  ASSERT_TRUE(buffer != nullptr);
  EXPECT_GT(buffer->vertexCount, static_cast<size_t>(0));
  ASSERT_TRUE(buffer->vertices != nullptr);
  EXPECT_EQ(buffer->vertices->size(), buffer->vertexCount * 5 * sizeof(float));
}

TGFX_TEST(StencilCoverPathTest, Rasterizer_BezierPathReturnsBoundsTriangles) {
  Path path;
  path.moveTo(0, 0);
  path.quadTo(50, 100, 100, 0);
  path.cubicTo(120, 20, 180, 80, 200, 0);
  path.close();
  StencilCoverPathTessellator rasterizer(Shape::MakeFrom(path));
  // Same structural invariants as the rect case — don't predict the triangle count, just
  // verify the buffer is well-formed (non-empty, byte size matches the 5-float vertex stride).
  auto buffer = rasterizer.getData();
  ASSERT_TRUE(buffer != nullptr);
  EXPECT_GT(buffer->vertexCount, static_cast<size_t>(0));
  ASSERT_TRUE(buffer->vertices != nullptr);
  EXPECT_EQ(buffer->vertices->size(), buffer->vertexCount * 5 * sizeof(float));
}

// Degenerate inputs: PathDecomposer must not emit triangles for verbs that contribute no
// area, must not crash on bare moveTo, and must not over-produce triangles for paths whose
// bounding box is empty. These are guards against the rasterizer ever returning a buffer
// the upload task / draw op cannot consume safely.

// Zero-height rect: bounds.isEmpty() is true so the rasterizer must short-circuit and
// return null. Without this check the buffer would contain degenerate zero-area triangles
// that the stencil pass would happily write, with no visible effect but wasted GPU work.
TGFX_TEST(StencilCoverPathTest, Rasterizer_ZeroAreaRectReturnsNull) {
  Path path;
  path.addRect(Rect::MakeXYWH(10, 10, 100, 0));
  StencilCoverPathTessellator rasterizer(Shape::MakeFrom(path));
  EXPECT_TRUE(rasterizer.getData() == nullptr);
}

// Bare moveTo with no following verb: the contour is open and has no segments. The
// decomposer's processClose / processLine paths should never fire, so the output buffer
// must be empty (rasterizer returns null).
TGFX_TEST(StencilCoverPathTest, Rasterizer_BareMoveToReturnsNull) {
  Path path;
  path.moveTo(50, 50);
  StencilCoverPathTessellator rasterizer(Shape::MakeFrom(path));
  EXPECT_TRUE(rasterizer.getData() == nullptr);
}

// Single zero-length line: addLine's IsZeroLengthLine guard must drop the segment so the
// decomposer emits no triangles. A path with only this segment plus a close ends up with
// nothing renderable; rasterizer must return null rather than a 0-vertex buffer.
TGFX_TEST(StencilCoverPathTest, Rasterizer_DegenerateZeroLengthLineReturnsNull) {
  Path path;
  path.moveTo(50, 50);
  path.lineTo(50, 50);  // identical endpoint — IsZeroLengthLine drops it
  path.close();
  StencilCoverPathTessellator rasterizer(Shape::MakeFrom(path));
  EXPECT_TRUE(rasterizer.getData() == nullptr);
}

// Collinear quadratic: IsQuadDegenerate must demote it to a single line. The path triangle
// sits on a line and contributes no area; the demoted line itself has length so the buffer
// is non-empty and well-formed (the close-back-to-start adds the closing segment).
TGFX_TEST(StencilCoverPathTest, Rasterizer_CollinearQuadDemotesToLine) {
  Path path;
  // Three collinear control points along y = 50.
  path.moveTo(10, 50);
  path.quadTo(50, 50, 100, 50);
  path.lineTo(100, 60);  // pull the path off the degenerate line so close has area
  path.lineTo(10, 60);
  path.close();
  StencilCoverPathTessellator rasterizer(Shape::MakeFrom(path));
  auto buffer = rasterizer.getData();
  // Structural invariants only: ChopQuadAtMaxCurvature may split the collinear quad into
  // multiple sub-quads before IsQuadDegenerate demotes each to a line, so the exact vertex
  // count depends on the chopper's behaviour and must not be asserted here. What matters
  // is the buffer is well-formed and the vertex stride matches the 5-float layout — i.e.
  // every emitted primitive used the cheap line layout (3 vertices) rather than the
  // would-be quad layout (6 vertices, including a curve triangle whose interior the
  // fragment shader cannot evaluate on a self-collapsed quadratic).
  ASSERT_TRUE(buffer != nullptr);
  EXPECT_GT(buffer->vertexCount, static_cast<size_t>(0));
  EXPECT_EQ(buffer->vertices->size(), buffer->vertexCount * 5 * sizeof(float));
}

// ====================================================================================
// StencilCoverPathProxy + ProxyProvider factory (M4)
// ====================================================================================

TGFX_TEST(StencilCoverPathTest, Proxy_NullShapeReturnsNull) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto proxyProvider = context->proxyProvider();
  EXPECT_TRUE(proxyProvider->createStencilCoverPathProxy(nullptr, 0) == nullptr);
}

TGFX_TEST(StencilCoverPathTest, Proxy_FactoryReturnsProxy) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Path path;
  path.addRect(Rect::MakeXYWH(10, 20, 100, 80));
  auto shape = Shape::MakeFrom(path);

  auto proxy = context->proxyProvider()->createStencilCoverPathProxy(shape, 0);
  ASSERT_TRUE(proxy != nullptr);
  EXPECT_TRUE(proxy->getVertexBufferProxy() != nullptr);
}

TGFX_TEST(StencilCoverPathTest, Proxy_SameShapeReusesVertexBufferProxy) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Path path;
  path.addRoundRect(Rect::MakeXYWH(-12.5f, -12.5f, 25.f, 25.f), 5, 5);
  auto shape = Shape::MakeFrom(path);

  auto proxyProvider = context->proxyProvider();
  auto proxy1 = proxyProvider->createStencilCoverPathProxy(shape, 0);
  auto proxy2 = proxyProvider->createStencilCoverPathProxy(shape, 0);
  ASSERT_TRUE(proxy1 != nullptr);
  ASSERT_TRUE(proxy2 != nullptr);
  // Cache hit: the outer geometry proxy is recreated each call (cheap aggregator), but the
  // underlying GPUBufferProxy must be shared so multiple draws of the same shape reuse the
  // same uploaded vertex buffer.
  EXPECT_EQ(proxy1->getVertexBufferProxy().get(), proxy2->getVertexBufferProxy().get());
}

TGFX_TEST(StencilCoverPathTest, Proxy_DifferentShapesDoNotReuseVertexBufferProxy) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Path path1;
  path1.addRect(Rect::MakeXYWH(0, 0, 100, 100));
  auto shape1 = Shape::MakeFrom(path1);

  Path path2;
  path2.addRoundRect(Rect::MakeXYWH(0, 0, 100, 100), 8, 8);
  auto shape2 = Shape::MakeFrom(path2);

  auto proxyProvider = context->proxyProvider();
  auto proxy1 = proxyProvider->createStencilCoverPathProxy(shape1, 0);
  auto proxy2 = proxyProvider->createStencilCoverPathProxy(shape2, 0);
  ASSERT_TRUE(proxy1 != nullptr);
  ASSERT_TRUE(proxy2 != nullptr);
  EXPECT_NE(proxy1->getVertexBufferProxy().get(), proxy2->getVertexBufferProxy().get());
}

TGFX_TEST(StencilCoverPathTest, Proxy_DifferentTransformsHitSameCacheKey) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Path path;
  path.addRect(Rect::MakeXYWH(0, 0, 50, 50));
  auto baseShape = Shape::MakeFrom(path);
  // Apply different transforms: the bezier geometry cache key must ignore transform so that
  // future instanced batching can share a single GPU buffer across these two draws.
  auto shapeA = Shape::ApplyMatrix(baseShape, Matrix::MakeTrans(10, 20));
  auto shapeB = Shape::ApplyMatrix(baseShape, Matrix::MakeTrans(100, 200));

  auto proxyProvider = context->proxyProvider();
  auto proxyA = proxyProvider->createStencilCoverPathProxy(shapeA, 0);
  auto proxyB = proxyProvider->createStencilCoverPathProxy(shapeB, 0);
  ASSERT_TRUE(proxyA != nullptr);
  ASSERT_TRUE(proxyB != nullptr);
  EXPECT_EQ(proxyA->getVertexBufferProxy().get(), proxyB->getVertexBufferProxy().get());
}

// ====================================================================================
// StencilCoverPathDrawOp construction (M6)
// ====================================================================================

TGFX_TEST(StencilCoverPathTest, DrawOp_NullProxyReturnsNull) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto op = StencilCoverPathDrawOp::Make(nullptr, PMColor::Transparent(), Matrix::I(), Matrix::I(),
                                         Rect::MakeWH(100, 100), PathFillType::Winding);
  EXPECT_TRUE(op == nullptr);
}

TGFX_TEST(StencilCoverPathTest, DrawOp_MakeReturnsValidOp) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Path path;
  path.addRect(Rect::MakeXYWH(10, 20, 100, 80));
  auto shape = Shape::MakeFrom(path);
  auto geometryProxy = context->proxyProvider()->createStencilCoverPathProxy(shape, 0);
  ASSERT_TRUE(geometryProxy != nullptr);

  auto op = StencilCoverPathDrawOp::Make(geometryProxy, PMColor::Transparent(),
                                         Matrix::MakeTrans(10, 20), Matrix::I(),
                                         Rect::MakeXYWH(10, 20, 100, 80), PathFillType::EvenOdd);
  ASSERT_TRUE(op != nullptr);
  EXPECT_TRUE(op->hasCoverage());
  EXPECT_TRUE(op->needsStencil());
}

// Verifies StencilCoverPathDrawOp::Make accepts every PathFillType value and the
// per-fill-type cover-pass stencil reference is computed correctly. The reference value
// drives the GPU stencil compare: non-inverse non-zero fills compare-not-equal against 0,
// non-inverse even-odd fills compare-equal against 0xFF (interior leaves stencil at all
// ones via Invert), and inverse fills always compare against 0 to shade the path's
// exterior. Reads the private coverStencilRef via -fno-access-control. If a future change
// to CoverPassStencilReference forgets a branch, this test catches it before the visual
// dispatch tests do.
TGFX_TEST(StencilCoverPathTest, DrawOp_AllFillTypesProduceValidOpWithExpectedStencilRef) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Path path;
  path.addRect(Rect::MakeXYWH(10, 20, 100, 80));
  auto shape = Shape::MakeFrom(path);
  auto geometryProxy = context->proxyProvider()->createStencilCoverPathProxy(shape, 0);
  ASSERT_TRUE(geometryProxy != nullptr);

  struct Case {
    PathFillType fillType;
    uint32_t expectedRef;
    const char* name;
  };
  const Case cases[] = {
      {PathFillType::Winding, 0, "Winding"},
      {PathFillType::EvenOdd, 0xFF, "EvenOdd"},
      {PathFillType::InverseWinding, 0, "InverseWinding"},
      {PathFillType::InverseEvenOdd, 0, "InverseEvenOdd"},
  };
  for (const auto& c : cases) {
    auto op = StencilCoverPathDrawOp::Make(geometryProxy, PMColor::Transparent(),
                                           Matrix::MakeTrans(10, 20), Matrix::I(),
                                           Rect::MakeXYWH(10, 20, 100, 80), c.fillType);
    ASSERT_TRUE(op != nullptr) << c.name;
    EXPECT_TRUE(op->hasCoverage()) << c.name;
    EXPECT_TRUE(op->needsStencil()) << c.name;
    EXPECT_EQ(op->coverStencilRef, c.expectedRef) << c.name;
  }
}

// ====================================================================================
// OpsCompositor dispatch + end-to-end (M7 + demo integration)
// ====================================================================================

// The stencil-and-cover dispatch is gated by GPUFeatures::stencilCoverPathSupported. The
// dispatch tests below confirm two things:
//   1. With the flag forcibly disabled, drawing a non-AA path keeps using the legacy
//      ShapeDrawOp pipeline. This is the "AA path zero regression" guarantee.
//   2. With the flag forcibly enabled, drawing a non-AA path runs the full bezier
//      rasterization dispatch (stencil + cover passes) end-to-end and produces a precise
//      fill: the cover pass shades pixels the stencil pass kept and leaves the rest as the
//      surface's pre-existing colour. Both inside-shape and outside-shape pixels are
//      asserted so neither "stencil dropped everything" nor "cover overpainted everything"
//      can pass.

namespace {

// Reads back a single pixel and returns its red channel byte. RGBA_8888 + Premultiplied is
// stable across backends, and only the red byte is needed since every test below uses pure
// red as the brush colour.
uint8_t ReadRedChannel(Surface* surface, int x, int y) {
  uint32_t pixel = 0;
  auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
  if (!surface->readPixels(info, &pixel, x, y)) {
    return 0;
  }
  return static_cast<uint8_t>(pixel & 0xFF);
}

// Builds a centred convex pentagon inscribed in a 48x48 box at (8, 8). Used by all four
// fill-type dispatch tests instead of Path::addRect because Canvas::drawPath detects
// single-rectangle paths and routes them through Canvas::drawRect, which discards the fill
// type entirely (a known tgfx-side limitation, see the analogous note above
// BuildInverseSquarePath in the visual-debug section). Any non-rect / non-oval / non-rrect
// shape bypasses that fast path and reaches the rasterizer with its fill type intact.
//
// The pentagon is symmetric around (32, 32) inside a 64x64 surface so the centre (32, 32)
// is unambiguously inside, and corners like (4, 4) are unambiguously outside.
Path BuildCentredPentagonForDispatch() {
  Path path;
  path.moveTo(32, 9);
  path.lineTo(53, 24);
  path.lineTo(45, 49);
  path.lineTo(19, 49);
  path.lineTo(11, 24);
  path.close();
  return path;
}

// Common scaffolding for fill-type dispatch tests: makes a 64x64 surface cleared to black,
// forces the bezier path on, draws a centred pentagon with `fillType` and checks two
// pixels — one safely inside the pentagon, one safely outside. Pinning down both signals
// proves the cover pass shaded the surviving stencil pixels *and* the stencil test culled
// the rest, rather than the cover pass overpainting indiscriminately.
void RunFillTypeDispatch(PathFillType fillType, uint8_t expectedInsideRed,
                         uint8_t expectedOutsideRed) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  ScopedStencilCoverCaps capsGuard(context, true);

  constexpr int kSize = 64;
  auto surface = Surface::Make(context, kSize, kSize);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  // Clear to opaque black so we can tell a stencil+cover write apart from "nothing happened"
  // and apart from "cover pass overwrote pixels it should have left alone".
  canvas->clear(Color{0.f, 0.f, 0.f, 1.f});

  Paint paint;
  paint.setAntiAlias(false);
  paint.setColor(Color{1.f, 0.f, 0.f, 1.f});

  Path path = BuildCentredPentagonForDispatch();
  path.setFillType(fillType);
  canvas->drawPath(path, paint);
  context->flushAndSubmit();

  EXPECT_EQ(ReadRedChannel(surface.get(), kSize / 2, kSize / 2), expectedInsideRed)
      << "fill type " << static_cast<int>(fillType) << ": inside-shape pixel mismatch";
  EXPECT_EQ(ReadRedChannel(surface.get(), 4, 4), expectedOutsideRed)
      << "fill type " << static_cast<int>(fillType) << ": outside-shape pixel mismatch";
}

}  // namespace

// Renders a centred pentagon (a shape that bypasses Canvas's rect/oval/rrect fast paths
// and therefore actually reaches the dispatch fork in OpsCompositor::drawShape) under the
// given caps value, then returns the centre and corner red-channel pair so the caller can
// assert pixel correctness without re-knowing the surface layout.
namespace {
struct DispatchSnapshot {
  uint8_t insideRed;
  uint8_t outsideRed;
};

DispatchSnapshot RunPentagonUnderCaps(Context* context, bool capsValue) {
  ScopedStencilCoverCaps capsGuard(context, capsValue);

  constexpr int kSize = 64;
  auto surface = Surface::Make(context, kSize, kSize);
  if (surface == nullptr) {
    return {0, 0};
  }
  auto canvas = surface->getCanvas();
  canvas->clear(Color{0.f, 0.f, 0.f, 1.f});

  Paint paint;
  paint.setAntiAlias(false);
  paint.setColor(Color{1.f, 0.f, 0.f, 1.f});

  Path path = BuildCentredPentagonForDispatch();
  path.setFillType(PathFillType::Winding);
  canvas->drawPath(path, paint);
  context->flushAndSubmit();

  return {ReadRedChannel(surface.get(), kSize / 2, kSize / 2), ReadRedChannel(surface.get(), 4, 4)};
}
}  // namespace

// With the cap forcibly off, drawing a pentagon goes through the legacy ShapeDrawOp
// pipeline. The interior must still receive the brush colour and the exterior must stay
// background black — this catches "the legacy path was inadvertently broken by changes
// meant for the bezier path" regressions.
TGFX_TEST(StencilCoverPathTest, Dispatch_DisabledCapsRoutesThroughLegacyPath) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto snapshot = RunPentagonUnderCaps(context, /*capsValue=*/false);
  EXPECT_EQ(snapshot.insideRed, 0xFF) << "Legacy path interior should be red";
  EXPECT_EQ(snapshot.outsideRed, 0x00) << "Legacy path exterior should stay black";
}

// With the cap forcibly on, the dispatch reaches drawStencilCoverPath. Same pixel
// expectations as the legacy variant — proving the bezier path runs to completion *and*
// produces a precise fill (stencil + cover both correct).
TGFX_TEST(StencilCoverPathTest, Dispatch_EnabledCapsRoutesThroughNewBranch) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto snapshot = RunPentagonUnderCaps(context, /*capsValue=*/true);
  EXPECT_EQ(snapshot.insideRed, 0xFF) << "Bezier path interior should be red";
  EXPECT_EQ(snapshot.outsideRed, 0x00) << "Bezier path exterior should stay black";
}

// Caps invariance: rendering the same path through the legacy and bezier pipelines must
// produce pixel-identical output at the sample points. This is the strongest available
// signal that path-selection is observable from the test and that the two pipelines stay
// in agreement — if a future change to either path drifts (e.g. legacy starts AA-bleeding
// or bezier loses a vertex), the equality assertion catches it. Pixel-level full-surface
// equality is not asserted because edge anti-aliasing differs by 1 px between the two
// pipelines on curved paths; sampling well-inside and well-outside the path keeps the
// comparison robust.
TGFX_TEST(StencilCoverPathTest, Dispatch_LegacyAndBezierProduceMatchingSampledPixels) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto legacy = RunPentagonUnderCaps(context, /*capsValue=*/false);
  auto bezier = RunPentagonUnderCaps(context, /*capsValue=*/true);

  EXPECT_EQ(legacy.insideRed, bezier.insideRed)
      << "Inside-shape pixel diverges between legacy and bezier paths";
  EXPECT_EQ(legacy.outsideRed, bezier.outsideRed)
      << "Outside-shape pixel diverges between legacy and bezier paths";
}

// Non-inverse fills paint the path's interior. Inside the rect must be brush red, outside
// must keep the background black. This is the strict counterpart of the original "centre
// pixel matches" check — it also pins down that the stencil test culls pixels outside the
// path rather than letting the cover pass overpaint them.
TGFX_TEST(StencilCoverPathTest, Dispatch_EnabledCapsDrawsStencilCoveredColor_EvenOdd) {
  RunFillTypeDispatch(PathFillType::EvenOdd, /*insideRed=*/0xFF, /*outsideRed=*/0x00);
}

TGFX_TEST(StencilCoverPathTest, Dispatch_EnabledCapsDrawsStencilCoveredColor_NonZero) {
  RunFillTypeDispatch(PathFillType::Winding, /*insideRed=*/0xFF, /*outsideRed=*/0x00);
}

// Inverse fills paint the path's exterior. The cover pass must read against zero stencil
// (CoverPassStencilReference returns 0 for inverse fills) and shade the surface everywhere
// the path is *not*. Inside the rect must stay background black, outside must be brush red.
// Without inverse-fill support these tests would either leave the surface fully black (no
// cover pass at all) or fully red (cover pass without precise stencil culling).
TGFX_TEST(StencilCoverPathTest, Dispatch_EnabledCapsDrawsStencilCoveredColor_InverseWinding) {
  RunFillTypeDispatch(PathFillType::InverseWinding, /*insideRed=*/0x00, /*outsideRed=*/0xFF);
}

TGFX_TEST(StencilCoverPathTest, Dispatch_EnabledCapsDrawsStencilCoveredColor_InverseEvenOdd) {
  RunFillTypeDispatch(PathFillType::InverseEvenOdd, /*insideRed=*/0x00, /*outsideRed=*/0xFF);
}

// Regression for stencil-write scissor isolation across consecutive bezier ops sharing a
// render pass. StencilCoverPathDrawOp::bindStencilPipeline applies the op's scissor
// before the stencil pass, so the stencil writes are confined to the cover-pass region.
// Without that scissor the first op's stencil pass would write outside its own clip and
// the second op — whose cover pass clears stencil only inside its own clip — would read
// non-zero stencil values left over from the first op, producing red bleed inside the
// second pentagon under non-zero fill (Equal-to-zero compare flips into NotEqual hits).
//
// Layout (64x64 surface):
//   - Pentagon A is centred in the LEFT half, drawn under clipRect(0,0,32,64).
//   - Pentagon B is centred in the RIGHT half, drawn under clipRect(32,0,64,64) with
//     InverseWinding so its cover pass shades pixels where stencil == 0.
//
// If the bug is present, residue from A's stencil (written outside A's scissor into B's
// half of the depth/stencil attachment) would make pixels inside B's pentagon fail the
// Equal-zero compare, leaving them background black instead of brush red — failing the
// "inside B is red" check.
TGFX_TEST(StencilCoverPathTest, Dispatch_StencilWritesAreScissoredToOpClip) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  ScopedStencilCoverCaps capsGuard(context, true);

  constexpr int kSize = 64;
  auto surface = Surface::Make(context, kSize, kSize);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color{0.f, 0.f, 0.f, 1.f});

  Paint paint;
  paint.setAntiAlias(false);
  paint.setColor(Color{1.f, 0.f, 0.f, 1.f});

  // Op A: pentagon (non-inverse Winding) clipped to the left half. The stencil pass writes
  // counter increments inside the pentagon; the cover pass shades non-zero pixels red and
  // resets the stencil to zero — but only inside A's clip.
  Path pathA;
  pathA.moveTo(16, 9);
  pathA.lineTo(28, 24);
  pathA.lineTo(24, 41);
  pathA.lineTo(8, 41);
  pathA.lineTo(4, 24);
  pathA.close();
  pathA.setFillType(PathFillType::Winding);
  canvas->save();
  canvas->clipRect(Rect::MakeXYWH(0, 0, 32, kSize));
  canvas->drawPath(pathA, paint);
  canvas->restore();

  // Op B: pentagon (InverseWinding) clipped to the right half. Inverse fill compares
  // stencil == 0; if op A leaked stencil writes into the right half, those pixels would
  // fail the Equal-zero compare and stay black even though they are outside pentagon B.
  Path pathB;
  pathB.moveTo(48, 9);
  pathB.lineTo(60, 24);
  pathB.lineTo(56, 41);
  pathB.lineTo(40, 41);
  pathB.lineTo(36, 24);
  pathB.close();
  pathB.setFillType(PathFillType::InverseWinding);
  canvas->save();
  canvas->clipRect(Rect::MakeXYWH(32, 0, 32, kSize));
  canvas->drawPath(pathB, paint);
  canvas->restore();

  context->flushAndSubmit();

  // Inside pentagon A (left half centre): non-inverse fill, must be red.
  EXPECT_EQ(ReadRedChannel(surface.get(), 16, 28), 0xFF) << "Pentagon A interior should be red";
  // Outside pentagon A but inside A's clip (left half corner): stencil-culled, must be black.
  EXPECT_EQ(ReadRedChannel(surface.get(), 1, 1), 0x00)
      << "Outside A inside A's clip should be black";
  // Inside pentagon B (right half centre): inverse fill — interior is the path itself,
  // which inverse leaves *unfilled*, so it must be black.
  EXPECT_EQ(ReadRedChannel(surface.get(), 48, 28), 0x00) << "Pentagon B interior should be black";
  // Outside pentagon B but inside B's clip (right half top corner well clear of B's bbox):
  // inverse fill — exterior is shaded red. Critically this is the pixel that detects stencil
  // bleed from op A: if A's stencil writes leaked into the right half, this pixel would
  // fail the Equal-zero compare and stay black.
  EXPECT_EQ(ReadRedChannel(surface.get(), 62, 1), 0xFF)
      << "Outside B inside B's clip should be red (catches stencil bleed from earlier op)";
}

// ====================================================================================
// Visual debug cases
// ====================================================================================
//
// These cases render a single path through the stencil-and-cover pipeline and hand the
// result off to Baseline::Compare for screenshot regression protection.
//
// Two operating modes, controlled by `TGFX_BEZIER_VISUAL_DEBUG_EXPORT` below:
//
//   * Default (macro = 0): only the bezier pass is rendered, and the result is compared
//     against the canonical baseline `StencilCoverPathDebug/<Name>` (no suffix). This is the
//     CI-friendly mode — accept new baselines via `/accept-baseline` once outputs stabilise.
//
//   * Debug export (macro = 1): both pipelines are rendered into the same surface size and
//     written out as `<Name>_Legacy.webp` / `<Name>_Bezier.webp` for side-by-side eyeballing.
//     The bezier pass still runs Baseline::Compare so the regression check stays active; the
//     legacy pass is purely a visual reference and its return value is dropped. Flip the
//     macro locally while tuning the triangulator, then flip it back before committing.
//
// Conventions (see .codebuddy/rules/Test.md):
//   - Content is centred with roughly 50px margin on every side.
//   - Use integer coordinates where possible to keep edges crisp.
//   - Force caps on via ScopedStencilCoverCaps so we always exercise the bezier path, regardless of
//     the backend's current default. The guard restores the previous value on scope exit so
//     other tests stay deterministic even if a fatal ASSERT fires mid-test.

// Set to 1 locally to render both legacy and bezier pipelines and dump each as a separate
// webp under `test/out/StencilCoverPathDebug/`. Must remain 0 in committed code so CI runs
// the canonical single-pass baseline comparison.
#define TGFX_BEZIER_VISUAL_DEBUG_EXPORT 0

// Shared palette for the visual debug renders. Warm off-white background plus a deep teal
// foreground keeps the contrast readable while side-by-side comparing legacy vs bezier
// outputs without the eye-strain of pure red on pure black. Cases that test brush
// behaviour (gradient / colour filter / blend mode) override these locally because their
// pixel outcome depends on a specific colour choice.
static const Color kDebugBackground{0.961f, 0.941f, 0.910f, 1.f};  // #F5F0E8
static const Color kDebugForeground{0.173f, 0.373f, 0.365f, 1.f};  // #2C5F5D

// Optional transform hook applied around `canvas->drawPath` via canvas->save/restore. Cases
// that don't need a non-identity canvas matrix pass nullptr.
typedef void (*ApplyTransformFn)(Canvas* canvas);

// Renders a single pipeline pass (`useBezier` decides which) into a fresh 300x300 surface.
// `applyTransform` may be nullptr; when set, it is invoked between save/restore so the
// canvas matrix change is local to this draw. Caller is responsible for toggling
// GPUFeatures::stencilCoverPathSupported beforehand. Extracted from the double-pass helper
// so the two pipeline runs share one rendering implementation without a lambda.
static std::shared_ptr<Surface> RenderSingleVisualPass(Context* context, const Path& path,
                                                       const Paint& paint, const Color& clearColor,
                                                       ApplyTransformFn applyTransform) {
  auto surface = Surface::Make(context, 300, 300);
  if (surface == nullptr) {
    return nullptr;
  }
  auto canvas = surface->getCanvas();
  canvas->clear(clearColor);
  if (applyTransform != nullptr) {
    canvas->save();
    applyTransform(canvas);
  }
  canvas->drawPath(path, paint);
  if (applyTransform != nullptr) {
    canvas->restore();
  }
  context->flushAndSubmit();
  return surface;
}

// Renders `path` with `paint` (and an optional canvas transform / custom clear colour) into
// a 300x300 surface and runs Baseline::Compare. Behaviour switches on
// TGFX_BEZIER_VISUAL_DEBUG_EXPORT (see comment above the macro definition).
//
// Scale note: paths must be large enough that PathTriangulator::ShouldTriangulatePath() picks
// the real triangulation path instead of the mask-rasterization fallback — that function
// returns false when max(width, height) <= 162 (see PathTriangulator.cpp). Each path builder
// in this file is sized for a 300x300 surface with a comfortable ~50px margin and a bbox
// well above the 162 threshold.
static void RenderPathAndCompareBaselines(Context* context, const Path& path, const Paint& paint,
                                          const Color& clearColor, ApplyTransformFn applyTransform,
                                          const std::string& keyPrefix) {
  // The guard restores the original caps value on scope exit, including via fatal Google
  // Test ASSERT failures triggered inside the render passes.
  ScopedStencilCoverCaps capsGuard(context, false);

#if TGFX_BEZIER_VISUAL_DEBUG_EXPORT
  // Debug-export mode: render both pipelines and dump each one. The legacy pass is a pure
  // visual reference (return value dropped on purpose — it has no canonical baseline and is
  // expected to drift relative to the bezier output); only the bezier pass is enforced.
  capsGuard.features->stencilCoverPathSupported = false;
  auto legacySurface = RenderSingleVisualPass(context, path, paint, clearColor, applyTransform);
  ASSERT_TRUE(legacySurface != nullptr);
  (void)Baseline::Compare(legacySurface, keyPrefix + "_Legacy");

  capsGuard.features->stencilCoverPathSupported = true;
  auto bezierSurface = RenderSingleVisualPass(context, path, paint, clearColor, applyTransform);
  ASSERT_TRUE(bezierSurface != nullptr);
  EXPECT_TRUE(Baseline::Compare(bezierSurface, keyPrefix + "_Bezier"));
#else
  // Default mode: bezier-only, compared against the canonical baseline.
  capsGuard.features->stencilCoverPathSupported = true;
  auto surface = RenderSingleVisualPass(context, path, paint, clearColor, applyTransform);
  ASSERT_TRUE(surface != nullptr);
  EXPECT_TRUE(Baseline::Compare(surface, keyPrefix));
#endif
}

// Convenience wrapper: default debug-background clear, no canvas transform.
static void RenderPathAndCompareBaselines(Context* context, const Path& path, const Paint& paint,
                                          const std::string& keyPrefix) {
  RenderPathAndCompareBaselines(context, path, paint, kDebugBackground, nullptr, keyPrefix);
}

// Convenience wrapper: solid debug-foreground teal paint, default debug-background clear,
// no canvas transform — matches the most common visual-debug case shape.
static void RenderPathAndCompareBaselines(Context* context, const Path& path,
                                          const std::string& keyPrefix) {
  Paint paint;
  paint.setAntiAlias(false);
  paint.setColor(kDebugForeground);
  RenderPathAndCompareBaselines(context, path, paint, kDebugBackground, nullptr, keyPrefix);
}

// ----- Path builders (one per debug case) -----

// Classic two-cubic heart, symmetric around x=150. Bbox is [30, 270] x [60, 230].
static Path BuildHeartCubicPath() {
  Path path;
  path.moveTo(150, 230);
  path.cubicTo(270, 170, 270, 60, 150, 110);
  path.cubicTo(30, 60, 30, 170, 150, 230);
  path.close();
  return path;
}

// Concave hexagon with a notched corner, exercising fan-from-bbox-centre triangulation on a
// non-convex line-only contour. Bbox is [40, 260] x [40, 260].
static Path BuildConcavePolygonPath() {
  Path path;
  path.moveTo(40, 40);
  path.lineTo(260, 40);
  path.lineTo(260, 150);
  path.lineTo(150, 150);  // inward notch
  path.lineTo(150, 260);
  path.lineTo(40, 260);
  path.close();
  return path;
}

// Half-disc bounded above by a single quadratic arc. Tests the simplest possible Loop-Blinn
// quad evaluation. Bbox is [40, 260] x [70, 260].
static Path BuildSemicircleQuadPath() {
  Path path;
  path.moveTo(40, 260);
  // Control point pulled high above the bbox so the quadratic visibly bulges upwards. The
  // Loop-Blinn UV mapping makes the curve interior survive the fragment test.
  path.quadTo(150, 70, 260, 260);
  path.lineTo(40, 260);
  path.close();
  return path;
}

// Solid disc via Path::addOval, which internally emits four conic verbs. Exercises the
// NoConicsPathIterator → quad expansion path. Bbox is [40, 260] x [40, 260].
static Path BuildCirclePath() {
  Path path;
  path.addOval(Rect::MakeXYWH(40, 40, 220, 220));
  return path;
}

// Five-pointed star drawn as a single self-intersecting polygon. With EvenOdd the inner
// pentagon ends up unfilled (covered an even number of times); this is the canonical fill
// rule case and immediately surfaces winding bugs in the rasterizer. Bbox is [30, 270] x
// [55, 260] approximately, well above the 162-px threshold.
static Path BuildStarEvenOddPath() {
  Path path;
  path.setFillType(PathFillType::EvenOdd);
  // Visit alternating outer points of a pentagon to produce the classic five-pointed star.
  // Coordinates pre-computed so they are plain integers and the figure is symmetric around
  // x=150.
  path.moveTo(150, 55);
  path.lineTo(214, 252);
  path.lineTo(46, 130);
  path.lineTo(254, 130);
  path.lineTo(86, 252);
  path.close();
  return path;
}

// Donut (annulus): an outer disc with a concentric inner disc cut out. Drawn as two
// addOval contours; under EvenOdd the overlapping region of the inner disc is covered
// twice and therefore unfilled. Stresses the multi-contour path of the rasterizer —
// decomposer state (start/current point, close) has to reset cleanly between contours and
// the cover-pass stencil test has to interpret an "even" stencil value as outside. Outer
// bbox is [40, 260] x [40, 260].
static Path BuildDonutPath() {
  Path path;
  path.setFillType(PathFillType::EvenOdd);
  path.addOval(Rect::MakeXYWH(40, 40, 220, 220));    // outer disc
  path.addOval(Rect::MakeXYWH(100, 100, 100, 100));  // inner hole
  return path;
}

// C-shape (open-to-the-right squared annulus), drawn with line segments only. The fan
// origin used by the bezier triangulator is the bbox centre (150, 150), which falls
// *inside the C's open region* — i.e. outside the filled area. This probes the assumption
// that fan-from-centre triangulation plus the stencil toggle still converges on the
// correct silhouette even when the origin is not contained by the path. Bbox is
// [50, 250] x [50, 250].
static Path BuildCShapePath() {
  Path path;
  // Outer rectangle (clockwise), then reverse into the opening to carve out the interior
  // notch in a single contour.
  path.moveTo(250, 50);
  path.lineTo(50, 50);
  path.lineTo(50, 250);
  path.lineTo(250, 250);
  path.lineTo(250, 210);
  path.lineTo(90, 210);
  path.lineTo(90, 90);
  path.lineTo(250, 90);
  path.close();
  return path;
}

// Plain centred square used by the brush-combination cases below. Path geometry isn't the
// focus there; using a rectangle (line-only, no curves) keeps the comparison clean of the
// 1-pixel curve-edge differences seen in SemicircleQuad / Donut.
static Path BuildSquarePath() {
  Path path;
  path.addRect(Rect::MakeXYWH(50, 50, 200, 200));
  return path;
}

// A pentagon configured with an inverse fill type. Inverse fills paint the *outside* of the
// path, so the rendered surface should be black inside the pentagon and red everywhere else.
//
// IMPORTANT: do not build this with `path.addRect` followed by `setFillType(InverseWinding)`.
// `Canvas::drawPath` detects single-rectangle paths and routes them through the fast
// `drawRect` codepath, which discards the fill type altogether (a known tgfx-side gap
// unrelated to stencil-and-cover). Pentagons (and any non-rect / non-oval / non-rrect
// shape) bypass that fast path and reach the rasterizer with their fill type intact.
static Path BuildInverseSquarePath() {
  Path path;
  // Five vertices of a regular pentagon symmetric around (150, 150) with bbox roughly
  // [50, 250] x [55, 245] — large enough to clear PathTriangulator's MIN_TRIANGULATE_SIZE
  // threshold so the legacy comparison goes through real triangulation.
  path.moveTo(150, 55);
  path.lineTo(245, 124);
  path.lineTo(208, 235);
  path.lineTo(92, 235);
  path.lineTo(55, 124);
  path.close();
  path.setFillType(PathFillType::InverseWinding);
  return path;
}

// Empty path with InverseWinding — this is the fast-path Canvas::drawFill() takes whenever
// drawColor()/drawPaint() runs under an active clip. The expected rendering is a fully
// painted surface (the path's "outside" is everything). Until inverse-fill support landed
// in the rasterizer this case produced an empty image because the stencil pass was skipped
// without flipping the cover-pass compare to read against zero.
static Path BuildEmptyInversePath() {
  Path path;
  path.setFillType(PathFillType::InverseWinding);
  return path;
}

// ----- Test cases -----

TGFX_TEST(StencilCoverPathTest, Visual_HeartCubic) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildHeartCubicPath(), "StencilCoverPathDebug/HeartCubic");
}

TGFX_TEST(StencilCoverPathTest, Visual_ConcavePolygon) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildConcavePolygonPath(),
                                "StencilCoverPathDebug/ConcavePolygon");
}

TGFX_TEST(StencilCoverPathTest, Visual_SemicircleQuad) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildSemicircleQuadPath(),
                                "StencilCoverPathDebug/SemicircleQuad");
}

TGFX_TEST(StencilCoverPathTest, Visual_Circle) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildCirclePath(), "StencilCoverPathDebug/Circle");
}

TGFX_TEST(StencilCoverPathTest, Visual_StarEvenOdd) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildStarEvenOddPath(),
                                "StencilCoverPathDebug/StarEvenOdd");
}

TGFX_TEST(StencilCoverPathTest, Visual_Donut) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildDonutPath(), "StencilCoverPathDebug/Donut");
}

TGFX_TEST(StencilCoverPathTest, Visual_CShape) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildCShapePath(), "StencilCoverPathDebug/CShape");
}

// Inverse-fill rendering: the rasterizer must paint the path's *outside*. With the
// non-empty inverse-pentagon the surface should show a black hole (the pentagon shape)
// surrounded by red; with the empty-inverse path the entire surface should be red. The
// legacy path reaches the same outcome via Canvas::drawFill / inverse-fill mask routes.
TGFX_TEST(StencilCoverPathTest, Visual_InversePentagon) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildInverseSquarePath(),
                                "StencilCoverPathDebug/InversePentagon");
}

TGFX_TEST(StencilCoverPathTest, Visual_EmptyInverse) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildEmptyInversePath(),
                                "StencilCoverPathDebug/EmptyInverse");
}

// ----- Brush combination cases (square path keeps geometry differences out of the picture) -----

// Linear gradient from red on the left to blue on the right. Validates that the cover GP's
// emitTransforms hands the correct local coordinates to the shader FP — if uvMatrix were
// wrong the gradient would be skewed or stretched.
TGFX_TEST(StencilCoverPathTest, Visual_BrushLinearGradient) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Paint paint;
  paint.setAntiAlias(false);
  // Anchor the gradient at the path's bbox so the visual gradient direction is unambiguous.
  paint.setShader(Shader::MakeLinearGradient(
      {50, 150}, {250, 150}, {Color{1.f, 0.f, 0.f, 1.f}, Color{0.f, 0.f, 1.f, 1.f}}, {0.f, 1.f}));
  RenderPathAndCompareBaselines(context, BuildSquarePath(), paint,
                                "StencilCoverPathDebug/BrushLinearGradient");
}

// Colour matrix that inverts RGB (R' = 1 - R, etc.). Applied on top of a solid red brush
// the output should be cyan. Validates that the colour-filter FP joins the FP chain of the
// cover pass.
TGFX_TEST(StencilCoverPathTest, Visual_BrushColorFilterInvert) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  // Standard channel-invert matrix in row-major form.
  std::array<float, 20> invertMatrix = {{
      -1.f, 0.f, 0.f,  0.f, 1.f, 0.f, -1.f, 0.f, 0.f, 1.f,
      0.f,  0.f, -1.f, 0.f, 1.f, 0.f, 0.f,  0.f, 1.f, 0.f,
  }};

  Paint paint;
  paint.setAntiAlias(false);
  paint.setColor(Color{1.f, 0.f, 0.f, 1.f});
  paint.setColorFilter(ColorFilter::Matrix(invertMatrix));
  RenderPathAndCompareBaselines(context, BuildSquarePath(), paint,
                                "StencilCoverPathDebug/BrushColorFilterInvert");
}

// Plus / additive blend on top of a non-debug background. The square is drawn with green
// over a red clear, so the resulting overlap should be yellow if the blend mode reaches the
// cover pipeline correctly. Threads a custom clear colour through the shared helper.
TGFX_TEST(StencilCoverPathTest, Visual_BrushBlendModePlus) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Paint paint;
  paint.setAntiAlias(false);
  paint.setColor(Color{0.f, 1.f, 0.f, 1.f});
  paint.setBlendMode(BlendMode::PlusLighter);
  RenderPathAndCompareBaselines(context, BuildSquarePath(), paint, Color{1.f, 0.f, 0.f, 1.f},
                                nullptr, "StencilCoverPathDebug/BrushBlendModePlus");
}

// Rotate 30 degrees and scale 0.7x around the surface centre. Without the centre pivot the
// rotated heart would slide off-canvas. The 0.7x factor keeps the heart inside the 300x300
// surface even after rotation enlarges its bounding box.
static void ApplyBrushScaleRotate(Canvas* canvas) {
  canvas->translate(150, 150);
  canvas->rotate(30);
  canvas->scale(0.7f, 0.7f);
  canvas->translate(-150, -150);
}

// Apply a non-trivial canvas transform (scale + rotate) before drawing. Validates that the
// view matrix accumulates correctly through both the stencil pass and the cover pass, and
// that the implicit-curve fragment test still classifies pixels correctly under rotation
// (KLM is computed in path-local space, so the rotation should not affect classification).
TGFX_TEST(StencilCoverPathTest, Visual_BrushScaleRotate) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Paint paint;
  paint.setAntiAlias(false);
  paint.setColor(kDebugForeground);
  RenderPathAndCompareBaselines(context, BuildHeartCubicPath(), paint, kDebugBackground,
                                ApplyBrushScaleRotate, "StencilCoverPathDebug/BrushScaleRotate");
}

// ----- Canvas transform correctness cases -----
//
// The bezier path stores vertex buffers in path-local space and applies the canvas matrix
// via a GPU uniform on the stencil + cover GPs. Legacy ShapeDrawOp instead bakes the matrix
// into the path before triangulation. The two pipelines must produce visually identical
// output across the full range of affine transforms. Each case routes through the shared
// RenderPathAndCompareBaselines helper with a transform function pointer, so the
// double-pass / debug-export logic stays centralised.

// Source path for the scale cases: a small disc centred on (75, 75) with diameter 80, well
// below the 162-px MIN_TRIANGULATE_SIZE threshold but the canvas matrix scales it up so the
// rendered geometry clears that threshold. Using a smaller source keeps it inside the
// surface after a 2x or 4x scale.
static Path BuildSmallCirclePath() {
  Path path;
  path.addOval(Rect::MakeXYWH(35, 35, 80, 80));
  return path;
}

// Solid debug-foreground paint shared by every transform case below. Defined locally so the
// test bodies stay short and clearly mirror the path/transform pair under test.
static Paint MakeTransformDebugPaint() {
  Paint paint;
  paint.setAntiAlias(false);
  paint.setColor(kDebugForeground);
  return paint;
}

// Pure uniform scale, 2x around the surface centre. Verifies the simplest non-identity
// matrix path: scaleX = scaleY, no rotation, no skew. The disc must remain a disc and
// preserve its aspect ratio in both pipelines.
static void ApplyPureScale2x(Canvas* canvas) {
  canvas->translate(150, 150);
  canvas->scale(2.0f, 2.0f);
  canvas->translate(-75, -75);
}

TGFX_TEST(StencilCoverPathTest, Visual_TransformPureScale2x) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildSmallCirclePath(), MakeTransformDebugPaint(),
                                kDebugBackground, ApplyPureScale2x,
                                "StencilCoverPathDebug/TransformPureScale2x");
}

// Non-uniform scale: 2x horizontally, 1x vertically. The disc must turn into a horizontal
// ellipse with a 2:1 aspect ratio. This stresses the implicit-curve fragment test more than
// uniform scale because the KLM coordinates that bezier emits are in path-local space and
// the GPU only learns about the non-uniform stretch through the view matrix uniform.
static void ApplyNonUniformScale(Canvas* canvas) {
  canvas->translate(150, 150);
  canvas->scale(2.0f, 1.0f);
  canvas->translate(-75, -75);
}

TGFX_TEST(StencilCoverPathTest, Visual_TransformNonUniformScale) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildSmallCirclePath(), MakeTransformDebugPaint(),
                                kDebugBackground, ApplyNonUniformScale,
                                "StencilCoverPathDebug/TransformNonUniformScale");
}

// Pure 45-degree rotation around the surface centre. The heart's bottom tip rotates to the
// right. Validates that view matrix rotation propagates correctly through both stencil and
// cover passes — any rotation bug in the GP uniform handling would show up as kinked
// straight segments or warped cubic arcs.
static void ApplyPureRotate45(Canvas* canvas) {
  canvas->translate(150, 150);
  canvas->rotate(45);
  canvas->translate(-150, -150);
}

TGFX_TEST(StencilCoverPathTest, Visual_TransformPureRotate45) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildHeartCubicPath(), MakeTransformDebugPaint(),
                                kDebugBackground, ApplyPureRotate45,
                                "StencilCoverPathDebug/TransformPureRotate45");
}

// Large 4x scale on a tiny disc. This is where bezier and legacy diverge most visibly:
// legacy bakes the matrix into the path then triangulates the scaled-up shape with Skia's
// flattening tolerance (0.25 px), so the edge stays smooth at the new size. Bezier emits a
// fixed local-space mesh and lets the fragment shader evaluate the implicit curve at the
// scaled-up resolution — so the edge is still mathematically smooth, regardless of any
// triangle density that would otherwise show up. Both pipelines should produce a
// pixel-perfect circle filling most of the 300x300 surface.
static void ApplyLargeScale4x(Canvas* canvas) {
  canvas->translate(150, 150);
  canvas->scale(4.0f, 4.0f);
  canvas->translate(-37.5f, -37.5f);
}

TGFX_TEST(StencilCoverPathTest, Visual_TransformLargeScale4x) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  // Tiny source: 30x30 disc, scaled 4x ends up at 120x120 around the centre.
  Path path;
  path.addOval(Rect::MakeXYWH(22.5f, 22.5f, 30.f, 30.f));
  RenderPathAndCompareBaselines(context, path, MakeTransformDebugPaint(), kDebugBackground,
                                ApplyLargeScale4x, "StencilCoverPathDebug/TransformLargeScale4x");
}

// ====================================================================================
// Complex curve cases (additional debug renders, all comparing legacy vs bezier)
//
// These cases stress the curve-rasterization path with shapes the simpler debug renders
// don't reach: long quad chains, cubic chains converted via PathUtils::ConvertCubicToQuads,
// self-intersecting curves under EvenOdd, mixed line+arc contours, multi-contour shapes
// composed with addPath, and high aspect-ratio ellipses where ChopQuadAtMaxCurvature kicks
// in. Each renders both pipelines so the resulting _Legacy.webp / _Bezier.webp can be
// diffed side by side. The pure-line StarEvenOdd produced byte-identical output across
// pipelines; the curve-bearing HeartCubic / SemicircleQuad / Donut differed by 1–2 px on
// the curved edge — the cases below should reveal whether that gap stays bounded as curve
// complexity grows.
// ====================================================================================

// A long horizontal wave built from six consecutive quadratic segments, alternating up and
// down. This stresses the seam between adjacent quads: each segment's curve triangle ends
// at the next segment's start point, so any drift in the per-vertex KLM parameterisation
// would show up as a visible kink at the join. The legacy path flattens each quad into
// many small line triangles; the bezier path emits one curve triangle per quad and lets
// the fragment shader evaluate the implicit equation. Bbox is [40, 260] x [120, 220].
static Path BuildWaveQuadStripPath() {
  Path path;
  path.moveTo(40, 220);
  path.quadTo(70, 120, 100, 220);
  path.quadTo(130, 120, 160, 220);
  path.quadTo(190, 120, 220, 220);
  path.quadTo(235, 170, 260, 220);
  path.lineTo(40, 220);
  path.close();
  return path;
}

TGFX_TEST(StencilCoverPathTest, Visual_WaveQuadStrip) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildWaveQuadStripPath(),
                                "StencilCoverPathDebug/WaveQuadStrip");
}

// A spiral assembled from three cubic Bezier turns, each tightening towards the centre.
// Cubics are decomposed into a chain of quads by PathUtils::ConvertCubicToQuads with
// tolerance=1.0 before they reach the rasterizer, so this path actually emits a long
// quad chain at runtime — same mechanism as ConvertCubicToQuads on HeartCubic but with
// far more sub-quads and tighter curvature. Any error in the cubic→quads conversion or
// the chop-at-max-curvature heuristic would manifest as a wobble in the spiral arm.
// Drawn with a large stroke-like inflation via two nested contours under EvenOdd so the
// path renders as a visible ribbon rather than a hairline. Bbox roughly [50, 250].
static Path BuildSpiralCubicPath() {
  Path path;
  path.setFillType(PathFillType::EvenOdd);
  // Outer spiral arm
  path.moveTo(150, 50);
  path.cubicTo(250, 50, 250, 250, 150, 250);
  path.cubicTo(70, 250, 70, 100, 150, 100);
  path.cubicTo(200, 100, 200, 200, 150, 200);
  // Inner spiral arm — drawn as the carved-out interior so EvenOdd leaves a ribbon.
  path.cubicTo(170, 200, 170, 130, 150, 130);
  path.cubicTo(120, 130, 120, 220, 150, 220);
  path.cubicTo(220, 220, 220, 80, 150, 80);
  path.cubicTo(60, 80, 60, 270, 150, 270);
  path.close();
  return path;
}

TGFX_TEST(StencilCoverPathTest, Visual_SpiralCubic) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildSpiralCubicPath(),
                                "StencilCoverPathDebug/SpiralCubic");
}

// Five-petal flower: each petal is one cubic segment that loops out from and back to the
// centre, so consecutive petals share the centre vertex and the boundary curves cross
// inside the centre. Under EvenOdd the centre region is covered an even number of times
// and ends up unfilled — this exposes how stencil counting at curve self-intersections
// converges between the two pipelines. Petals are positioned at 5-fold symmetry with the
// centre at (150, 150). Bbox approximately [55, 245] x [55, 245].
static Path BuildFlowerSelfIntersectPath() {
  Path path;
  path.setFillType(PathFillType::EvenOdd);
  path.moveTo(150, 150);
  // Petal 1 (top)
  path.cubicTo(120, 50, 180, 50, 150, 150);
  // Petal 2 (upper right)
  path.cubicTo(245, 105, 215, 175, 150, 150);
  // Petal 3 (lower right)
  path.cubicTo(225, 235, 165, 235, 150, 150);
  // Petal 4 (lower left)
  path.cubicTo(135, 235, 75, 235, 150, 150);
  // Petal 5 (upper left)
  path.cubicTo(85, 175, 55, 105, 150, 150);
  path.close();
  return path;
}

TGFX_TEST(StencilCoverPathTest, Visual_FlowerSelfIntersect) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildFlowerSelfIntersectPath(),
                                "StencilCoverPathDebug/FlowerSelfIntersect");
}

// Rounded rectangle via Path::addRoundRect, which internally emits a sequence of conic
// verbs (quarter-circle corners) joined by line segments along the edges. Conics are
// expanded to quads by NoConicsPathIterator before they reach the rasterizer, so this is
// the canonical "mixed line + curve" multi-contour path that real-world UI rendering
// hits. Bbox is [40, 260] x [40, 260] with 40 px corner radius.
static Path BuildRoundedRectPath() {
  Path path;
  path.addRoundRect(Rect::MakeXYWH(40, 40, 220, 220), 40, 40);
  return path;
}

TGFX_TEST(StencilCoverPathTest, Visual_RoundedRect) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildRoundedRectPath(),
                                "StencilCoverPathDebug/RoundedRect");
}

// Lollipop: a circle (head) joined to a thin rectangle (stick) via Path::addPath. Two
// contours, one curve-only and one line-only, sharing the same fill. Tests that the
// rasterizer's per-contour state (start/current point, contourOpen flag) resets cleanly
// between contours and that the cover quad's local-bounds rect spans both contours
// correctly. Bbox is [115, 240] x [40, 260] (head: [80, 220]², stick: [140, 160] x
// [220, 260]).
static Path BuildLollipopPath() {
  Path head;
  head.addOval(Rect::MakeXYWH(80, 40, 140, 140));

  Path stick;
  stick.addRect(Rect::MakeXYWH(140, 160, 20, 100));

  Path path;
  path.addPath(head);
  path.addPath(stick);
  return path;
}

TGFX_TEST(StencilCoverPathTest, Visual_Lollipop) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildLollipopPath(), "StencilCoverPathDebug/Lollipop");
}

// Highly elongated ellipse (10:1 aspect ratio). The conic→quad expansion produces quads
// with very high curvature at the two ends and very low curvature along the long sides —
// the perfect input for ChopQuadAtMaxCurvature, which subdivides high-curvature quads
// at their max-curvature parameter to keep `u² - v > 0` well-defined. If the chop
// threshold is wrong, the ellipse caps would show kinks; if the chop is too aggressive,
// the long flat sides accumulate more triangles than necessary but visually match.
// Bbox is [30, 270] x [130, 170].
static Path BuildNarrowEllipsePath() {
  Path path;
  path.addOval(Rect::MakeXYWH(30, 130, 240, 40));
  return path;
}

TGFX_TEST(StencilCoverPathTest, Visual_NarrowEllipse) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildNarrowEllipsePath(),
                                "StencilCoverPathDebug/NarrowEllipse");
}

}  // namespace tgfx

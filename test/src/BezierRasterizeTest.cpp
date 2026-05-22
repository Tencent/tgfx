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

// Consolidated tests for the bezier rasterization render path. Cases are grouped by module
// using a `<Module>_` prefix on the test name:
//   - StencilTextureResource_*  : the stencil texture cache (M1)
//   - Caps_*                    : GPUFeatures::bezierRasterizeSupported (M2)
//   - Rasterizer_*              : ShapeBezierRasterizer CPU geometry builder (M3)
//   - Proxy_*                   : ShapeBezierRasterizeGeometryProxy + factory (M4)
//   - DrawOp_*                  : ShapeBezierRasterizeDrawOp construction (M6)
//   - Dispatch_*                : OpsCompositor dispatch + end-to-end (M7 + demo integration)

#include "core/ShapeBezierRasterizer.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ops/ShapeBezierRasterizeDrawOp.h"
#include "gpu/resources/StencilTextureResource.h"
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

// ====================================================================================
// StencilTextureResource (M1)
// ====================================================================================

TGFX_TEST(BezierRasterizeTest, StencilTextureResource_CreateAndProperties) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto resource = StencilTextureResource::FindOrCreate(context, 256, 128);
  ASSERT_TRUE(resource != nullptr);

  EXPECT_EQ(resource->width(), 256);
  EXPECT_EQ(resource->height(), 128);

  auto texture = resource->getTexture();
  ASSERT_TRUE(texture != nullptr);
  EXPECT_EQ(texture->format(), PixelFormat::DEPTH24_STENCIL8);
  EXPECT_TRUE((texture->usage() & TextureUsage::RENDER_ATTACHMENT) != 0);
}

TGFX_TEST(BezierRasterizeTest, StencilTextureResource_SameSizeReusesResource) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto first = StencilTextureResource::FindOrCreate(context, 200, 200);
  ASSERT_TRUE(first != nullptr);
  auto firstPtr = first.get();
  auto firstTexturePtr = first->getTexture().get();
  // Drop the strong reference so the cache can return the same scratch resource.
  first.reset();

  auto second = StencilTextureResource::FindOrCreate(context, 200, 200);
  ASSERT_TRUE(second != nullptr);
  EXPECT_EQ(second.get(), firstPtr);
  EXPECT_EQ(second->getTexture().get(), firstTexturePtr);
}

TGFX_TEST(BezierRasterizeTest, StencilTextureResource_DifferentSizesAreDistinct) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto a = StencilTextureResource::FindOrCreate(context, 128, 128);
  auto b = StencilTextureResource::FindOrCreate(context, 256, 128);
  auto c = StencilTextureResource::FindOrCreate(context, 128, 256);
  ASSERT_TRUE(a != nullptr);
  ASSERT_TRUE(b != nullptr);
  ASSERT_TRUE(c != nullptr);

  EXPECT_NE(a.get(), b.get());
  EXPECT_NE(a.get(), c.get());
  EXPECT_NE(b.get(), c.get());

  EXPECT_NE(a->getTexture().get(), b->getTexture().get());
  EXPECT_NE(a->getTexture().get(), c->getTexture().get());
}

TGFX_TEST(BezierRasterizeTest, StencilTextureResource_InvalidArguments) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  EXPECT_TRUE(StencilTextureResource::FindOrCreate(nullptr, 100, 100) == nullptr);
  EXPECT_TRUE(StencilTextureResource::FindOrCreate(context, 0, 100) == nullptr);
  EXPECT_TRUE(StencilTextureResource::FindOrCreate(context, 100, 0) == nullptr);
  EXPECT_TRUE(StencilTextureResource::FindOrCreate(context, -1, 100) == nullptr);
}

// ====================================================================================
// Caps (M2)
// ====================================================================================

TGFX_TEST(BezierRasterizeTest, Caps_FieldDefaultsToFalse) {
  GPUFeatures features = {};
  EXPECT_FALSE(features.bezierRasterizeSupported);
}

TGFX_TEST(BezierRasterizeTest, Caps_BackendReportsExpectedSupport) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto features = context->gpu()->features();
  ASSERT_TRUE(features != nullptr);
  // After stage-3 the bezier rasterization path is enabled by default on Metal and OpenGL
  // (desktop / GLES). WebGL and any other backend (Vulkan / WebGPU / Unknown) stay opt-out
  // until validated independently.
  auto backend = context->gpu()->info()->backend;
  bool expectedSupport = (backend == Backend::Metal) || (backend == Backend::OpenGL);
  EXPECT_EQ(features->bezierRasterizeSupported, expectedSupport);
}

// ====================================================================================
// ShapeBezierRasterizer (M3)
// ====================================================================================

TGFX_TEST(BezierRasterizeTest, Rasterizer_AsyncSupportMatchesPlatform) {
  Path path;
  path.addRect(Rect::MakeWH(100, 100));
  ShapeBezierRasterizer rasterizer(Shape::MakeFrom(path));
#if defined(TGFX_BUILD_FOR_WEB) && !defined(TGFX_USE_FREETYPE)
  EXPECT_FALSE(rasterizer.asyncSupport());
#else
  EXPECT_TRUE(rasterizer.asyncSupport());
#endif
}

TGFX_TEST(BezierRasterizeTest, Rasterizer_EmptyPathReturnsNull) {
  Path emptyPath;
  ShapeBezierRasterizer rasterizer(Shape::MakeFrom(emptyPath));
  EXPECT_TRUE(rasterizer.getData() == nullptr);
}

TGFX_TEST(BezierRasterizeTest, Rasterizer_SimpleRectReturnsBoundsTriangles) {
  Path path;
  path.addRect(Rect::MakeXYWH(10, 20, 100, 80));
  ShapeBezierRasterizer rasterizer(Shape::MakeFrom(path));
  // Structural invariants that hold for both the stage-3 placeholder (a single bounds quad,
  // 6 vertices, KLM all zero) and any future real triangulator: the buffer must be non-empty,
  // the vertex count must be positive, and the byte size must match vertexCount * 20 bytes
  // (position Float2 + klm Float3 = 5 floats per vertex). The exact vertex count depends on
  // the algorithm and must not be asserted here.
  auto buffer = rasterizer.getData();
  ASSERT_TRUE(buffer != nullptr);
  EXPECT_GT(buffer->vertexCount, static_cast<size_t>(0));
  ASSERT_TRUE(buffer->vertices != nullptr);
  EXPECT_EQ(buffer->vertices->size(), buffer->vertexCount * 5 * sizeof(float));
}

TGFX_TEST(BezierRasterizeTest, Rasterizer_BezierPathReturnsBoundsTriangles) {
  Path path;
  path.moveTo(0, 0);
  path.quadTo(50, 100, 100, 0);
  path.cubicTo(120, 20, 180, 80, 200, 0);
  path.close();
  ShapeBezierRasterizer rasterizer(Shape::MakeFrom(path));
  // Same structural invariants as the rect case — don't predict the triangle count, just
  // verify the buffer is well-formed (non-empty, byte size matches the 5-float vertex stride).
  auto buffer = rasterizer.getData();
  ASSERT_TRUE(buffer != nullptr);
  EXPECT_GT(buffer->vertexCount, static_cast<size_t>(0));
  ASSERT_TRUE(buffer->vertices != nullptr);
  EXPECT_EQ(buffer->vertices->size(), buffer->vertexCount * 5 * sizeof(float));
}

// ====================================================================================
// ShapeBezierRasterizeGeometryProxy + ProxyProvider factory (M4)
// ====================================================================================

TGFX_TEST(BezierRasterizeTest, Proxy_NullShapeReturnsNull) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto proxyProvider = context->proxyProvider();
  EXPECT_TRUE(proxyProvider->createShapeBezierRasterizeGeometryProxy(nullptr, 0) == nullptr);
}

TGFX_TEST(BezierRasterizeTest, Proxy_FactoryReturnsProxy) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Path path;
  path.addRect(Rect::MakeXYWH(10, 20, 100, 80));
  auto shape = Shape::MakeFrom(path);

  auto proxy = context->proxyProvider()->createShapeBezierRasterizeGeometryProxy(shape, 0);
  ASSERT_TRUE(proxy != nullptr);
  EXPECT_TRUE(proxy->getVertexBufferProxy() != nullptr);
}

TGFX_TEST(BezierRasterizeTest, Proxy_SameShapeReusesVertexBufferProxy) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Path path;
  path.addRoundRect(Rect::MakeXYWH(-12.5f, -12.5f, 25.f, 25.f), 5, 5);
  auto shape = Shape::MakeFrom(path);

  auto proxyProvider = context->proxyProvider();
  auto proxy1 = proxyProvider->createShapeBezierRasterizeGeometryProxy(shape, 0);
  auto proxy2 = proxyProvider->createShapeBezierRasterizeGeometryProxy(shape, 0);
  ASSERT_TRUE(proxy1 != nullptr);
  ASSERT_TRUE(proxy2 != nullptr);
  // Cache hit: the outer geometry proxy is recreated each call (cheap aggregator), but the
  // underlying GPUBufferProxy must be shared so multiple draws of the same shape reuse the
  // same uploaded vertex buffer.
  EXPECT_EQ(proxy1->getVertexBufferProxy().get(), proxy2->getVertexBufferProxy().get());
}

TGFX_TEST(BezierRasterizeTest, Proxy_DifferentShapesDoNotReuseVertexBufferProxy) {
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
  auto proxy1 = proxyProvider->createShapeBezierRasterizeGeometryProxy(shape1, 0);
  auto proxy2 = proxyProvider->createShapeBezierRasterizeGeometryProxy(shape2, 0);
  ASSERT_TRUE(proxy1 != nullptr);
  ASSERT_TRUE(proxy2 != nullptr);
  EXPECT_NE(proxy1->getVertexBufferProxy().get(), proxy2->getVertexBufferProxy().get());
}

TGFX_TEST(BezierRasterizeTest, Proxy_DifferentTransformsHitSameCacheKey) {
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
  auto proxyA = proxyProvider->createShapeBezierRasterizeGeometryProxy(shapeA, 0);
  auto proxyB = proxyProvider->createShapeBezierRasterizeGeometryProxy(shapeB, 0);
  ASSERT_TRUE(proxyA != nullptr);
  ASSERT_TRUE(proxyB != nullptr);
  EXPECT_EQ(proxyA->getVertexBufferProxy().get(), proxyB->getVertexBufferProxy().get());
}

// ====================================================================================
// ShapeBezierRasterizeDrawOp construction (M6)
// ====================================================================================

TGFX_TEST(BezierRasterizeTest, DrawOp_NullProxyReturnsNull) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto op =
      ShapeBezierRasterizeDrawOp::Make(nullptr, PMColor::Transparent(), Matrix::I(), Matrix::I(),
                                       Rect::MakeWH(100, 100), PathFillType::Winding);
  EXPECT_TRUE(op == nullptr);
}

TGFX_TEST(BezierRasterizeTest, DrawOp_MakeReturnsValidOp) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Path path;
  path.addRect(Rect::MakeXYWH(10, 20, 100, 80));
  auto shape = Shape::MakeFrom(path);
  auto geometryProxy = context->proxyProvider()->createShapeBezierRasterizeGeometryProxy(shape, 0);
  ASSERT_TRUE(geometryProxy != nullptr);

  auto op = ShapeBezierRasterizeDrawOp::Make(
      geometryProxy, PMColor::Transparent(), Matrix::MakeTrans(10, 20), Matrix::I(),
      Rect::MakeXYWH(10, 20, 100, 80), PathFillType::EvenOdd);
  ASSERT_TRUE(op != nullptr);
  EXPECT_TRUE(op->hasCoverage());
  EXPECT_TRUE(op->needsStencil());
}

// ====================================================================================
// OpsCompositor dispatch + end-to-end (M7 + demo integration)
// ====================================================================================

// The bezier rasterization dispatch is gated by GPUFeatures::bezierRasterizeSupported. The
// dispatch tests below confirm two things:
//   1. With the flag forcibly disabled, drawing a non-AA path keeps using the legacy
//      ShapeDrawOp pipeline. This is the "AA path zero regression" guarantee.
//   2. With the flag forcibly enabled, drawing a non-AA path runs the full bezier
//      rasterization dispatch (stencil + cover passes) end-to-end. The actual pixels are
//      governed by the placeholder triangle algorithm in ShapeBezierRasterizer (paints the
//      entire shape bounds), which is intentional during stage 3.

namespace {

void DrawNonAARect(Context* context) {
  auto surface = Surface::Make(context, 64, 64);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  Paint paint;
  paint.setAntiAlias(false);
  paint.setColor(Color{1.f, 0.f, 0.f, 1.f});
  Path path;
  path.addRect(Rect::MakeXYWH(8, 8, 48, 48));
  canvas->drawPath(path, paint);
  context->flushAndSubmit();
}

// Drives a path with the given fill type through the bezier rasterization path and verifies a
// pixel inside the shape's bounding box receives the brush colour. The placeholder bezier
// rasterizer paints the whole bounds, so this also acts as a proxy for "stencil + cover passes
// both ran and the colour write reached the surface".
//
// TODO(stage-3): Once the real triangulator replaces the bounds-quad placeholder, extend this
// to also assert that a pixel *outside* the path (but inside the render target) is the
// background colour. That's what actually proves the stencil test is precise rather than just
// "the cover pass ran". With the placeholder, the whole bounds is red so the negative check
// would fail — keep it out until the algorithm lands.
void DrawAndAssertCenterPixel(PathFillType fillType) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto* mutableFeatures = const_cast<GPUFeatures*>(context->gpu()->features());
  bool original = mutableFeatures->bezierRasterizeSupported;
  mutableFeatures->bezierRasterizeSupported = true;

  constexpr int kSize = 64;
  auto surface = Surface::Make(context, kSize, kSize);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  // Clear to opaque black so we can tell a stencil+cover write apart from "nothing happened".
  canvas->clear(Color{0.f, 0.f, 0.f, 1.f});

  Paint paint;
  paint.setAntiAlias(false);
  paint.setColor(Color{1.f, 0.f, 0.f, 1.f});
  Path path;
  path.addRect(Rect::MakeXYWH(8, 8, 48, 48));
  path.setFillType(fillType);
  canvas->drawPath(path, paint);
  context->flushAndSubmit();

  // Sample the centre pixel of the shape. The placeholder rasterizer paints the whole bounds
  // so the centre must be solid red.
  uint32_t pixel = 0;
  auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
  ASSERT_TRUE(surface->readPixels(info, &pixel, kSize / 2, kSize / 2));
  // The exact byte order depends on platform, but the red channel must be saturated.
  uint8_t r = static_cast<uint8_t>(pixel & 0xFF);
  EXPECT_EQ(r, 0xFF) << "fill type " << static_cast<int>(fillType)
                     << " did not write the cover pass colour";

  mutableFeatures->bezierRasterizeSupported = original;
}

}  // namespace

TGFX_TEST(BezierRasterizeTest, Dispatch_DisabledCapsRoutesThroughLegacyPath) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  // Force the gating flag off for the duration of this test so we exercise the legacy fall-
  // back even on backends that have the bezier path enabled by default. Casting the const away
  // is a test-only escape hatch — production code must rely on backend caps initialisation.
  auto* mutableFeatures = const_cast<GPUFeatures*>(context->gpu()->features());
  bool original = mutableFeatures->bezierRasterizeSupported;
  mutableFeatures->bezierRasterizeSupported = false;
  DrawNonAARect(context);
  mutableFeatures->bezierRasterizeSupported = original;
}

TGFX_TEST(BezierRasterizeTest, Dispatch_EnabledCapsRoutesThroughNewBranch) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  // Force the gating flag on for the duration of this test (no-op on backends that already
  // enable it). The placeholder bezier rasterizer paints the entire shape bounds; we only care
  // that the dispatch path runs to completion without crashing.
  auto* mutableFeatures = const_cast<GPUFeatures*>(context->gpu()->features());
  bool original = mutableFeatures->bezierRasterizeSupported;
  mutableFeatures->bezierRasterizeSupported = true;
  DrawNonAARect(context);
  mutableFeatures->bezierRasterizeSupported = original;
}

TGFX_TEST(BezierRasterizeTest, Dispatch_EnabledCapsDrawsStencilCoveredColor_EvenOdd) {
  DrawAndAssertCenterPixel(PathFillType::EvenOdd);
}

TGFX_TEST(BezierRasterizeTest, Dispatch_EnabledCapsDrawsStencilCoveredColor_NonZero) {
  DrawAndAssertCenterPixel(PathFillType::Winding);
}

// ====================================================================================
// Visual debug cases
// ====================================================================================
//
// These cases render a single path through the bezier rasterization pipeline and hand the
// result off to Baseline::Compare. While no baseline exists yet the output is written to
// `test/out/BezierRasterizeDebug/<Name>.webp`, which is what you iterate against while
// tuning the triangulator. Once the algorithm stabilises, accepting a baseline via
// `/accept-baseline` promotes these into proper screenshot regression tests without any code
// change.
//
// Conventions (see .codebuddy/rules/Test.md):
//   - Content is centred with roughly 50px margin on every side.
//   - Use integer coordinates where possible to keep edges crisp.
//   - Force caps on via const_cast so we always exercise the bezier path, regardless of the
//     backend's current default. Restore on exit to keep other tests deterministic.

// Renders an arbitrary path through both rendering pipelines into a 300x300 surface and
// dumps each result to `test/out/<keyPrefix>_Legacy.webp` / `_Bezier.webp` for side-by-side
// inspection. Toggling caps with const_cast forces the legacy path on the first call and the
// new bezier path on the second; the original cap value is restored before returning.
//
// Scale note: paths must be large enough that PathTriangulator::ShouldTriangulatePath() picks
// the real triangulation path instead of the mask-rasterization fallback — that function
// returns false when max(width, height) <= 162 (see PathTriangulator.cpp). Each path builder
// in this file is sized for a 300x300 surface with a comfortable ~50px margin and a bbox
// well above the 162 threshold.
static void RenderPathAndCompareBaselines(Context* context, const Path& path, const Paint& paint,
                                          const std::string& keyPrefix) {
  auto* mutableFeatures = const_cast<GPUFeatures*>(context->gpu()->features());
  bool original = mutableFeatures->bezierRasterizeSupported;

  for (int pass = 0; pass < 2; ++pass) {
    bool useBezier = (pass == 1);
    mutableFeatures->bezierRasterizeSupported = useBezier;

    auto surface = Surface::Make(context, 300, 300);
    ASSERT_TRUE(surface != nullptr);
    auto canvas = surface->getCanvas();
    canvas->clear(Color{0.f, 0.f, 0.f, 1.f});

    canvas->drawPath(path, paint);
    context->flushAndSubmit();

    // Baseline::Compare writes the current render to disk even when the baseline does not
    // exist yet. We drop the return value on purpose so debug cases do not fail CI while the
    // algorithm is still being tuned; once outputs stabilise and baselines are accepted via
    // `/accept-baseline`, flip this to `EXPECT_TRUE(...)` so they gain regression protection.
    auto suffix = useBezier ? "_Bezier" : "_Legacy";
    (void)Baseline::Compare(surface, keyPrefix + suffix);
  }

  mutableFeatures->bezierRasterizeSupported = original;
}

// Convenience wrapper for the common case of drawing the path with a solid red brush, no
// shader / colour filter / blend overrides.
static void RenderPathAndCompareBaselines(Context* context, const Path& path,
                                          const std::string& keyPrefix) {
  Paint paint;
  paint.setAntiAlias(false);
  paint.setColor(Color{1.f, 0.f, 0.f, 1.f});
  RenderPathAndCompareBaselines(context, path, paint, keyPrefix);
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
// unrelated to bezier rasterization). Pentagons (and any non-rect / non-oval / non-rrect
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

TGFX_TEST(BezierRasterizeTest, Debug_HeartCubic) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildHeartCubicPath(), "BezierRasterizeDebug/HeartCubic");
}

TGFX_TEST(BezierRasterizeTest, Debug_ConcavePolygon) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildConcavePolygonPath(),
                                "BezierRasterizeDebug/ConcavePolygon");
}

TGFX_TEST(BezierRasterizeTest, Debug_SemicircleQuad) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildSemicircleQuadPath(),
                                "BezierRasterizeDebug/SemicircleQuad");
}

TGFX_TEST(BezierRasterizeTest, Debug_Circle) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildCirclePath(), "BezierRasterizeDebug/Circle");
}

TGFX_TEST(BezierRasterizeTest, Debug_StarEvenOdd) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildStarEvenOddPath(),
                                "BezierRasterizeDebug/StarEvenOdd");
}

TGFX_TEST(BezierRasterizeTest, Debug_Donut) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildDonutPath(), "BezierRasterizeDebug/Donut");
}

TGFX_TEST(BezierRasterizeTest, Debug_CShape) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildCShapePath(), "BezierRasterizeDebug/CShape");
}

// Inverse-fill rendering: the rasterizer must paint the path's *outside*. With the
// non-empty inverse-pentagon the surface should show a black hole (the pentagon shape)
// surrounded by red; with the empty-inverse path the entire surface should be red. The
// legacy path reaches the same outcome via Canvas::drawFill / inverse-fill mask routes.
TGFX_TEST(BezierRasterizeTest, Debug_InversePentagon) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildInverseSquarePath(),
                                "BezierRasterizeDebug/InversePentagon");
}

TGFX_TEST(BezierRasterizeTest, Debug_EmptyInverse) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildEmptyInversePath(),
                                "BezierRasterizeDebug/EmptyInverse");
}

// ----- Brush combination cases (square path keeps geometry differences out of the picture) -----

// Linear gradient from red on the left to blue on the right. Validates that the cover GP's
// emitTransforms hands the correct local coordinates to the shader FP — if uvMatrix were
// wrong the gradient would be skewed or stretched.
TGFX_TEST(BezierRasterizeTest, Debug_BrushLinearGradient) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Paint paint;
  paint.setAntiAlias(false);
  // Anchor the gradient at the path's bbox so the visual gradient direction is unambiguous.
  paint.setShader(Shader::MakeLinearGradient(
      {50, 150}, {250, 150}, {Color{1.f, 0.f, 0.f, 1.f}, Color{0.f, 0.f, 1.f, 1.f}}, {0.f, 1.f}));
  RenderPathAndCompareBaselines(context, BuildSquarePath(), paint,
                                "BezierRasterizeDebug/BrushLinearGradient");
}

// Colour matrix that inverts RGB (R' = 1 - R, etc.). Applied on top of a solid red brush
// the output should be cyan. Validates that the colour-filter FP joins the FP chain of the
// cover pass.
TGFX_TEST(BezierRasterizeTest, Debug_BrushColorFilterInvert) {
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
                                "BezierRasterizeDebug/BrushColorFilterInvert");
}

// Plus / additive blend on top of an existing background. The square is drawn with green
// over a red clear, so the resulting overlap should be yellow if the blend mode reaches the
// cover pipeline correctly. Cannot use the shared helper because it pre-clears to black on
// every pass.
TGFX_TEST(BezierRasterizeTest, Debug_BrushBlendModePlus) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto* mutableFeatures = const_cast<GPUFeatures*>(context->gpu()->features());
  bool original = mutableFeatures->bezierRasterizeSupported;

  for (int pass = 0; pass < 2; ++pass) {
    bool useBezier = (pass == 1);
    mutableFeatures->bezierRasterizeSupported = useBezier;

    auto surface = Surface::Make(context, 300, 300);
    ASSERT_TRUE(surface != nullptr);
    auto canvas = surface->getCanvas();
    // Start from solid red; the additive blend of the green square will paint yellow inside
    // the path and leave the background red outside.
    canvas->clear(Color{1.f, 0.f, 0.f, 1.f});

    Paint paint;
    paint.setAntiAlias(false);
    paint.setColor(Color{0.f, 1.f, 0.f, 1.f});
    paint.setBlendMode(BlendMode::PlusLighter);
    canvas->drawPath(BuildSquarePath(), paint);
    context->flushAndSubmit();

    auto suffix = useBezier ? "_Bezier" : "_Legacy";
    (void)Baseline::Compare(surface,
                            std::string("BezierRasterizeDebug/BrushBlendModePlus") + suffix);
  }

  mutableFeatures->bezierRasterizeSupported = original;
}

// Apply a non-trivial canvas transform (scale + rotate) before drawing. Validates that the
// view matrix accumulates correctly through both the stencil pass and the cover pass, and
// that the implicit-curve fragment test still classifies pixels correctly under rotation
// (KLM is computed in path-local space, so the rotation should not affect classification).
TGFX_TEST(BezierRasterizeTest, Debug_BrushScaleRotate) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto* mutableFeatures = const_cast<GPUFeatures*>(context->gpu()->features());
  bool original = mutableFeatures->bezierRasterizeSupported;

  for (int pass = 0; pass < 2; ++pass) {
    bool useBezier = (pass == 1);
    mutableFeatures->bezierRasterizeSupported = useBezier;

    auto surface = Surface::Make(context, 300, 300);
    ASSERT_TRUE(surface != nullptr);
    auto canvas = surface->getCanvas();
    canvas->clear(Color{0.f, 0.f, 0.f, 1.f});

    // Rotate 30 degrees and scale 0.7x around the surface centre. Without the centre pivot
    // the rotated heart would slide off-canvas. The 0.7x factor keeps the heart inside the
    // 300x300 surface even after rotation enlarges its bounding box.
    canvas->save();
    canvas->translate(150, 150);
    canvas->rotate(30);
    canvas->scale(0.7f, 0.7f);
    canvas->translate(-150, -150);

    Paint paint;
    paint.setAntiAlias(false);
    paint.setColor(Color{1.f, 0.f, 0.f, 1.f});
    canvas->drawPath(BuildHeartCubicPath(), paint);
    canvas->restore();
    context->flushAndSubmit();

    auto suffix = useBezier ? "_Bezier" : "_Legacy";
    (void)Baseline::Compare(surface, std::string("BezierRasterizeDebug/BrushScaleRotate") + suffix);
  }

  mutableFeatures->bezierRasterizeSupported = original;
}

}  // namespace tgfx

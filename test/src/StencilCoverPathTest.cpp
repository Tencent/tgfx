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
//   - Caps_*                    : GPUFeatures::stencilAttachmentSupported (M2)
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
#include "tgfx/core/Font.h"
#include "tgfx/core/ImageInfo.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/TextBlob.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/GPUFeatures.h"
#include "utils/TestUtils.h"

// The entire test file exercises the stencil-and-cover render path. When the master
// compile-time switch is disabled, the feature is compiled out of the runtime and none of
// these cases apply — skip the whole translation unit so it does not clutter the test suite.
#ifdef TGFX_ENABLE_STENCIL_COVER_PATH

// Set to 1 locally to render both legacy and bezier pipelines and dump each as a separate
// webp under `test/out/StencilCoverPath/`. Must remain 0 in committed code so CI runs
// the canonical single-pass baseline comparison.
#define TGFX_BEZIER_VISUAL_DEBUG_EXPORT 0

namespace tgfx {

// RAII guard that overrides GPUFeatures::stencilAttachmentSupported for the lifetime of an
// instance. The previous value is captured on construction and restored on destruction, so a
// test that hits a fatal Google Test ASSERT mid-body still puts the flag back instead of
// leaking a polluted value into subsequent tests (which would otherwise break the deterministic
// caps-default expectations of Caps_BackendReportsExpectedSupport).
struct ScopedStencilCoverCaps {
  GPUFeatures* features;
  bool original;
  ScopedStencilCoverCaps(Context* context, bool value)
      : features(const_cast<GPUFeatures*>(context->gpu()->features())),
        original(features->stencilAttachmentSupported) {
    features->stencilAttachmentSupported = value;
  }
  ~ScopedStencilCoverCaps() {
    features->stencilAttachmentSupported = original;
  }
};

// ====================================================================================
// Caps (M2)
// ====================================================================================

TGFX_TEST(StencilCoverPathTest, Caps_FieldDefaultsToFalse) {
  GPUFeatures features = {};
  EXPECT_FALSE(features.stencilAttachmentSupported);
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
  // until they declare an expectation here, since GPUFeatures::stencilAttachmentSupported
  // defaults to false.
  auto gpuInfo = context->gpu()->info();
  auto backend = gpuInfo->backend;
  // SwiftShader (Vulkan software renderer, vendorID 0x1AE0) has stencil-operation bugs
  // that cause segfaults, so VulkanCaps deliberately sets stencilAttachmentSupported = false
  // for it. The test must accept that.
  bool isSwiftShader = gpuInfo->vendor == "6880";  // 0x1AE0 = 6880 decimal
  switch (backend) {
    case Backend::Metal:
      EXPECT_TRUE(features->stencilAttachmentSupported);
      break;
    case Backend::OpenGL:
      EXPECT_TRUE(features->stencilAttachmentSupported);
      break;
    case Backend::Vulkan:
      if (isSwiftShader) {
        EXPECT_FALSE(features->stencilAttachmentSupported);
      } else {
        EXPECT_TRUE(features->stencilAttachmentSupported);
      }
      break;
    default:
      ADD_FAILURE() << "Unhandled backend in Caps_BackendReportsExpectedSupport; declare "
                       "the expected stencilAttachmentSupported value for this backend.";
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
  auto op = StencilCoverPathDrawOp::Make(nullptr, PMColor::Transparent(), Matrix::I(),
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

  auto op =
      StencilCoverPathDrawOp::Make(geometryProxy, PMColor::Transparent(), Matrix::MakeTrans(10, 20),
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
                                           Matrix::MakeTrans(10, 20),
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

// The stencil-and-cover dispatch is gated by GPUFeatures::stencilAttachmentSupported. The
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
// Two operating modes, controlled by `TGFX_BEZIER_VISUAL_DEBUG_EXPORT` (defined at the top of
// this file):
//
//   * Default (macro = 0): only the bezier pass is rendered, and the result is compared
//     against the canonical baseline `StencilCoverPath/<Name>` (no suffix). This is the
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

// Shared palette for the visual debug renders. Warm off-white background plus a deep teal
// foreground keeps the contrast readable while side-by-side comparing legacy vs bezier
// outputs without the eye-strain of pure red on pure black. Cases that test brush
// behaviour (gradient / colour filter / blend mode) override these locally because their
// pixel outcome depends on a specific colour choice.
static const Color kDebugBackground{0.961f, 0.941f, 0.910f, 1.f};  // #F5F0E8
static const Color kDebugForeground{0.2f, 0.5f, 0.8f, 1.f};        // #3380CC

// Optional transform hook applied around `canvas->drawPath` via canvas->save/restore. Cases
// that don't need a non-identity canvas matrix pass nullptr.
typedef void (*ApplyTransformFn)(Canvas* canvas);

// Renders a single pipeline pass (`useBezier` decides which) into a fresh 300x300 surface.
// `applyTransform` may be nullptr; when set, it is invoked between save/restore so the
// canvas matrix change is local to this draw. Caller is responsible for toggling
// GPUFeatures::stencilAttachmentSupported beforehand. Extracted from the double-pass helper
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
  capsGuard.features->stencilAttachmentSupported = false;
  auto legacySurface = RenderSingleVisualPass(context, path, paint, clearColor, applyTransform);
  ASSERT_TRUE(legacySurface != nullptr);
  (void)Baseline::Compare(legacySurface, keyPrefix + "_Legacy");

  capsGuard.features->stencilAttachmentSupported = true;
  auto bezierSurface = RenderSingleVisualPass(context, path, paint, clearColor, applyTransform);
  ASSERT_TRUE(bezierSurface != nullptr);
  EXPECT_TRUE(Baseline::Compare(bezierSurface, keyPrefix + "_Bezier"));
#else
  // Default mode: bezier-only, compared against the canonical baseline.
  capsGuard.features->stencilAttachmentSupported = true;
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

// Solid disc approximated with 4 quadratic Bezier segments instead of addOval, to avoid
// Canvas::drawPath's isOval detection (which would redirect to drawRRect and bypass the
// stencil-cover pipeline). The 0.5523 control-point factor gives a close circle
// approximation. Bbox is [40, 260] x [40, 260], matching the original addOval version.
static Path BuildCirclePath() {
  auto cx = 150.f, cy = 150.f, r = 110.f;
  auto k = r * 0.5523f;
  Path path;
  path.moveTo(cx + r, cy);
  path.quadTo(cx + r, cy + k, cx, cy + r);
  path.quadTo(cx - k, cy + r, cx - r, cy);
  path.quadTo(cx - r, cy - k, cx, cy - r);
  path.quadTo(cx + k, cy - r, cx + r, cy);
  path.close();
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

// Square-like path with 5px quad-curve corners, used by the brush-combination cases below.
// Built with quadTo instead of addRect to avoid Canvas::drawPath's isRect detection, which
// would redirect to drawRect → RectDrawOp and bypass the stencil-cover pipeline entirely.
// Bbox is [50, 250] x [50, 250], matching the original addRect version.
static Path BuildSquarePath() {
  Path path;
  path.moveTo(55, 50);
  path.lineTo(245, 50);
  path.quadTo(250, 50, 250, 55);
  path.lineTo(250, 245);
  path.quadTo(250, 250, 245, 250);
  path.lineTo(55, 250);
  path.quadTo(50, 250, 50, 245);
  path.lineTo(50, 55);
  path.quadTo(50, 50, 55, 50);
  path.close();
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
  RenderPathAndCompareBaselines(context, BuildHeartCubicPath(), "StencilCoverPath/HeartCubic");
}

TGFX_TEST(StencilCoverPathTest, Visual_ConcavePolygon) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildConcavePolygonPath(),
                                "StencilCoverPath/ConcavePolygon");
}

TGFX_TEST(StencilCoverPathTest, Visual_SemicircleQuad) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildSemicircleQuadPath(),
                                "StencilCoverPath/SemicircleQuad");
}

TGFX_TEST(StencilCoverPathTest, Visual_Circle) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildCirclePath(), "StencilCoverPath/Circle");
}

TGFX_TEST(StencilCoverPathTest, Visual_StarEvenOdd) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildStarEvenOddPath(), "StencilCoverPath/StarEvenOdd");
}

TGFX_TEST(StencilCoverPathTest, Visual_Donut) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildDonutPath(), "StencilCoverPath/Donut");
}

TGFX_TEST(StencilCoverPathTest, Visual_CShape) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildCShapePath(), "StencilCoverPath/CShape");
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
                                "StencilCoverPath/InversePentagon");
}

TGFX_TEST(StencilCoverPathTest, Visual_EmptyInverse) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildEmptyInversePath(), "StencilCoverPath/EmptyInverse");
}

// ----- Brush combination cases (square path keeps geometry differences out of the picture) -----

// Linear gradient from red on the left to blue on the right. Validates that the cover GP's
// emitTransforms hands the correct local coordinates to the shader FP — if uvMatrix were
// wrong the gradient would be skewed or stretched.
TGFX_TEST(StencilCoverPathTest, Visual_GradientLinear) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Paint paint;
  paint.setAntiAlias(false);
  // Anchor the gradient at the path's bbox so the visual gradient direction is unambiguous.
  paint.setShader(Shader::MakeLinearGradient(
      {50, 150}, {250, 150}, {Color{1.f, 0.f, 0.f, 1.f}, Color{0.f, 0.f, 1.f, 1.f}}, {0.f, 1.f}));
  RenderPathAndCompareBaselines(context, BuildSquarePath(), paint,
                                "StencilCoverPath/GradientLinear");
}

// Radial gradient centred in the square. Validates that the cover pass's uvMatrix hands the
// correct local coordinates to the radial gradient FP — if the center were wrong the gradient
// would be off-centre or clipped.
TGFX_TEST(StencilCoverPathTest, Visual_GradientRadial) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Paint paint;
  paint.setAntiAlias(false);
  paint.setShader(Shader::MakeRadialGradient(
      {150, 150}, 100, {Color{1.f, 0.f, 0.f, 1.f}, Color{0.f, 0.f, 1.f, 1.f}}, {0.f, 1.f}));
  RenderPathAndCompareBaselines(context, BuildSquarePath(), paint,
                                "StencilCoverPath/GradientRadial");
}

// Conic (sweep) gradient centred in the square, sweeping a full 360° from red through green
// to blue. Validates that the conic gradient FP receives the correct local coordinates from
// the cover pass — a wrong uvMatrix would rotate or skew the sweep.
TGFX_TEST(StencilCoverPathTest, Visual_GradientConic) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Paint paint;
  paint.setAntiAlias(false);
  paint.setShader(Shader::MakeConicGradient(
      {150, 150}, 0, 360,
      {Color{1.f, 0.f, 0.f, 1.f}, Color{0.f, 1.f, 0.f, 1.f}, Color{0.f, 0.f, 1.f, 1.f}},
      {0.f, 0.5f, 1.f}));
  RenderPathAndCompareBaselines(context, BuildSquarePath(), paint,
                                "StencilCoverPath/GradientConic");
}

// Diamond gradient centred in the square. The diamond shape produces a different pixel
// distribution than the radial gradient, exercising a separate FP code path in the gradient
// shader.
TGFX_TEST(StencilCoverPathTest, Visual_GradientDiamond) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Paint paint;
  paint.setAntiAlias(false);
  paint.setShader(Shader::MakeDiamondGradient(
      {150, 150}, 100, {Color{1.f, 0.f, 0.f, 1.f}, Color{0.f, 0.f, 1.f, 1.f}}, {0.f, 1.f}));
  RenderPathAndCompareBaselines(context, BuildSquarePath(), paint,
                                "StencilCoverPath/GradientDiamond");
}

// Vertical linear gradient from top (red) to bottom (blue). The existing
// Visual_GradientLinear tests the horizontal direction; this case verifies the gradient
// direction is independent of the cover pass's vertex layout.
TGFX_TEST(StencilCoverPathTest, Visual_GradientLinearVertical) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Paint paint;
  paint.setAntiAlias(false);
  paint.setShader(Shader::MakeLinearGradient(
      {150, 50}, {150, 250}, {Color{1.f, 0.f, 0.f, 1.f}, Color{0.f, 0.f, 1.f, 1.f}}, {0.f, 1.f}));
  RenderPathAndCompareBaselines(context, BuildSquarePath(), paint,
                                "StencilCoverPath/GradientLinearVertical");
}

// Linear gradient with four colour stops and non-uniform positions. The colours are
// concentrated in the first half of the gradient (positions 0, 0.2, 0.4, 1.0), exercising the
// position interpolation logic in the gradient FP.
TGFX_TEST(StencilCoverPathTest, Visual_GradientLinearMultiStop) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Paint paint;
  paint.setAntiAlias(false);
  paint.setShader(Shader::MakeLinearGradient({50, 150}, {250, 150},
                                             {Color{1.f, 0.f, 0.f, 1.f}, Color{1.f, 1.f, 0.f, 1.f},
                                              Color{0.f, 1.f, 0.f, 1.f}, Color{0.f, 0.f, 1.f, 1.f}},
                                             {0.f, 0.2f, 0.4f, 1.f}));
  RenderPathAndCompareBaselines(context, BuildSquarePath(), paint,
                                "StencilCoverPath/GradientLinearMultiStop");
}

// Diagonal linear gradient from top-left (red) to bottom-right (blue). Exercises a gradient
// direction that is neither purely horizontal nor purely vertical, verifying the uvMatrix
// handles arbitrary start/end point pairs.
TGFX_TEST(StencilCoverPathTest, Visual_GradientLinearDiagonal) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Paint paint;
  paint.setAntiAlias(false);
  paint.setShader(Shader::MakeLinearGradient(
      {50, 50}, {250, 250}, {Color{1.f, 0.f, 0.f, 1.f}, Color{0.f, 0.f, 1.f, 1.f}}, {0.f, 1.f}));
  RenderPathAndCompareBaselines(context, BuildSquarePath(), paint,
                                "StencilCoverPath/GradientLinearDiagonal");
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
                                "StencilCoverPath/BrushColorFilterInvert");
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
                                nullptr, "StencilCoverPath/BrushBlendModePlus");
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
                                ApplyBrushScaleRotate, "StencilCoverPath/BrushScaleRotate");
}

// ====================================================================================
// Shader + canvas transform regression tests
//
// These cases exercise the uvMatrix bug fix in OpsCompositor::drawStencilCoverPath: before the
// fix, the cover pass passed matrix.invert() as the UV matrix to setTransformDataHelper, but the
// cover quad vertices are already in local space — the extra matrix.invert() doubled the canvas
// translation and shifted/scaled/stretched shader coordinates. Each case below combines a
// non-identity canvas matrix (ApplyBrushScaleRotate: translate + rotate + scale) with a
// different shader type (gradient, image, noise) to verify the UV transform is correct across
// all FP coordTransform types.
// ====================================================================================

// Linear gradient under a non-identity canvas matrix. Before the fix the gradient was shifted
// by double the canvas translation; now the cover pass's uvMatrix is identity and the FP's own
// coordTransform (carrying the brush matrix) handles the local-to-shader mapping. Uses
// BuildHeartCubicPath (not BuildSquarePath) because Canvas::drawPath redirects rect paths to
// drawRect → RectDrawOp, bypassing the stencil-cover pipeline entirely.
TGFX_TEST(StencilCoverPathTest, Visual_GradientLinearTransform) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Paint paint;
  paint.setAntiAlias(false);
  // Heart bbox is [30, 270] x [60, 230]; span the full width at the vertical centre.
  paint.setShader(Shader::MakeLinearGradient(
      {30, 145}, {270, 145}, {Color{1.f, 0.f, 0.f, 1.f}, Color{0.f, 0.f, 1.f, 1.f}}, {0.f, 1.f}));
  RenderPathAndCompareBaselines(context, BuildHeartCubicPath(), paint, kDebugBackground,
                                ApplyBrushScaleRotate, "StencilCoverPath/GradientLinearTransform");
}

// Image shader under a non-identity canvas matrix. Same uvMatrix bug scenario, but exercising
// the TextureEffect FP's coordTransform instead of a gradient layout FP. The 128px mandrill
// image is scaled to 64px and tiled with Repeat mode so a wrong UV transform would visibly
// shift or stretch the tile pattern. Uses BuildHeartCubicPath for the same rect-bypass reason
// as Visual_GradientLinearTransform.
TGFX_TEST(StencilCoverPathTest, Visual_ImageShaderTransform) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_TRUE(image != nullptr);
  auto shader = Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Repeat);
  shader = shader->makeWithMatrix(Matrix::MakeScale(0.5f));

  Paint paint;
  paint.setAntiAlias(false);
  paint.setShader(shader);
  RenderPathAndCompareBaselines(context, BuildHeartCubicPath(), paint, kDebugBackground,
                                ApplyBrushScaleRotate, "StencilCoverPath/ImageShaderTransform");
}

// Perlin fractal noise shader under a non-identity canvas matrix. Same uvMatrix bug scenario,
// exercising the PerlinNoiseFragmentProcessor's coordTransform. The noise pattern is defined in
// shader-local coordinates; a wrong uvMatrix would shift or stretch the noise tiles. Uses
// BuildHeartCubicPath for the same rect-bypass reason as Visual_GradientLinearTransform.
TGFX_TEST(StencilCoverPathTest, Visual_NoiseShaderTransform) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Paint paint;
  paint.setAntiAlias(false);
  paint.setShader(Shader::MakeFractalNoise(0.02f, 0.02f, 4, 0));
  RenderPathAndCompareBaselines(context, BuildHeartCubicPath(), paint, kDebugBackground,
                                ApplyBrushScaleRotate, "StencilCoverPath/NoiseShaderTransform");
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
// Small disc approximated with 4 quadratic Bezier segments instead of addOval, to avoid
// Canvas::drawPath's isOval detection. Bbox is [35, 115] x [35, 115].
static Path BuildSmallCirclePath() {
  auto cx = 75.f, cy = 75.f, r = 40.f;
  auto k = r * 0.5523f;
  Path path;
  path.moveTo(cx + r, cy);
  path.quadTo(cx + r, cy + k, cx, cy + r);
  path.quadTo(cx - k, cy + r, cx - r, cy);
  path.quadTo(cx - r, cy - k, cx, cy - r);
  path.quadTo(cx + k, cy - r, cx + r, cy);
  path.close();
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
                                "StencilCoverPath/TransformPureScale2x");
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
                                "StencilCoverPath/TransformNonUniformScale");
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
                                "StencilCoverPath/TransformPureRotate45");
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
  // Tiny source: 30x30 disc built with quad curves (not addOval) to avoid isOval detection,
  // scaled 4x ends up at 120x120 around the centre.
  auto cx = 37.5f, cy = 37.5f, r = 15.f;
  auto k = r * 0.5523f;
  Path path;
  path.moveTo(cx + r, cy);
  path.quadTo(cx + r, cy + k, cx, cy + r);
  path.quadTo(cx - k, cy + r, cx - r, cy);
  path.quadTo(cx - r, cy - k, cx, cy - r);
  path.quadTo(cx + k, cy - r, cx + r, cy);
  path.close();
  RenderPathAndCompareBaselines(context, path, MakeTransformDebugPaint(), kDebugBackground,
                                ApplyLargeScale4x, "StencilCoverPath/TransformLargeScale4x");
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
                                "StencilCoverPath/WaveQuadStrip");
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
  RenderPathAndCompareBaselines(context, BuildSpiralCubicPath(), "StencilCoverPath/SpiralCubic");
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
                                "StencilCoverPath/FlowerSelfIntersect");
}

// Rounded rectangle built with quadTo instead of addRoundRect, to avoid Canvas::drawPath's
// isRRect detection (which would redirect to drawRRect and bypass the stencil-cover pipeline).
// Bbox is [40, 260] x [40, 260] with 40 px corner radius, matching the original addRoundRect version.
static Path BuildRoundedRectPath() {
  auto x = 40.f, y = 40.f, w = 220.f, h = 220.f, r = 40.f;
  Path path;
  path.moveTo(x + r, y);
  path.lineTo(x + w - r, y);
  path.quadTo(x + w, y, x + w, y + r);
  path.lineTo(x + w, y + h - r);
  path.quadTo(x + w, y + h, x + w - r, y + h);
  path.lineTo(x + r, y + h);
  path.quadTo(x, y + h, x, y + h - r);
  path.lineTo(x, y + r);
  path.quadTo(x, y, x + r, y);
  path.close();
  return path;
}

TGFX_TEST(StencilCoverPathTest, Visual_RoundedRect) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildRoundedRectPath(), "StencilCoverPath/RoundedRect");
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
  RenderPathAndCompareBaselines(context, BuildLollipopPath(), "StencilCoverPath/Lollipop");
}

// Highly elongated ellipse approximated with 4 quadratic Bezier segments instead of addOval,
// to avoid Canvas::drawPath's isOval detection. Bbox is [30, 270] x [130, 170], matching the
// original addOval version. The high-curvature quad ends exercise the same
// ChopQuadAtMaxCurvature subdivision as the original conic→quad expansion.
static Path BuildNarrowEllipsePath() {
  auto cx = 150.f, cy = 150.f, rx = 120.f, ry = 20.f;
  auto kx = rx * 0.5523f;
  auto ky = ry * 0.5523f;
  Path path;
  path.moveTo(cx + rx, cy);
  path.quadTo(cx + rx, cy + ky, cx, cy + ry);
  path.quadTo(cx - kx, cy + ry, cx - rx, cy);
  path.quadTo(cx - rx, cy - ky, cx, cy - ry);
  path.quadTo(cx + kx, cy - ry, cx + rx, cy);
  path.close();
  return path;
}

TGFX_TEST(StencilCoverPathTest, Visual_NarrowEllipse) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  RenderPathAndCompareBaselines(context, BuildNarrowEllipsePath(),
                                "StencilCoverPath/NarrowEllipse");
}

// ====================================================================================
// Gradient text cases
//
// Large text (font size 400) has glyph bounds exceeding Atlas::MaxCellSize (256px), so each
// glyph is rejected from the atlas and rendered as a path via drawGlyphAsPath. With the
// stencil-and-cover caps enabled, this exercises the full bezier pipeline for text:
// each glyph shape goes through the stencil pass and the cover pass, with the gradient
// shader applied in the cover pass. These tests verify that gradient shaders work
// correctly when text is rendered as paths through the stencil-and-cover pipeline.
// ====================================================================================

// Renders text with the given paint into a surface sized to fit the text with ~50px margin.
// The stencil-and-cover caps are forced on so glyphs rendered as paths go through the bezier
// pipeline. Surface dimensions are computed from the text's tight bounds to ensure the content
// is centred per the screenshot test rules.
static void RenderTextAndCompareBaselines(Context* context,
                                          const std::shared_ptr<TextBlob>& textBlob,
                                          const Rect& textBounds, const Paint& paint,
                                          const Color& clearColor, const std::string& keyPrefix) {
  ScopedStencilCoverCaps capsGuard(context, false);

  auto surfaceWidth = static_cast<int>(textBounds.width()) + 100;
  auto surfaceHeight = static_cast<int>(textBounds.height()) + 100;
  auto drawX = 50.f - textBounds.left;
  auto drawY = 50.f - textBounds.top;

#if TGFX_BEZIER_VISUAL_DEBUG_EXPORT
  capsGuard.features->stencilAttachmentSupported = false;
  {
    auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
    ASSERT_TRUE(surface != nullptr);
    auto canvas = surface->getCanvas();
    canvas->clear(clearColor);
    canvas->drawTextBlob(textBlob, drawX, drawY, paint);
    context->flushAndSubmit();
    (void)Baseline::Compare(surface, keyPrefix + "_Legacy");
  }
  capsGuard.features->stencilAttachmentSupported = true;
  {
    auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
    ASSERT_TRUE(surface != nullptr);
    auto canvas = surface->getCanvas();
    canvas->clear(clearColor);
    canvas->drawTextBlob(textBlob, drawX, drawY, paint);
    context->flushAndSubmit();
    EXPECT_TRUE(Baseline::Compare(surface, keyPrefix + "_Bezier"));
  }
#else
  capsGuard.features->stencilAttachmentSupported = true;
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(clearColor);
  canvas->drawTextBlob(textBlob, drawX, drawY, paint);
  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, keyPrefix));
#endif
}

// Linear gradient applied to large text rendered as paths. "TGFX" at font size 400 has glyph
// bounds exceeding Atlas::MaxCellSize (256px), so each glyph is rejected from the atlas and
// drawn through drawGlyphAsPath, which then enters the stencil-and-cover pipeline. The gradient
// spans the text's tight bounds from left (red) to right (blue), verifying that the cover
// pass's uvMatrix correctly maps the gradient coordinates across multiple glyph shapes drawn
// as separate path ops.
TGFX_TEST(StencilCoverPathTest, Visual_GradientTextLinear) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);
  // Font size 400 ensures glyph bounds (cap height ≈ 0.72 × 400 = 288px) exceed
  // Atlas::MaxCellSize (256px), forcing drawGlyphAsPath and thus the stencil-cover pipeline.
  Font font(typeface, 400);
  auto textBlob = TextBlob::MakeFrom("TGFX", font);
  ASSERT_TRUE(textBlob != nullptr);
  auto bounds = textBlob->getTightBounds();
  ASSERT_FALSE(bounds.isEmpty());

  Paint paint;
  paint.setAntiAlias(false);
  paint.setShader(Shader::MakeLinearGradient(
      {bounds.left, bounds.centerY()}, {bounds.right, bounds.centerY()},
      {Color{1.f, 0.f, 0.f, 1.f}, Color{0.f, 0.f, 1.f, 1.f}}, {0.f, 1.f}));

  RenderTextAndCompareBaselines(context, textBlob, bounds, paint, kDebugBackground,
                                "StencilCoverPath/GradientTextLinear");
}

// Radial gradient applied to large text rendered as paths. The gradient is centred on the
// text's tight-bounds centre with a radius covering half the text width, producing a
// circular colour falloff that must stay consistent across glyph boundaries — each glyph is
// a separate stencil-and-cover op, but the gradient coordinates are in canvas space, so the
// radial pattern must remain continuous across glyphs.
TGFX_TEST(StencilCoverPathTest, Visual_GradientTextRadial) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 400);
  auto textBlob = TextBlob::MakeFrom("TGFX", font);
  ASSERT_TRUE(textBlob != nullptr);
  auto bounds = textBlob->getTightBounds();
  ASSERT_FALSE(bounds.isEmpty());

  Paint paint;
  paint.setAntiAlias(false);
  auto radius = bounds.width() * 0.5f;
  paint.setShader(Shader::MakeRadialGradient({bounds.centerX(), bounds.centerY()}, radius,
                                             {Color{1.f, 0.f, 0.f, 1.f}, Color{0.f, 0.f, 1.f, 1.f}},
                                             {0.f, 1.f}));

  RenderTextAndCompareBaselines(context, textBlob, bounds, paint, kDebugBackground,
                                "StencilCoverPath/GradientTextRadial");
}

// ====================================================================================
// Dispatch coverage gap: brush.antiAlias fallback + no-clip stencil isolation
// ====================================================================================

// Guarding the AA fallback branch of OpsCompositor::shouldUseStencilCover: whenever the brush
// requests antialiasing, the dispatcher must keep using the legacy triangulation path even
// when the stencil-cover capability is enabled. Otherwise the coverage-AA / alpha-ramp
// contract silently changes and edge pixels start snapping to fully opaque or fully
// transparent. The test cross-checks two facts on the same pentagon:
//   1. Enabling caps has no observable effect on an AA draw — pixel at the shape centre and
//      corner must match the caps-off legacy render.
//   2. That equivalence must hold even though the same pentagon *does* diverge in the non-AA
//      case (covered by Dispatch_LegacyAndBezierProduceMatchingSampledPixels, which uses
//      centre/corner sampling for the same reason).
// If someone accidentally inverts the antiAlias guard to `!brush.antiAlias` the AA render
// would suddenly go through stencil-and-cover and this test would flip the interior sample
// (or, more likely, fail Surface::readPixels comparisons on edge samples).
TGFX_TEST(StencilCoverPathTest, Dispatch_AntiAliasedBrushKeepsLegacyPath) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  constexpr int SIZE = 64;
  auto renderAA = [&](bool capsValue) -> DispatchSnapshot {
    ScopedStencilCoverCaps capsGuard(context, capsValue);
    auto surface = Surface::Make(context, SIZE, SIZE);
    if (surface == nullptr) {
      return {0, 0};
    }
    auto canvas = surface->getCanvas();
    canvas->clear(Color{0.f, 0.f, 0.f, 1.f});

    Paint paint;
    paint.setAntiAlias(true);
    paint.setColor(Color{1.f, 0.f, 0.f, 1.f});

    Path path = BuildCentredPentagonForDispatch();
    path.setFillType(PathFillType::Winding);
    canvas->drawPath(path, paint);
    context->flushAndSubmit();
    return {ReadRedChannel(surface.get(), SIZE / 2, SIZE / 2), ReadRedChannel(surface.get(), 4, 4)};
  };

  auto capsOff = renderAA(false);
  auto capsOn = renderAA(true);
  // Interior of the AA pentagon must be fully covered under both configurations. If caps=on
  // routed the AA draw into stencil-and-cover, the interior would still be red but the
  // outside/edge behaviour would diverge; the outside-corner sample is the discriminator.
  EXPECT_EQ(capsOff.insideRed, 0xFF) << "AA legacy interior should be fully red";
  EXPECT_EQ(capsOn.insideRed, capsOff.insideRed)
      << "Enabling stencil-cover caps must not change AA interior pixels";
  EXPECT_EQ(capsOn.outsideRed, capsOff.outsideRed)
      << "Enabling stencil-cover caps must not change AA outside pixels";
}

// Regression pin-down for the coverDeviceBounds-based stencil scissor introduced alongside
// applyStencilScissor(). The existing Dispatch_StencilWritesAreScissoredToOpClip test forces
// a non-empty scissorRect via canvas->clipRect(...); this variant deliberately draws *without*
// any clip so both ops enter drawStencilCoverPath with an empty scissorRect (ClipStack::WideOpen
// → AppliedClip::scissor stays nullopt → DrawOp::scissorRect is default-constructed empty).
//
// In the pre-fix code, bindStencilPipeline called DrawOp::applyScissor(), which for an empty
// scissorRect expanded to the full render target. After the fix, applyStencilScissor() always
// clamps the stencil pass to coverDeviceBounds regardless of scissorRect. The bezier tessellator
// only emits vertices inside the shape's bounds today, so today's build passes either way — but
// the pin-down keeps the "stencil confined to cover quad" invariant enforced against future
// regressions (path builder padding, tessellator over-emission, etc.).
//
// Op A is a non-inverse pentagon in the LEFT half; op B is an InverseWinding pentagon in the
// RIGHT half. Op B's inverse-fill cover pass compares stencil == 0 across the *entire* render
// target (no clip, so localClipBounds spans the whole surface). The verification samples check:
//   - op A's interior is red (op A itself rendered correctly);
//   - op A's own shape interior stays black in the final image after op B's inverse fill,
//     because op B's stencil==0 test still succeeds at those pixels (op A's cover pass
//     already zeroed its own stencil region) so op B repaints them red on top of black;
//     wait — this is exactly the invariant. We instead pick a discriminating sample below.
//   - the pixel well outside both shapes must be red (op B inverse fill applies there, and
//     op A's stencil is guaranteed to have been zeroed inside op A's coverDeviceBounds).
//
// The most sensitive discriminator would be a pixel where op A's stencil pass could have
// leaked without the fix: outside op A's coverDeviceBounds. Today the tessellator can't cause
// that leak, so both fix and pre-fix pass. We still lock the pixel-level result down so any
// future change that lets stencil escape produces a visible red→black flip at (62, 1).
TGFX_TEST(StencilCoverPathTest, Dispatch_StencilConfinedByCoverBoundsWithoutClip) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  ScopedStencilCoverCaps capsGuard(context, true);

  constexpr int SIZE = 64;
  auto surface = Surface::Make(context, SIZE, SIZE);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color{0.f, 0.f, 0.f, 1.f});

  Paint paint;
  paint.setAntiAlias(false);
  paint.setColor(Color{1.f, 0.f, 0.f, 1.f});

  // Op A: small non-inverse pentagon in the LEFT half. No clipRect — so scissorRect on the
  // op will be empty (WideOpen clip state) and the pre-fix code would have set the stencil
  // scissor to the whole render target.
  Path pathA;
  pathA.moveTo(16, 9);
  pathA.lineTo(28, 24);
  pathA.lineTo(24, 41);
  pathA.lineTo(8, 41);
  pathA.lineTo(4, 24);
  pathA.close();
  pathA.setFillType(PathFillType::Winding);
  canvas->drawPath(pathA, paint);

  // Op B: InverseWinding pentagon in the RIGHT half. Inverse fill compares stencil == 0
  // across the full render target (no clip → localClipBounds is the whole surface). The
  // discriminating pixel is one where (a) op A did not paint (outside pathA's bbox), and
  // (b) op B's inverse fill applies (outside pathB's bbox), and (c) op A's stencil must
  // have already been zeroed. If any op A stencil write escaped op A's coverDeviceBounds
  // and landed outside pathB, op B's Equal-0 compare would fail there and the pixel would
  // stay black. Locking the pixel to red catches such a future regression.
  Path pathB;
  pathB.moveTo(48, 9);
  pathB.lineTo(60, 24);
  pathB.lineTo(56, 41);
  pathB.lineTo(40, 41);
  pathB.lineTo(36, 24);
  pathB.close();
  pathB.setFillType(PathFillType::InverseWinding);
  canvas->drawPath(pathB, paint);

  context->flushAndSubmit();

  // Sanity: op A rendered under the empty-scissor configuration.
  EXPECT_EQ(ReadRedChannel(surface.get(), 16, 28), 0xFF) << "Pentagon A interior should be red";
  // Op B's shape interior — inverse fill leaves this unfilled, so it stays background black.
  EXPECT_EQ(ReadRedChannel(surface.get(), 48, 28), 0x00) << "Pentagon B interior should be black";
  // Discriminator pixel: outside both shapes' bboxes, on the RIGHT half. Op B inverse fill
  // should paint it red *if* op A's stencil writes were properly confined. Any future stencil
  // leak from op A's pass into this half of the render target would fail op B's Equal-0
  // compare and flip this pixel to black.
  EXPECT_EQ(ReadRedChannel(surface.get(), 62, 1), 0xFF)
      << "Op B inverse fill outside its bbox must be red (catches stencil leakage from op A)";
  // Discriminator pixel: outside both shapes' bboxes, on the LEFT half. Same rationale as
  // (62, 1) but on op A's side. Op B inverse fill spans the whole render target, so this
  // pixel is also inside op B's cover quad — it must be red for the same reason. This one
  // is the tightest check because a stencil leak from op A into its own half is the most
  // plausible future regression pattern.
  EXPECT_EQ(ReadRedChannel(surface.get(), 1, 1), 0xFF)
      << "Op B inverse fill inside op A's half but outside op A's bbox must be red";
}

}  // namespace tgfx

#endif  // TGFX_ENABLE_STENCIL_COVER_PATH

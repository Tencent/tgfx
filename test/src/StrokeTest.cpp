#include <vector>
#include "core/ShapeBezierTriangulator.h"
#include "core/utils/BlockAllocator.h"
#include "core/utils/PathUtils.h"
#include "core/utils/StrokeUtils.h"
#include "gpu/AAType.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProxyProvider.h"
#include "gpu/processors/HairlineLineGeometryProcessor.h"
#include "gpu/processors/HairlineQuadGeometryProcessor.h"
#include "gtest/gtest.h"
#include "tgfx/core/BytesKey.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/RenderFlags.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/ShapeStyle.h"
#include "tgfx/svg/SVGPathParser.h"
#include "utils/TestUtils.h"
#include "utils/TextShaper.h"

namespace tgfx {
TGFX_TEST(StrokeTest, DrawPathByHairlinePaint) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  Paint paint;
  paint.setAntiAlias(true);
  paint.setColor(Color::FromRGBA(255, 0, 0, 255));
  paint.setStyle(PaintStyle::Stroke);
  paint.setStroke(Stroke(0.0f));

  EXPECT_TRUE(IsHairlineStroke(*paint.getStroke()));

  auto path = Path();
  path.addRoundRect(Rect::MakeXYWH(-12.5f, -12.5f, 25.f, 25.f), 5, 5);
  canvas->translate(100, 100);
  canvas->drawPath(path, paint);
  canvas->scale(2.f, 2.f);
  canvas->drawPath(path, paint);
  canvas->scale(2.f, 2.f);
  canvas->drawPath(path, paint);
  canvas->scale(1.9f, 1.9f);
  canvas->drawPath(path, paint);

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/DrawPathByHairlinePaint"));
}

TGFX_TEST(StrokeTest, DrawShapeByHairlinePaint) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  Paint paint;
  paint.setAntiAlias(true);
  paint.setColor(Color::FromRGBA(255, 0, 0, 255));
  paint.setStyle(PaintStyle::Stroke);
  paint.setStroke(Stroke(0.0f));

  auto path = Path();
  path.addRoundRect(Rect::MakeXYWH(-12.5f, -12.5f, 25.f, 25.f), 5, 5);
  auto shape = Shape::MakeFrom(path);

  canvas->translate(100, 100);
  canvas->drawShape(shape, paint);
  canvas->scale(2.f, 2.f);
  canvas->drawShape(shape, paint);

  canvas->scale(3.f, 3.f);
  Stroke thickStroke(5.f);
  auto thickStrokeShape = Shape::ApplyStroke(shape, &thickStroke);
  canvas->drawShape(thickStrokeShape, paint);

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/DrawShapeByHairlinePaint"));
}

TGFX_TEST(StrokeTest, HairlineLayer) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  auto canvas = surface->getCanvas();
  canvas->clear();

  Path path1 = {};
  path1.addRect(-10, -10, 10, 10);
  auto shapeLayer1 = ShapeLayer::Make();
  shapeLayer1->setPath(path1);
  auto strokeStyle = ShapeStyle::Make(Color::Red());
  shapeLayer1->setLineWidth(0.0f);
  shapeLayer1->setStrokeStyle(strokeStyle);
  shapeLayer1->setLineDashAdaptive(true);
  shapeLayer1->setLineDashPattern({2.f, 2.f});
  shapeLayer1->setLineDashPhase(2);
  auto matrix = Matrix::MakeTrans(100, 100);
  matrix.preScale(5, 5);
  shapeLayer1->setMatrix(matrix);

  Path path2 = {};
  path2.addRect(-80, -80, 80, 80);
  auto shapeLayer2 = ShapeLayer::Make();
  auto shape = Shape::MakeFrom(path2);
  shape = Shape::ApplyEffect(shape, PathEffect::MakeCorner(20));
  shapeLayer2->setShape(shape);
  shapeLayer2->setLineWidth(0.0f);
  shapeLayer2->setStrokeStyle(strokeStyle);
  shapeLayer2->setMatrix(Matrix::MakeTrans(100, 100));

  DisplayList displayList;
  displayList.root()->addChild(shapeLayer1);
  displayList.root()->addChild(shapeLayer2);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/HairlineLayer"));
}

TGFX_TEST(StrokeTest, ZoomUpTinyStrokeShape) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 200);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  canvas->clear(Color::Black());
  Paint paint;
  paint.setColor(Color::FromRGBA(255, 255, 0));
  {
    Path path;
    path.addRoundRect(Rect::MakeXYWH(-2, -2, 4, 4), 2, 2);
    auto shape = Shape::MakeFrom(path);
    auto stroke = Stroke(1.f);
    shape = Shape::ApplyStroke(shape, &stroke);
    shape = Shape::ApplyMatrix(shape, Matrix::MakeScale(20, 20));

    canvas->translate(100, 100);
    canvas->drawShape(shape, paint);
  }
  {
    Path path;
    path.addRoundRect(Rect::MakeXYWH(-2, -2, 4, 4), 2, 2);
    auto shape = Shape::MakeFrom(path);
    shape = Shape::ApplyMatrix(shape, Matrix::MakeScale(20, 20));
    auto stroke = Stroke(20.f);
    shape = Shape::ApplyStroke(shape, &stroke);

    canvas->translate(200, 0);
    canvas->drawShape(shape, paint);
  }

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/ZoomUpTinyStrokeShape"));
}

TGFX_TEST(StrokeTest, ExtremelyThinStrokePath) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 400);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::Black());

  auto path = SVGPathParser::FromSVGString(
      "M1690.5,699.5C1690.5,1113.7136,1164.2136,1449.5,750,1449.5C335.78641,1449.5,0,1113.7136,0,"
      "699.5C0,285.28641,335.78641,0,750,0C1164.2136,0,1690.5,285.28641,1690."
      "5,699.5Z");
  Paint paint;
  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(1.f);
  paint.setColor(Color::FromRGBA(255, 255, 0, 255));

  canvas->scale(0.2f, 0.2f);
  canvas->drawPath(*path, paint);
  canvas->scale(0.5f, 0.5f);
  canvas->drawPath(*path, paint);
  canvas->scale(0.5f, 0.5f);
  canvas->drawPath(*path, paint);

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/ExtremelyThinStrokePath"));
}

TGFX_TEST(StrokeTest, ExtremelyThinStrokeLayer) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  auto canvas = surface->getCanvas();
  canvas->clear();

  Path path2 = {};
  path2.addRect(-200, -200, 200, 200);
  auto shapeLayer = ShapeLayer::Make();
  auto shape = Shape::MakeFrom(path2);
  shape = Shape::ApplyEffect(shape, PathEffect::MakeCorner(50));
  shapeLayer->setShape(shape);
  shapeLayer->setLineWidth(1.0f);
  auto strokeStyle = ShapeStyle::Make(Color::Red());
  shapeLayer->setStrokeStyle(strokeStyle);
  auto matrix = Matrix::MakeTrans(100, 100);
  matrix.preScale(0.4f, 0.4f);
  shapeLayer->setMatrix(matrix);

  DisplayList displayList;
  displayList.root()->addChild(shapeLayer);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/ExtremelyThinStrokeLayer"));
}

TGFX_TEST(StrokeTest, HairlineUniqueKey) {
  Stroke hairlineStroke1(0.f);
  hairlineStroke1.cap = LineCap::Round;
  hairlineStroke1.join = LineJoin::Miter;

  Stroke hairlineStroke2(0.f);
  hairlineStroke2.cap = LineCap::Butt;
  hairlineStroke2.join = LineJoin::Round;

  auto path = Path();
  path.addRoundRect(Rect::MakeXYWH(-12.5f, -12.5f, 25.f, 25.f), 5, 5);

  auto shape = Shape::MakeFrom(path);
  auto strokeShape1 = Shape::ApplyStroke(shape, &hairlineStroke1);
  auto strokeShape2 = Shape::ApplyStroke(shape, &hairlineStroke2);
  EXPECT_EQ(strokeShape1->getUniqueKey(), strokeShape2->getUniqueKey());

  hairlineStroke1.width = 1.f;
  hairlineStroke2.width = 1.f;

  auto normalStrokeShape1 = Shape::ApplyStroke(shape, &hairlineStroke1);
  auto normalStrokeShape2 = Shape::ApplyStroke(shape, &hairlineStroke2);
  EXPECT_NE(normalStrokeShape1->getUniqueKey(), normalStrokeShape2->getUniqueKey());
}

TGFX_TEST(StrokeTest, LineRenderAsHairline) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  auto canvas = surface->getCanvas();
  canvas->clear();

  tgfx::Paint paint1;
  paint1.setColor(tgfx::Color::White());
  paint1.setStyle(tgfx::PaintStyle::Stroke);
  paint1.setStrokeWidth(0.0f);

  canvas->drawLine(50, 20, 150, 20, paint1);   // horizontal line
  canvas->drawLine(50, 40, 150, 140, paint1);  // 45 degree line
  canvas->drawLine(50, 60, 50, 160, paint1);   // vertical line

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/LineRenderAsHairline"));
}

TGFX_TEST(StrokeTest, SquareCapDashStrokeAsSolidStroke) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  auto canvas = surface->getCanvas();
  canvas->clear();

  Path path1 = {};
  path1.addRect(-70, -70, 70, 70);
  auto shapeLayer1 = ShapeLayer::Make();
  shapeLayer1->setPath(path1);
  auto strokeStyle = ShapeStyle::Make(Color::Red());
  shapeLayer1->setLineWidth(2.0f);
  shapeLayer1->setStrokeStyle(strokeStyle);
  shapeLayer1->setLineDashAdaptive(true);
  shapeLayer1->setLineDashPattern({2.f, 2.f});
  shapeLayer1->setLineCap(LineCap::Square);
  auto matrix = Matrix::MakeTrans(100, 100);
  shapeLayer1->setMatrix(matrix);

  auto simplifiedDashes1 =
      SimplifyLineDashPattern(shapeLayer1->_lineDashPattern, shapeLayer1->stroke);
  EXPECT_EQ(simplifiedDashes1.size(), 0u);

  Path path2 = {};
  path2.addRect(-90, -90, 90, 90);
  auto shapeLayer2 = ShapeLayer::Make();
  auto shape = Shape::MakeFrom(path2);
  shape = Shape::ApplyEffect(shape, PathEffect::MakeCorner(20));
  shapeLayer2->setShape(shape);
  shapeLayer2->setLineWidth(2.0f);
  shapeLayer2->setStrokeStyle(strokeStyle);
  shapeLayer2->setLineDashAdaptive(true);
  shapeLayer2->setLineDashPattern({2.f, 2.f, 2.f, 4.f});
  shapeLayer2->setLineCap(LineCap::Square);
  shapeLayer2->setMatrix(Matrix::MakeTrans(100, 100));

  auto simplifiedDashes2 =
      SimplifyLineDashPattern(shapeLayer2->_lineDashPattern, shapeLayer2->stroke);
  EXPECT_EQ(simplifiedDashes2.size(), 2u);
  EXPECT_EQ(simplifiedDashes2[0], 6.f);
  EXPECT_EQ(simplifiedDashes2[1], 4.f);

  DisplayList displayList;
  displayList.root()->addChild(shapeLayer1);
  displayList.root()->addChild(shapeLayer2);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/DashStrokeAsSolidStroke"));
}

TGFX_TEST(StrokeTest, HairlineWithDropShadow) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 400);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  Paint paint;
  paint.setColor(Color::Red());
  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(0.0f);

  // Add drop shadow effect
  Color shadowColor = {0.2f, 0.2f, 1.0f, 1.0f};
  auto shadowEffect = ImageFilter::DropShadow(0.0f, 0.0f, 2.0f, 3.0f, shadowColor);
  paint.setImageFilter(shadowEffect);

  // Draw horizontal and vertical hairlines with shadow
  canvas->drawLine(50, 100, 350, 100, paint);  // horizontal line
  canvas->drawLine(200, 50, 200, 350, paint);  // vertical line

  // Draw diagonal hairline for comparison
  canvas->drawLine(50, 50, 350, 350, paint);  // diagonal line

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/HairlineWithDropShadow"));
}

static void DrawCubicPath(tgfx::Canvas* canvas, const Point points[4]) {
  Paint cubicPaint;
  cubicPaint.setColor(Color::FromRGBA(255, 255, 0, 255));
  cubicPaint.setStyle(PaintStyle::Stroke);
  cubicPaint.setStrokeWidth(2.0f);

  tgfx::Path path;
  path.moveTo(points[0]);
  path.cubicTo(points[1], points[2], points[3]);
  canvas->drawPath(path, cubicPaint);
}

static void DrawQuadPath(tgfx::Canvas* canvas, const std::vector<Point>& quadPoints) {
  Paint quadPaint;
  quadPaint.setColor(Color::FromRGBA(255, 0, 0, 255));
  quadPaint.setStyle(PaintStyle::Stroke);
  quadPaint.setStrokeWidth(2.0f);

  Path path;
  path.moveTo(quadPoints[0]);
  for (size_t i = 0; i < quadPoints.size() / 3; ++i) {
    path.quadTo(quadPoints[(3 * i) + 1], quadPoints[(3 * i) + 2]);
  }
  canvas->drawPath(path, quadPaint);
}

TGFX_TEST(StrokeTest, ConvertCubicToQuads) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 600, 600);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::Black());

  {
    Point cubicPoints[4] = {Point(25, 50), Point(25, -17), Point(175, -2), Point(175, 50)};
    float tolerance = 1.f;
    auto quads = PathUtils::ConvertCubicToQuads(cubicPoints, tolerance);
    DrawCubicPath(canvas, cubicPoints);
    canvas->save();
    canvas->translate(0, 100.f);
    DrawQuadPath(canvas, quads);
    canvas->restore();
  }

  canvas->translate(200.f, 0.f);
  {
    //alpha type cubic
    Point cubicPoints[4] = {Point(25, 50), Point(292, 90.5), Point(-68.5, 117.5), Point(175, 50)};
    float tolerance = 1.f;
    auto quads = PathUtils::ConvertCubicToQuads(cubicPoints, tolerance);
    DrawCubicPath(canvas, cubicPoints);
    canvas->save();
    canvas->translate(0, 100.f);
    DrawQuadPath(canvas, quads);
    canvas->restore();
  }

  canvas->translate(200.f, 0.f);
  {
    // z type cubic
    Point cubicPoints[4] = {Point(25, 50), Point(310, 6), Point(-96, 111), Point(175, 50)};
    float tolerance = 1.f;
    auto quads = PathUtils::ConvertCubicToQuads(cubicPoints, tolerance);
    DrawCubicPath(canvas, cubicPoints);
    canvas->save();
    canvas->translate(0, 100.f);
    DrawQuadPath(canvas, quads);
    canvas->restore();
  }

  canvas->translate(-400.f, 200.f);
  {
    Point cubicPoints[4] = {Point(25, 50), Point(126.5, 111), Point(64, -14), Point(175, 50)};
    float tolerance = 1.f;
    auto quads = PathUtils::ConvertCubicToQuads(cubicPoints, tolerance);
    DrawCubicPath(canvas, cubicPoints);
    canvas->save();
    canvas->translate(0, 100.f);
    DrawQuadPath(canvas, quads);
    canvas->restore();
  }

  canvas->translate(200.f, 0.f);
  {
    // line type cubic
    Point cubicPoints[4] = {Point(25, 50), Point(175, 50), Point(25, 50), Point(175, 50)};
    float tolerance = 1.f;
    auto quads = PathUtils::ConvertCubicToQuads(cubicPoints, tolerance);
    DrawCubicPath(canvas, cubicPoints);
    canvas->save();
    canvas->translate(0, 100.f);
    DrawQuadPath(canvas, quads);
    canvas->restore();
  }

  canvas->translate(200.f, 0.f);
  {
    Point cubicPoints[4] = {Point(25, 50), Point(94.5, 7), Point(147.5, -1.5), Point(175, 50)};
    float tolerance = 1.f;
    auto quads = PathUtils::ConvertCubicToQuads(cubicPoints, tolerance);
    DrawCubicPath(canvas, cubicPoints);
    canvas->save();
    canvas->translate(0, 100.f);
    DrawQuadPath(canvas, quads);
    canvas->restore();
  }

  canvas->translate(-400.f, 200.f);
  {
    Point cubicPoints[4] = {Point(25, 50), Point(-69, 16.5), Point(269, 23), Point(175, 50)};
    float tolerance = 1.f;
    auto quads = PathUtils::ConvertCubicToQuads(cubicPoints, tolerance);
    DrawCubicPath(canvas, cubicPoints);
    canvas->save();
    canvas->translate(0, 100.f);
    DrawQuadPath(canvas, quads);
    canvas->restore();
  }

  canvas->translate(200.f, 0.f);
  {
    Point cubicPoints[4] = {Point(25, 50), Point(52.5, -33.5), Point(94, 65), Point(175, 50)};
    float tolerance = 1.f;
    auto quads = PathUtils::ConvertCubicToQuads(cubicPoints, tolerance);
    DrawCubicPath(canvas, cubicPoints);
    canvas->save();
    canvas->translate(0, 100.f);
    DrawQuadPath(canvas, quads);
    canvas->restore();
  }

  canvas->translate(200.f, 0.f);
  {
    Point cubicPoints[4] = {Point(25, 50), Point(157, 111), Point(41, 110), Point(175, 50)};
    float tolerance = 1.f;
    auto quads = PathUtils::ConvertCubicToQuads(cubicPoints, tolerance);
    DrawCubicPath(canvas, cubicPoints);
    canvas->save();
    canvas->translate(0, 100.f);
    DrawQuadPath(canvas, quads);
    canvas->restore();
  }

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/ConvertCubicToQuads"));
}

TGFX_TEST(StrokeTest, HairlineBasicRendering) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 500);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  Paint paint;
  paint.setColor(Color::Red());
  paint.setStyle(PaintStyle::Stroke);

  Path path;
  path.moveTo(100, 100);
  path.quadTo(200, 50, 300, 100);
  path.lineTo(100, 100);

  paint.setAntiAlias(true);
  paint.setStrokeWidth(0.0f);
  canvas->drawPath(path, paint);

  canvas->translate(0, 100);
  paint.setAntiAlias(true);
  paint.setStrokeWidth(0.5f);
  canvas->drawPath(path, paint);

  canvas->translate(0, 100);
  paint.setAntiAlias(false);
  paint.setStrokeWidth(0.0f);
  canvas->drawPath(path, paint);

  canvas->translate(0, 100);
  paint.setAntiAlias(false);
  paint.setStrokeWidth(0.5f);
  canvas->drawPath(path, paint);

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/HairlineBasicRendering"));
}

TGFX_TEST(StrokeTest, HairlineBufferCacheReuse) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Path path;
  path.addRoundRect(Rect::MakeXYWH(-12.5f, -12.5f, 25.f, 25.f), 5, 5);
  auto shape = Shape::MakeFrom(path);

  auto proxyProvider = context->proxyProvider();
  auto areBuffersEqual = [](const std::shared_ptr<GPUHairlineProxy>& proxy1,
                            const std::shared_ptr<GPUHairlineProxy>& proxy2) {
    return proxy1->getLineVertexBufferProxy().get() == proxy2->getLineVertexBufferProxy().get() &&
           proxy1->getLineIndexBufferProxy().get() == proxy2->getLineIndexBufferProxy().get() &&
           proxy1->getQuadVertexBufferProxy().get() == proxy2->getQuadVertexBufferProxy().get() &&
           proxy1->getQuadIndexBufferProxy().get() == proxy2->getQuadIndexBufferProxy().get();
  };

  // Test 1: Verify proxyMap size changes
  {
    auto& proxyMap = proxyProvider->proxyMap;
    proxyProvider->purgeExpiredProxies();
    size_t initialSize = proxyMap.size();
    {
      auto proxy1 = proxyProvider->createGPUHairlineProxy(shape, true, 0);
      ASSERT_TRUE(proxy1 != nullptr);

      size_t sizeAfterFirst = proxyMap.size();
      EXPECT_GT(sizeAfterFirst, initialSize);

      auto proxy2 = proxyProvider->createGPUHairlineProxy(shape, true, 0);
      ASSERT_TRUE(proxy2 != nullptr);

      size_t sizeAfterSecond = proxyMap.size();
      EXPECT_EQ(sizeAfterSecond, sizeAfterFirst);
    }

    proxyProvider->purgeExpiredProxies();
    size_t sizeAfterPurge = proxyMap.size();
    EXPECT_LE(sizeAfterPurge, initialSize + 4);
  }

  // Test 2: Same shape and hasCap should reuse buffer proxies
  {
    auto proxy1 = proxyProvider->createGPUHairlineProxy(shape, true, 0);
    auto proxy2 = proxyProvider->createGPUHairlineProxy(shape, true, 0);
    ASSERT_TRUE(proxy1 != nullptr);
    ASSERT_TRUE(proxy2 != nullptr);
    EXPECT_TRUE(areBuffersEqual(proxy1, proxy2));
  }

  // Test 3: Different hasCap should create new buffer proxies
  {
    auto proxy1 = proxyProvider->createGPUHairlineProxy(shape, true, 0);
    auto proxy2 = proxyProvider->createGPUHairlineProxy(shape, false, 0);
    ASSERT_TRUE(proxy1 != nullptr);
    ASSERT_TRUE(proxy2 != nullptr);
    EXPECT_FALSE(areBuffersEqual(proxy1, proxy2));
  }

  // Test 4: hasCap = false should share the same cache key (no cap flag in UniqueKey)
  {
    auto proxy1 = proxyProvider->createGPUHairlineProxy(shape, false, 0);
    auto proxy2 = proxyProvider->createGPUHairlineProxy(shape, false, 0);
    ASSERT_TRUE(proxy1 != nullptr);
    ASSERT_TRUE(proxy2 != nullptr);
    EXPECT_TRUE(areBuffersEqual(proxy1, proxy2));
  }

  // Test 5: Different shapes should not reuse buffer proxies
  {
    Path path2;
    path2.addOval(Rect::MakeXYWH(0, 0, 50, 50));
    auto shape2 = Shape::MakeFrom(path2);
    auto proxy1 = proxyProvider->createGPUHairlineProxy(shape, true, 0);
    auto proxy2 = proxyProvider->createGPUHairlineProxy(shape2, true, 0);
    ASSERT_TRUE(proxy1 != nullptr);
    ASSERT_TRUE(proxy2 != nullptr);
    EXPECT_FALSE(areBuffersEqual(proxy1, proxy2));
  }

  // Test 6: DisableCache flag behavior
  {
    auto proxy1 = proxyProvider->createGPUHairlineProxy(shape, true, RenderFlags::DisableCache);
    auto proxy2 = proxyProvider->createGPUHairlineProxy(shape, true, RenderFlags::DisableCache);
    ASSERT_TRUE(proxy1 != nullptr);
    ASSERT_TRUE(proxy2 != nullptr);
  }
}

TGFX_TEST(StrokeTest, HairlineShaderProgramCacheReuse) {
  ContextScope scope;
  auto context = scope.getContext();
  auto surface = Surface::Make(context, 512, 512);
  auto canvas = surface->getCanvas();

  enum class DrawType { Line, Quad };

  struct TestCase {
    DrawType drawType = DrawType::Line;
    float strokeWidth = 0.0f;
    bool antiAlias = true;
    Color color = Color::White();
    std::string description;
    bool shouldReuseProgram = false;
  };

  std::vector<TestCase> testCases = {
      // Line Processor tests
      {DrawType::Line, 0.0f, true, Color::White(), "Line: AA hairline (width=0)", false},
      {DrawType::Line, 0.0f, true, Color::Red(), "Line: Same config, different color", true},
      {DrawType::Line, 0.5f, true, Color::Green(), "Line: Width 0.5 (also hairline)", true},
      {DrawType::Line, 0.0f, false, Color::Blue(), "Line: Different AAType (None)", false},
      {DrawType::Line, 0.0f, false, Color::Black(), "Line: AAType None reuse", true},
      {DrawType::Line, 2.0f, true, Color::FromRGBA(0, 255, 255),
       "Line: Width > 1 (not hairline path)", false},
      // Quad Processor tests
      {DrawType::Quad, 0.0f, true, Color::White(), "Quad: AA hairline (width=0)", false},
      {DrawType::Quad, 0.0f, true, Color::Red(), "Quad: Same config, different color", true},
      {DrawType::Quad, 0.5f, true, Color::Green(), "Quad: Width 0.5 (also hairline)", true},
      {DrawType::Quad, 0.0f, false, Color::Blue(), "Quad: Different AAType (None)", false},
      {DrawType::Quad, 0.0f, false, Color::Black(), "Quad: AAType None reuse", true},
      {DrawType::Quad, 2.0f, true, Color::FromRGBA(0, 255, 255),
       "Quad: Width > 1 (not hairline path)", false},
  };

  auto globalCache = context->globalCache();
  auto initialCount = globalCache->programMap.size();
  size_t lastProgramCount = initialCount;

  for (size_t i = 0; i < testCases.size(); ++i) {
    const auto& testCase = testCases[i];

    Paint paint;
    paint.setStrokeWidth(testCase.strokeWidth);
    paint.setAntiAlias(testCase.antiAlias);
    paint.setColor(testCase.color);
    paint.setStyle(PaintStyle::Stroke);

    if (testCase.drawType == DrawType::Line) {
      canvas->drawLine((10.0f + (i * 10)), (10.0f + (i * 10)), (100.0f + (i * 10)),
                       (100.0f + (i * 10)), paint);
    } else {
      Path curvePath;
      curvePath.moveTo((50.0f + (i * 10)), (50.0f + (i * 10)));
      curvePath.quadTo((100.0f + (i * 5)), (20.0f + (i * 5)), (150.0f + (i * 10)),
                       (50.0f + (i * 10)));
      canvas->drawPath(curvePath, paint);
    }

    context->flushAndSubmit();

    auto currentCount = globalCache->programMap.size();

    if (testCase.shouldReuseProgram) {
      EXPECT_EQ(currentCount, lastProgramCount);
    }
  }
}

TGFX_TEST(StrokeTest, HairlineComplexPathRendering) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 800, 600);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto path = SVGPathParser::FromSVGString(
      "M264,7887 C264,7891.418 260.418,7895 256,7895 L254,7895 C250.686,7895 248,7892.314 248,7889 "
      "C248,7885.686 250.686,7883 254,7883 L256,7883 C258.209,7883 260,7884.791 260,7887 "
      "C260,7889.209 258.209,7891 256,7891 L254,7891 C252.896,7891 252,7890.105 252,7889 "
      "C252,7887.895 252.896,7887 254,7887 C255.105,7887 256,7887.895 256,7889 C257.105,7889 "
      "258,7888.105 258,7887 C258,7885.895 257.105,7885 256,7885 L254,7885 C251.791,7885 "
      "250,7886.791 250,7889 C250,7891.209 251.791,7893 254,7893 L256,7893 C259.314,7893 "
      "262,7890.314 262,7887 C262,7883.686 259.314,7881 256,7881 L254,7881 C249.582,7881 "
      "246,7884.582 246,7889 C246,7893.418 249.582,7897 254,7897 L255,7897 C255.552,7897 "
      "256,7897.448 256,7898 C256,7898.552 255.552,7899 255,7899 L254,7899 C248.477,7899 "
      "244,7894.523 244,7889 C244,7883.477 248.477,7879 254,7879 L256,7879 C260.418,7879 "
      "264,7882.582 264,7887");
  path->transform(Matrix::MakeTrans(-230.f, -7878.f));
  path->transform(Matrix::MakeScale(4.f, 4.f));

  struct TestCase {
    bool antiAlias = false;
    float strokeWidth = 0.0f;
  };

  std::vector<TestCase> testCases = {
      {true, 0.0f},
      {true, 0.5f},
      {false, 0.0f},
      {false, 0.5f},
  };

  Paint paint;
  paint.setStyle(PaintStyle::Stroke);
  paint.setColor(Color::Red());

  float startX = 100.0f;
  float startY = 80.0f;
  float offsetX = 200.0f;
  float offsetY = 150.0f;

  for (size_t i = 0; i < testCases.size(); ++i) {
    auto& testCase = testCases[i];
    float x = startX + ((i % 2) * offsetX);
    float y = startY + ((static_cast<float>(i) / 2.0f) * offsetY);

    canvas->save();
    canvas->translate(x, y);

    paint.setAntiAlias(testCase.antiAlias);
    paint.setStrokeWidth(testCase.strokeWidth);
    canvas->drawPath(*path, paint);

    canvas->restore();
  }

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/HairlineComplexPathRendering"));
}

TGFX_TEST(StrokeTest, HairlineCanvasTransformations) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1000, 800);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto path = SVGPathParser::FromSVGString(
      "M264,7887 C264,7891.418 260.418,7895 256,7895 L254,7895 C250.686,7895 248,7892.314 248,7889 "
      "C248,7885.686 250.686,7883 254,7883 L256,7883 C258.209,7883 260,7884.791 260,7887 "
      "C260,7889.209 258.209,7891 256,7891 L254,7891 C252.896,7891 252,7890.105 252,7889 "
      "C252,7887.895 252.896,7887 254,7887 C255.105,7887 256,7887.895 256,7889 C257.105,7889 "
      "258,7888.105 258,7887 C258,7885.895 257.105,7885 256,7885 L254,7885 C251.791,7885 "
      "250,7886.791 250,7889 C250,7891.209 251.791,7893 254,7893 L256,7893 C259.314,7893 "
      "262,7890.314 262,7887 C262,7883.686 259.314,7881 256,7881 L254,7881 C249.582,7881 "
      "246,7884.582 246,7889 C246,7893.418 249.582,7897 254,7897 L255,7897 C255.552,7897 "
      "256,7897.448 256,7898 C256,7898.552 255.552,7899 255,7899 L254,7899 C248.477,7899 "
      "244,7894.523 244,7889 C244,7883.477 248.477,7879 254,7879 L256,7879 C260.418,7879 "
      "264,7882.582 264,7887");
  path->transform(Matrix::MakeTrans(-230.f, -7878.f));
  path->transform(Matrix::MakeScale(4.f, 4.f));

  Paint paint;
  paint.setStyle(PaintStyle::Stroke);
  paint.setColor(Color::Black());
  paint.setAntiAlias(true);
  paint.setStrokeWidth(0.0f);

  struct TransformCase {
    std::string name;
    float translateX = 0.0f;
    float translateY = 0.0f;
    float rotation = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
  };

  std::vector<TransformCase> transformCases = {
      {"Original", 100.0f, 80.0f, 0.0f, 1.0f, 1.0f},
      {"Translate", 300.0f, 80.0f, 0.0f, 1.0f, 1.0f},
      {"Rotate45", 550.0f, 100.0f, 45.0f, 1.0f, 1.0f},
      {"ScaleUp", 750.0f, 80.0f, 0.0f, 2.0f, 2.0f},
      {"ScaleDown", 100.0f, 300.0f, 0.0f, 0.5f, 0.5f},
      {"RotateAndScale", 350.0f, 350.0f, 30.0f, 1.5f, 1.5f},
      {"ScaleNonUniform", 650.0f, 350.0f, 0.0f, 1.5f, 0.8f},
      {"ComplexTransform", 100.0f, 580.0f, -15.0f, 1.2f, 1.2f},
  };

  for (const auto& testCase : transformCases) {
    canvas->save();

    canvas->translate(testCase.translateX, testCase.translateY);

    if (testCase.rotation != 0.0f) {
      canvas->rotate(testCase.rotation);
    }

    if (testCase.scaleX != 1.0f || testCase.scaleY != 1.0f) {
      canvas->scale(testCase.scaleX, testCase.scaleY);
    }

    canvas->drawPath(*path, paint);

    canvas->restore();
  }

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/HairlineCanvasTransformations"));
}

TGFX_TEST(StrokeTest, HairlineClipping) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 400);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  Paint circlePaint;
  circlePaint.setStyle(PaintStyle::Stroke);
  circlePaint.setColor(Color::Red());
  circlePaint.setAntiAlias(true);
  circlePaint.setStrokeWidth(0.0f);

  Paint clipPaint;
  clipPaint.setStyle(PaintStyle::Stroke);
  clipPaint.setColor(Color::FromRGBA(128, 128, 128, 128));
  clipPaint.setAntiAlias(true);
  clipPaint.setStrokeWidth(1.0f);

  Path path;
  path.addOval(Rect::MakeXYWH(-50, -50, 100, 100));

  canvas->translate(100, 100);
  canvas->drawPath(path, circlePaint);

  canvas->translate(150, 0);
  Path clipPath1;
  clipPath1.addOval(Rect::MakeXYWH(0, 0, 100, 100));
  canvas->drawPath(clipPath1, clipPaint);
  canvas->save();
  canvas->clipPath(clipPath1);
  canvas->drawPath(path, circlePaint);
  canvas->restore();

  canvas->translate(-150, 150);
  Path clipPath2;
  clipPath2.addRect(-25, -25, 50, 50);
  canvas->drawPath(clipPath2, clipPaint);
  canvas->save();
  canvas->clipPath(clipPath2);
  canvas->drawPath(path, circlePaint);
  canvas->restore();

  canvas->translate(150, 0);
  Path clipPath3;
  clipPath3.addRect(-60, -20, 60, 20);
  canvas->drawPath(clipPath3, clipPaint);
  canvas->save();
  canvas->clipPath(clipPath3);
  canvas->drawPath(path, circlePaint);
  canvas->restore();

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/HairlineClipping"));
}

}  // namespace tgfx

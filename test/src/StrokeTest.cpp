#include "core/utils/StrokeUtils.h"
#include "gtest/gtest.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/ShapeStyle.h"
#include "tgfx/svg/SVGPathParser.h"
#include "utils/TestUtils.h"

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

TGFX_TEST(StrokeTest, ExtremelyThinStrokePathIdentityMatrix) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 800, 400);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::Black());

  Path path;
  path.addOval(Rect::MakeXYWH(50, 50, 300, 300));

  Paint paint;
  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(0.5f);
  paint.setColor(Color::FromRGBA(255, 255, 0, 255));

  canvas->drawPath(path, paint);
  canvas->translate(400, 0);
  canvas->drawPath(path, paint);

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/ExtremelyThinStrokePathIdentityMatrix"));
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

TGFX_TEST(StrokeTest, HairlineStrokeText) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);

  // Create surface
  auto surface = Surface::Make(context, 150, 300);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  // Clear background
  canvas->clear(Color::White());

  // Create font
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 12);

  // Create stroke paint only
  Paint strokePaint;
  strokePaint.setStyle(PaintStyle::Stroke);
  strokePaint.setColor(Color::Red());

  Paint fillPaint;
  fillPaint.setStyle(PaintStyle::Fill);
  fillPaint.setColor(Color::Blue());

  // Draw text with stroke only
  strokePaint.setStrokeWidth(2.0f);
  canvas->drawSimpleText("Width 2.0", 50, 100, font, fillPaint);
  canvas->drawSimpleText("Width 2.0", 50, 100, font, strokePaint);

  // Test with different stroke widths
  strokePaint.setStrokeWidth(1.0f);
  canvas->drawSimpleText("Width 1.0", 50, 150, font, fillPaint);
  canvas->drawSimpleText("Width 1.0", 50, 150, font, strokePaint);

  strokePaint.setStrokeWidth(0.5f);
  canvas->drawSimpleText("Width 0.5", 50, 200, font, fillPaint);
  canvas->drawSimpleText("Width 0.5", 50, 200, font, strokePaint);

  // Test with very small stroke width
  strokePaint.setStrokeWidth(0.2f);
  canvas->drawSimpleText("Width 0.2", 50, 250, font, fillPaint);
  canvas->drawSimpleText("Width 0.2", 50, 250, font, strokePaint);

  context->flush();
  // Compare with baseline
  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/HairlineStrokeText"));
  device->unlock();
}

TGFX_TEST(StrokeTest, HairlineRectDrawOp) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 400);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  // Test hairline rect rendering with RectDrawOp at different scales and LineJoin types.
  // This test is created to verify the rendering behavior before fixing hairline issues.

  Paint paint;
  paint.setColor(Color::Red());
  paint.setStyle(PaintStyle::Stroke);

  // Row 1: Miter join (default)
  Stroke miterStroke(0.0f);
  miterStroke.join = LineJoin::Miter;
  paint.setStroke(miterStroke);

  canvas->save();
  canvas->translate(60, 60);
  canvas->drawRect(Rect::MakeXYWH(-40, -40, 80, 80), paint);
  canvas->translate(120, 0);
  canvas->scale(2.0f, 2.0f);
  canvas->drawRect(Rect::MakeXYWH(-20, -20, 40, 40), paint);
  canvas->translate(60, 0);
  canvas->scale(2.0f, 2.0f);
  canvas->drawRect(Rect::MakeXYWH(-10, -10, 20, 20), paint);
  canvas->restore();

  // Row 2: Bevel join
  paint.setColor(Color::Green());
  Stroke bevelStroke(0.0f);
  bevelStroke.join = LineJoin::Bevel;
  paint.setStroke(bevelStroke);

  canvas->save();
  canvas->translate(60, 180);
  canvas->drawRect(Rect::MakeXYWH(-40, -40, 80, 80), paint);
  canvas->translate(120, 0);
  canvas->scale(2.0f, 2.0f);
  canvas->drawRect(Rect::MakeXYWH(-20, -20, 40, 40), paint);
  canvas->translate(60, 0);
  canvas->scale(2.0f, 2.0f);
  canvas->drawRect(Rect::MakeXYWH(-10, -10, 20, 20), paint);
  canvas->restore();

  // Row 3: Round join
  paint.setColor(Color::Blue());
  Stroke roundStroke(0.0f);
  roundStroke.join = LineJoin::Round;
  paint.setStroke(roundStroke);

  canvas->save();
  canvas->translate(60, 300);
  canvas->drawRect(Rect::MakeXYWH(-40, -40, 80, 80), paint);
  canvas->translate(120, 0);
  canvas->scale(2.0f, 2.0f);
  canvas->drawRect(Rect::MakeXYWH(-20, -20, 40, 40), paint);
  canvas->translate(60, 0);
  canvas->scale(2.0f, 2.0f);
  canvas->drawRect(Rect::MakeXYWH(-10, -10, 20, 20), paint);
  canvas->restore();

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/HairlineRectDrawOp"));
}

TGFX_TEST(StrokeTest, HairlineRectDrawOpWithAA) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 200);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  // Test hairline rect rendering with antialiasing enabled.

  Paint paint;
  paint.setColor(Color::Red());
  paint.setStyle(PaintStyle::Stroke);
  paint.setAntiAlias(true);

  Stroke hairlineStroke(0.0f);
  paint.setStroke(hairlineStroke);

  // Draw multiple hairline rects at different positions and scales
  canvas->save();
  canvas->translate(60, 60);
  canvas->drawRect(Rect::MakeXYWH(-40, -40, 80, 80), paint);

  canvas->translate(120, 0);
  canvas->scale(2.0f, 2.0f);
  canvas->drawRect(Rect::MakeXYWH(-20, -20, 40, 40), paint);

  canvas->translate(60, 0);
  canvas->scale(2.0f, 2.0f);
  canvas->drawRect(Rect::MakeXYWH(-10, -10, 20, 20), paint);
  canvas->restore();

  // Draw with rotation to test non-axis-aligned hairline
  paint.setColor(Color::Blue());
  canvas->save();
  canvas->translate(60, 140);
  canvas->rotate(15.0f);
  canvas->drawRect(Rect::MakeXYWH(-30, -30, 60, 60), paint);
  canvas->restore();

  canvas->save();
  canvas->translate(180, 140);
  canvas->rotate(30.0f);
  canvas->drawRect(Rect::MakeXYWH(-30, -30, 60, 60), paint);
  canvas->restore();

  canvas->save();
  canvas->translate(300, 140);
  canvas->rotate(45.0f);
  canvas->drawRect(Rect::MakeXYWH(-30, -30, 60, 60), paint);
  canvas->restore();

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/HairlineRectDrawOpWithAA"));
}

TGFX_TEST(StrokeTest, HairlineRRectDrawOp) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 400);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  // Test hairline RRect/Oval/Circle rendering with RRectDrawOp (non-AA mode).
  // This includes drawRoundRect, drawOval, and drawCircle which all go through RRectDrawOp.

  Paint paint;
  paint.setColor(Color::Red());
  paint.setStyle(PaintStyle::Stroke);

  Stroke hairlineStroke(0.0f);
  paint.setStroke(hairlineStroke);

  // Row 1: RoundRect at different scales
  canvas->save();
  canvas->translate(60, 60);
  canvas->drawRoundRect(Rect::MakeXYWH(-40, -40, 80, 80), 10, 10, paint);
  canvas->translate(120, 0);
  canvas->scale(2.0f, 2.0f);
  canvas->drawRoundRect(Rect::MakeXYWH(-20, -20, 40, 40), 5, 5, paint);
  canvas->translate(60, 0);
  canvas->scale(2.0f, 2.0f);
  canvas->drawRoundRect(Rect::MakeXYWH(-10, -10, 20, 20), 2.5f, 2.5f, paint);
  canvas->restore();

  // Row 2: Oval at different scales
  paint.setColor(Color::Green());
  canvas->save();
  canvas->translate(60, 180);
  canvas->drawOval(Rect::MakeXYWH(-40, -30, 80, 60), paint);
  canvas->translate(120, 0);
  canvas->scale(2.0f, 2.0f);
  canvas->drawOval(Rect::MakeXYWH(-20, -15, 40, 30), paint);
  canvas->translate(60, 0);
  canvas->scale(2.0f, 2.0f);
  canvas->drawOval(Rect::MakeXYWH(-10.0f, -7.5f, 20.0f, 15.0f), paint);
  canvas->restore();

  // Row 3: Circle at different scales
  paint.setColor(Color::Blue());
  canvas->save();
  canvas->translate(60, 300);
  canvas->drawCircle(0, 0, 40, paint);
  canvas->translate(120, 0);
  canvas->scale(2.0f, 2.0f);
  canvas->drawCircle(0, 0, 20, paint);
  canvas->translate(60, 0);
  canvas->scale(2.0f, 2.0f);
  canvas->drawCircle(0, 0, 10, paint);
  canvas->restore();

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/HairlineRRectDrawOp"));
}

TGFX_TEST(StrokeTest, HairlineRRectDrawOpWithAA) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 400);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  // Test hairline RRect/Oval/Circle rendering with RRectDrawOp (AA mode).

  Paint paint;
  paint.setColor(Color::Red());
  paint.setStyle(PaintStyle::Stroke);
  paint.setAntiAlias(true);

  Stroke hairlineStroke(0.0f);
  paint.setStroke(hairlineStroke);

  // Row 1: RoundRect at different scales
  canvas->save();
  canvas->translate(60, 60);
  canvas->drawRoundRect(Rect::MakeXYWH(-40, -40, 80, 80), 10, 10, paint);
  canvas->translate(120, 0);
  canvas->scale(2.0f, 2.0f);
  canvas->drawRoundRect(Rect::MakeXYWH(-20, -20, 40, 40), 5, 5, paint);
  canvas->translate(60, 0);
  canvas->scale(2.0f, 2.0f);
  canvas->drawRoundRect(Rect::MakeXYWH(-10, -10, 20, 20), 2.5f, 2.5f, paint);
  canvas->restore();

  // Row 2: Oval at different scales
  paint.setColor(Color::Green());
  canvas->save();
  canvas->translate(60, 180);
  canvas->drawOval(Rect::MakeXYWH(-40, -30, 80, 60), paint);
  canvas->translate(120, 0);
  canvas->scale(2.0f, 2.0f);
  canvas->drawOval(Rect::MakeXYWH(-20, -15, 40, 30), paint);
  canvas->translate(60, 0);
  canvas->scale(2.0f, 2.0f);
  canvas->drawOval(Rect::MakeXYWH(-10.0f, -7.5f, 20.0f, 15.0f), paint);
  canvas->restore();

  // Row 3: Circle at different scales
  paint.setColor(Color::Blue());
  canvas->save();
  canvas->translate(60, 300);
  canvas->drawCircle(0, 0, 40, paint);
  canvas->translate(120, 0);
  canvas->scale(2.0f, 2.0f);
  canvas->drawCircle(0, 0, 20, paint);
  canvas->translate(60, 0);
  canvas->scale(2.0f, 2.0f);
  canvas->drawCircle(0, 0, 10, paint);
  canvas->restore();

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/HairlineRRectDrawOpWithAA"));
}

}  // namespace tgfx

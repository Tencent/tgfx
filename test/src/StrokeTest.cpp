#include "core/utils/StrokeUtils.h"
#include "gtest/gtest.h"
#include "tgfx/core/PathStroker.h"
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

TGFX_TEST(StrokeTest, PathStrokerEmptyParams) {
  Path path = {};
  path.addRect(Rect::MakeXYWH(0, 0, 100, 100));

  std::vector<PathStroker::PointParam> emptyParams = {};
  EXPECT_FALSE(PathStroker::StrokePathWithMultiParams(&path, 5.0f, emptyParams, 1.0f));

  EXPECT_FALSE(PathStroker::StrokePathWithMultiParams(nullptr, 5.0f, emptyParams, 1.0f));
}

TGFX_TEST(StrokeTest, PathStrokerSingleParam) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 512, 512);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear();

  Path path = {};
  path.moveTo(25, 100);
  path.cubicTo(50, 80, 100, 80, 125, 100);
  path.cubicTo(100, 120, 50, 120, 25, 100);
  path.transform(Matrix::MakeScale(3.0f));

  auto hairlinePath = path;

  std::vector<PathStroker::PointParam> params = {};
  params.emplace_back(LineCap::Square, LineJoin::Round);
  EXPECT_TRUE(PathStroker::StrokePathWithMultiParams(&path, 10.0f, params, 1.0f));

  Paint paint = {};
  paint.setColor(Color::FromRGBA(0, 255, 0, 255));
  canvas->drawPath(path, paint);

  Paint hairlinePaint = {};
  hairlinePaint.setColor(Color::Red());
  hairlinePaint.setStyle(PaintStyle::Stroke);
  hairlinePaint.setStrokeWidth(0.0f);
  canvas->drawPath(hairlinePath, hairlinePaint);

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/PathStrokerSingleParam"));
}

TGFX_TEST(StrokeTest, PathStrokerSingleLine) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 300);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear();

  Paint hairlinePaint = {};
  hairlinePaint.setColor(Color::Red());
  hairlinePaint.setStyle(PaintStyle::Stroke);
  hairlinePaint.setStrokeWidth(0.0f);

  Path line1 = {};
  line1.moveTo(50, 50);
  line1.lineTo(250, 50);

  auto hairlineLine1 = line1;

  std::vector<PathStroker::PointParam> params1 = {};
  params1.emplace_back(LineCap::Butt);
  params1.emplace_back(LineCap::Round);

  EXPECT_TRUE(PathStroker::StrokePathWithMultiParams(&line1, 15.0f, params1, 1.0f));

  Paint paint = {};
  paint.setColor(Color::FromRGBA(255, 0, 0, 255));
  canvas->drawPath(line1, paint);
  canvas->drawPath(hairlineLine1, hairlinePaint);

  canvas->translate(0, 80);
  Path line2 = {};
  line2.moveTo(50, 50);
  line2.lineTo(250, 50);

  auto hairlineLine2 = line2;

  std::vector<PathStroker::PointParam> params2 = {};
  params2.emplace_back(LineCap::Round);

  EXPECT_TRUE(PathStroker::StrokePathWithMultiParams(&line2, 15.0f, params2, 1.0f));
  paint.setColor(Color::FromRGBA(0, 255, 0, 255));
  canvas->drawPath(line2, paint);
  canvas->drawPath(hairlineLine2, hairlinePaint);

  canvas->translate(0, 80);
  Path line3 = {};
  line3.moveTo(50, 50);
  line3.lineTo(250, 50);

  auto hairlineLine3 = line3;

  std::vector<PathStroker::PointParam> params3 = {};
  params3.emplace_back(LineCap::Square);

  EXPECT_TRUE(PathStroker::StrokePathWithMultiParams(&line3, 15.0f, params3, 1.0f));
  paint.setColor(Color::FromRGBA(0, 0, 255, 255));
  canvas->drawPath(line3, paint);
  canvas->drawPath(hairlineLine3, hairlinePaint);

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/PathStrokerSingleLine"));
}

TGFX_TEST(StrokeTest, PathStrokerWithMultiParams) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 150);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear();

  Path path = {};
  path.moveTo(50, 50);
  path.lineTo(100, 100);
  path.lineTo(150, 50);
  path.lineTo(200, 100);
  path.lineTo(250, 50);

  auto hairlinePath = path;

  std::vector<PathStroker::PointParam> params = {};
  params.emplace_back(LineCap::Square);
  params.emplace_back(LineJoin::Round);
  params.emplace_back(LineJoin::Bevel);
  params.emplace_back(LineJoin::Miter);
  params.emplace_back(LineCap::Round);

  EXPECT_TRUE(PathStroker::StrokePathWithMultiParams(&path, 10.0f, params, 1.0f));

  Paint paint = {};
  paint.setColor(Color::White());
  canvas->drawPath(path, paint);

  Paint hairlinePaint = {};
  hairlinePaint.setColor(Color::Red());
  hairlinePaint.setStyle(PaintStyle::Stroke);
  hairlinePaint.setStrokeWidth(0.0f);
  canvas->drawPath(hairlinePath, hairlinePaint);

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/PathStrokerWithMultiParams"));
}

TGFX_TEST(StrokeTest, PathStrokerDifferentMiterLimits) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 200);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear();

  Path path1 = {};
  path1.moveTo(50, 150);
  path1.lineTo(100, 50);
  path1.lineTo(150, 150);

  auto hairlinePath1 = path1;

  std::vector<PathStroker::PointParam> lowMiterParams = {};
  lowMiterParams.emplace_back(LineCap::Butt, LineJoin::Miter, 1.0f);
  lowMiterParams.emplace_back(LineCap::Butt, LineJoin::Miter, 1.0f);
  lowMiterParams.emplace_back(LineCap::Butt, LineJoin::Miter, 1.0f);

  EXPECT_TRUE(PathStroker::StrokePathWithMultiParams(&path1, 15.0f, lowMiterParams, 1.0f));

  Paint paint = {};
  paint.setColor(Color::FromRGBA(255, 255, 0, 255));
  canvas->drawPath(path1, paint);

  Paint hairlinePaint = {};
  hairlinePaint.setColor(Color::Red());
  hairlinePaint.setStyle(PaintStyle::Stroke);
  hairlinePaint.setStrokeWidth(0.0f);
  canvas->drawPath(hairlinePath1, hairlinePaint);

  canvas->translate(200, 0);
  Path path2 = {};
  path2.moveTo(50, 150);
  path2.lineTo(100, 50);
  path2.lineTo(150, 150);

  auto hairlinePath2 = path2;

  std::vector<PathStroker::PointParam> highMiterParams = {};
  highMiterParams.emplace_back(LineCap::Butt, LineJoin::Miter, 10.0f);
  highMiterParams.emplace_back(LineCap::Butt, LineJoin::Miter, 10.0f);
  highMiterParams.emplace_back(LineCap::Butt, LineJoin::Miter, 10.0f);

  EXPECT_TRUE(PathStroker::StrokePathWithMultiParams(&path2, 15.0f, highMiterParams, 1.0f));
  canvas->drawPath(path2, paint);
  canvas->drawPath(hairlinePath2, hairlinePaint);

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/PathStrokerDifferentMiterLimits"));
}

TGFX_TEST(StrokeTest, PathStrokerOpenPath) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 300);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear();

  Path path1 = {};
  path1.moveTo(20, 50);
  path1.cubicTo(50, 20, 100, 80, 130, 50);
  path1.quadTo(160, 20, 180, 50);

  auto hairlinePath1 = path1;

  std::vector<PathStroker::PointParam> params1 = {};
  params1.emplace_back(LineCap::Butt, LineJoin::Round);
  params1.emplace_back(LineCap::Round, LineJoin::Miter);
  params1.emplace_back(LineCap::Square, LineJoin::Bevel);

  EXPECT_TRUE(PathStroker::StrokePathWithMultiParams(&path1, 8.0f, params1, 1.0f));

  Paint paint = {};
  paint.setColor(Color::FromRGBA(255, 100, 100, 255));
  canvas->drawPath(path1, paint);

  Paint hairlinePaint = {};
  hairlinePaint.setColor(Color::Red());
  hairlinePaint.setStyle(PaintStyle::Stroke);
  hairlinePaint.setStrokeWidth(0.0f);
  canvas->drawPath(hairlinePath1, hairlinePaint);

  canvas->translate(0, 100);
  Path path2 = {};
  path2.moveTo(20, 50);
  path2.quadTo(60, 100, 100, 50);
  path2.cubicTo(140, 0, 180, 100, 180, 50);

  auto hairlinePath2 = path2;

  std::vector<PathStroker::PointParam> params2 = {};
  params2.emplace_back(LineCap::Round, LineJoin::Round, 6.0f);
  params2.emplace_back(LineCap::Square, LineJoin::Miter, 6.0f);

  EXPECT_TRUE(PathStroker::StrokePathWithMultiParams(&path2, 10.0f, params2, 1.0f));
  paint.setColor(Color::FromRGBA(100, 255, 100, 255));
  canvas->drawPath(path2, paint);
  canvas->drawPath(hairlinePath2, hairlinePaint);

  canvas->translate(0, 100);
  Path path3 = {};
  path3.moveTo(20, 50);
  path3.cubicTo(60, 20, 120, 80, 180, 30);

  auto hairlinePath3 = path3;

  std::vector<PathStroker::PointParam> params3 = {};
  params3.emplace_back(LineCap::Square, LineJoin::Bevel);
  params3.emplace_back(LineCap::Round, LineJoin::Round, 8.0f);

  EXPECT_TRUE(PathStroker::StrokePathWithMultiParams(&path3, 6.0f, params3, 1.0f));
  paint.setColor(Color::FromRGBA(100, 100, 255, 255));
  canvas->drawPath(path3, paint);
  canvas->drawPath(hairlinePath3, hairlinePaint);

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/PathStrokerOpenPath"));
}

TGFX_TEST(StrokeTest, PathStrokerContours) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1024, 512);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear();

  Paint paint = {};
  paint.setColor(Color::FromRGBA(0, 255, 0, 255));
  Paint hairlinePaint = {};
  hairlinePaint.setColor(Color::Red());
  hairlinePaint.setStyle(PaintStyle::Stroke);
  hairlinePaint.setStrokeWidth(0.0f);

  // Test 1: Double cubic bezier curve (ContourI)
  {
    Path path = {};
    path.moveTo(25, 100);
    path.cubicTo(50, 80, 100, 80, 125, 100);
    path.cubicTo(100, 120, 50, 120, 25, 100);
    path.close();
    path.transform(Matrix::MakeScale(3.0f));

    auto hairlinePath = path;

    std::vector<PathStroker::PointParam> params = {};
    params.emplace_back(LineJoin::Round);
    params.emplace_back(LineJoin::Miter);
    params.emplace_back(LineJoin::Bevel);
    EXPECT_TRUE(PathStroker::StrokePathWithMultiParams(&path, 10.0f, params, 1.0f));

    canvas->save();
    canvas->translate(0, -200);
    canvas->drawPath(path, paint);
    canvas->drawPath(hairlinePath, hairlinePaint);
    canvas->restore();
  }

  // Test 2: Single cubic bezier curve (ContourII)
  {
    Path path = {};
    path.moveTo(25, 100);
    path.cubicTo(50, 80, 100, 80, 125, 100);
    path.close();
    path.transform(Matrix::MakeScale(3.0f));

    auto hairlinePath = path;

    std::vector<PathStroker::PointParam> params = {};
    params.emplace_back(LineJoin::Round);
    params.emplace_back(LineJoin::Bevel);
    EXPECT_TRUE(PathStroker::StrokePathWithMultiParams(&path, 10.0f, params, 1.0f));

    canvas->save();
    canvas->translate(400, -200);
    canvas->drawPath(path, paint);
    canvas->drawPath(hairlinePath, hairlinePaint);
    canvas->restore();
  }

  // Test 3: Triangle with line segments (ContourIII)
  {
    Path path = {};
    path.moveTo(9.5, 5);
    path.lineTo(3, 0);
    path.lineTo(0, 3.5);
    path.lineTo(9.5, 5);
    path.close();
    path.transform(Matrix::MakeScale(20.0f));

    auto hairlinePath = path;

    std::vector<PathStroker::PointParam> params = {};
    params.emplace_back();
    params.emplace_back();
    params.emplace_back();
    params.emplace_back();
    EXPECT_TRUE(PathStroker::StrokePathWithMultiParams(&path, 10.0f, params, 10.0f));

    canvas->save();
    canvas->translate(100, 200);
    canvas->drawPath(path, paint);
    canvas->drawPath(hairlinePath, hairlinePaint);
    canvas->restore();
  }

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/PathStrokerContours"));
}

TGFX_TEST(StrokeTest, PathStrokerComplexCurves) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 400);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear();

  Path path = {};
  path.moveTo(50, 200);
  path.quadTo(100, 100, 150, 200);
  path.cubicTo(180, 250, 220, 150, 250, 200);
  path.quadTo(275, 250, 300, 200);
  path.lineTo(350, 150);

  auto hairlinePath = path;

  std::vector<PathStroker::PointParam> params = {};
  params.emplace_back(LineCap::Butt, LineJoin::Miter, 2.0f);
  params.emplace_back(LineCap::Round, LineJoin::Round);
  params.emplace_back(LineCap::Square, LineJoin::Bevel, 6.0f);
  params.emplace_back(LineCap::Round, LineJoin::Miter, 8.0f);
  params.emplace_back(LineCap::Butt, LineJoin::Bevel);

  EXPECT_TRUE(PathStroker::StrokePathWithMultiParams(&path, 12.0f, params, 1.0f));

  Paint paint = {};
  paint.setColor(Color::White());
  canvas->drawPath(path, paint);

  Paint hairlinePaint = {};
  hairlinePaint.setColor(Color::Red());
  hairlinePaint.setStyle(PaintStyle::Stroke);
  hairlinePaint.setStrokeWidth(0.0f);
  canvas->drawPath(hairlinePath, hairlinePaint);

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/PathStrokerComplexCurves"));
}

TGFX_TEST(StrokeTest, PathStrokerDashMultiParamsBasic) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 200);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  Paint paint;
  paint.setColor(Color::Black());

  // Create a path with mixed line and curve segments
  Path path;
  path.moveTo(50, 150);
  path.lineTo(150, 50);
  path.lineTo(250, 150);
  path.lineTo(350, 50);

  // Set different join styles for corner vertices
  std::vector<PathStroker::PointParam> params;
  params.emplace_back(LineJoin::Miter);
  params.emplace_back(LineJoin::Round);
  params.emplace_back(LineJoin::Bevel);
  params.emplace_back(LineJoin::Miter);

  PathStroker::PointParam defaultParam(LineCap::Round);

  std::vector<float> intervals = {100.f, 100.f};
  EXPECT_TRUE(PathStroker::StrokeDashPathWithMultiParams(&path, 10.0f, params, defaultParam,
                                                         intervals.data(), 2, 50, 1.0f));
  // EXPECT_TRUE(PathStroker::StrokePathWithMultiParams(&path, 10.0f, params, 1.0f));

  canvas->drawPath(path, paint);

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/PathStrokerDashMultiParamsBasic"));
}

TGFX_TEST(StrokeTest, PathStrokerDashMultiParamsClosedCurve) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 300);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  Paint paint;
  paint.setColor(Color::FromRGBA(255, 0, 0));

  // Create a closed curve path with cubic curves
  Path path;
  path.moveTo(200, 50);
  path.cubicTo(300, 100, 300, 200, 200, 250);
  path.cubicTo(100, 200, 100, 100, 200, 50);
  path.close();

  // Set different join styles for corner vertices
  std::vector<PathStroker::PointParam> params;
  params.emplace_back(LineJoin::Miter);
  params.emplace_back(LineJoin::Round);
  params.emplace_back(LineJoin::Bevel);

  PathStroker::PointParam defaultParam(LineCap::Round);

  std::vector<float> intervals = {20.f, 20.f};
  EXPECT_TRUE(PathStroker::StrokeDashPathWithMultiParams(&path, 6.0f, params, defaultParam,
                                                         intervals.data(), 2, 10, 1.0f));

  canvas->drawPath(path, paint);

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/PathStrokerDashMultiParamsClosedCurve"));
}

TGFX_TEST(StrokeTest, PathStrokerDashMultiParamsComparison) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 600, 300);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  Paint paint;
  paint.setColor(Color::Black());

  // Create a wave path using quadratic curves
  Path path;
  path.moveTo(30, 150);
  path.quadTo(60, 80, 90, 150);
  path.quadTo(120, 220, 150, 150);
  path.quadTo(180, 80, 210, 150);
  path.quadTo(240, 220, 270, 150);

  // Set alternating join styles for wave peaks/valleys
  std::vector<PathStroker::PointParam> params;
  params.emplace_back(LineJoin::Round);
  params.emplace_back(LineJoin::Bevel);
  params.emplace_back(LineJoin::Round);
  params.emplace_back(LineJoin::Bevel);
  params.emplace_back(LineJoin::Round);

  // Left side: multi-params stroke without dash
  auto pathLeft = path;
  EXPECT_TRUE(PathStroker::StrokePathWithMultiParams(&pathLeft, 4.0f, params, 1.0f));
  canvas->drawPath(pathLeft, paint);

  // Right side: multi-params stroke with dash
  canvas->save();
  canvas->translate(300, 0);
  auto pathRight = path;
  PathStroker::PointParam defaultParam(LineCap::Round);
  std::vector<float> intervals = {20.f, 20.f};
  EXPECT_TRUE(PathStroker::StrokeDashPathWithMultiParams(&pathRight, 4.0f, params, defaultParam,
                                                         intervals.data(), 2, 10, 1.0f));
  canvas->drawPath(pathRight, paint);
  canvas->restore();

  EXPECT_TRUE(Baseline::Compare(surface, "StrokeTest/PathStrokerDashMultiParamsComparison"));
}

}  // namespace tgfx

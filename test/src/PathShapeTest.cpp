/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "core/PathRef.h"
#include "core/shapes/AppendShape.h"
#include "gtest/gtest.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/TextBlob.h"
#include "tgfx/svg/SVGPathParser.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(PathShapeTest, path) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 600, 500);
  auto canvas = surface->getCanvas();
  Path path;
  path.addRect(Rect::MakeXYWH(10, 10, 100, 100));
  Paint paint;
  paint.setColor(Color::White());
  canvas->drawPath(path, paint);
  path.reset();
  path.addRoundRect(Rect::MakeXYWH(10, 120, 100, 100), 10, 10);
  canvas->drawPath(path, paint);
  path.reset();
  path.addRect(Rect::MakeXYWH(0, 0, 100, 100));
  auto matrix = Matrix::I();
  matrix.postRotate(30, 50, 50);
  path.transform(matrix);
  matrix.reset();
  matrix.postTranslate(20, 250);
  canvas->setMatrix(matrix);
  canvas->drawPath(path, paint);
  paint.setColor(Color::Black());
  paint.setAlpha(0.7f);
  matrix.reset();
  matrix.postScale(0.5, 0.5, 50, 50);
  matrix.postTranslate(20, 250);
  canvas->setMatrix(matrix);
  canvas->drawPath(path, paint);
  Path roundPath = {};
  roundPath.addRoundRect(Rect::MakeXYWH(0, 0, 100, 100), 20, 20);
  matrix.reset();
  matrix.postRotate(30, 50, 50);
  roundPath.transform(matrix);
  matrix.reset();
  matrix.postRotate(15, 50, 50);
  matrix.postScale(2, 2, 50, 50);
  matrix.postTranslate(250, 150);
  paint.setShader(Shader::MakeLinearGradient(Point{0.f, 0.f}, Point{25.f, 100.f},
                                             {Color{0.f, 1.f, 0.f, 1.f}, Color{1.f, 0.f, 0.f, 0.f}},
                                             {}));
  canvas->setMatrix(matrix);
  canvas->drawPath(roundPath, paint);
  matrix.reset();
  matrix.postRotate(15, 50, 50);
  matrix.postScale(1.5f, 0.3f, 50, 50);
  matrix.postTranslate(250, 150);
  paint.setShader(nullptr);
  paint.setColor(Color::Black());
  paint.setAlpha(0.7f);
  canvas->setMatrix(matrix);
  canvas->drawPath(path, paint);
  canvas->resetMatrix();
  paint.setStrokeWidth(20);
  canvas->drawLine(200, 50, 400, 50, paint);
  paint.setLineCap(LineCap::Round);
  canvas->drawLine(200, 320, 400, 320, paint);
  path.reset();
  path.quadTo(Point{100, 150}, Point{150, 150});
  paint.setColor(Color::White());
  matrix.reset();
  matrix.postTranslate(450, 10);
  canvas->setMatrix(matrix);
  canvas->drawPath(path, paint);
  path.reset();
  canvas->drawPath(path, paint);

  path.addRect({0, 0, 150, 150});
  paint.setColor(Color::Red());
  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(1);
  matrix.reset();
  matrix.postTranslate(450, 200);
  canvas->setMatrix(matrix);
  canvas->drawPath(path, paint);

  path.reset();
  path.addArc({0, 0, 150, 150}, -90.f, 235.f);
  Color red = {1.f, 0.f, 0.f, 1.f};
  Color green = {0.f, 1.f, 0.f, 1.f};
  Color blue = {0.f, 0.f, 1.f, 1.f};
  paint.setStyle(PaintStyle::Fill);
  paint.setShader(Shader::MakeLinearGradient(Point{0.f, 0.f}, Point{25.f, 150.f},
                                             {red, green, blue, green, red, blue, red, green, red,
                                              green, blue, green, red, blue, red, green, blue},
                                             {}));
  matrix.reset();
  matrix.postTranslate(450, 200);
  canvas->setMatrix(matrix);
  canvas->drawPath(path, paint);

  paint.reset();
  auto arcStart = Point::Make(0, 0);
  auto arcEnd = Point::Make(45, 45);
  auto pathEnd = Point::Make(45, 0);
  std::vector<Point> transforms = {{0, 0}, {50, 0}, {100, -50}, {100, 0}};
  std::vector<std::pair<PathArcSize, bool>> arcType = {{PathArcSize::Small, false},
                                                       {PathArcSize::Large, false},
                                                       {PathArcSize::Small, true},
                                                       {PathArcSize::Large, true}};
  matrix.reset();
  matrix.setTranslate(10, 450);
  canvas->setMatrix(matrix);
  for (uint32_t i = 0; i < 4; i++) {
    path.reset();
    path.moveTo(arcStart);
    path.arcTo(45, 45, 0, arcType[i].first, arcType[i].second, arcEnd);
    path.lineTo(pathEnd);
    canvas->translate(transforms[i].x, transforms[i].y);
    canvas->drawPath(path, paint);
  }

  Point latestPoint = {};
  path.getLastPoint(&latestPoint);
  EXPECT_EQ(latestPoint, Point::Make(45, 0));

  paint.setColor(Color::Red());
  path.reset();
  path.arcTo({50, 0}, {50, 50}, 50);
  path.arcTo({50, 100}, {0, 100}, 50);
  matrix.reset();
  matrix.postTranslate(450, 390);
  canvas->setMatrix(matrix);
  canvas->drawPath(path, paint);

  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/path"));
}

TGFX_TEST(PathShapeTest, simpleShape) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto width = 400;
  auto height = 500;
  auto surface = Surface::Make(context, width, height);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());
  auto image = MakeImage("resources/apitest/imageReplacement_VP8L.webp");
  ASSERT_TRUE(image != nullptr);
  Paint paint;
  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(2);
  paint.setColor(Color{1.f, 0.f, 0.f, 1.f});
  auto point = Point::Make(width / 2, height / 2);
  auto radius = image->width() / 2;
  auto rect = Rect::MakeWH(radius * 2, radius * 2);
  canvas->drawCircle(point, static_cast<float>(radius) + 30.0f, paint);
  canvas->setMatrix(Matrix::MakeTrans(point.x - static_cast<float>(radius),
                                      point.y - static_cast<float>(radius)));
  canvas->drawRoundRect(rect, 10, 10, paint);

  canvas->setMatrix(Matrix::MakeTrans(point.x - static_cast<float>(radius),
                                      point.y - static_cast<float>(radius)));
  canvas->rotate(45.0f, static_cast<float>(radius), static_cast<float>(radius));
  canvas->drawImage(image, SamplingOptions(FilterMode::Linear));
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/shape"));
}

static std::vector<Resource*> FindResourceByDomainID(Context* context, uint32_t domainID) {
  std::vector<Resource*> resources = {};
  auto resourceCache = context->resourceCache();
  for (auto& item : resourceCache->uniqueKeyMap) {
    auto resource = item.second;
    if (resource->uniqueKey.domainID() == domainID) {
      resources.push_back(resource);
    }
  }
  return resources;
}

TGFX_TEST(PathShapeTest, inversePath) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 420, 100);
  auto canvas = surface->getCanvas();
  Paint paint;
  paint.setColor(Color::Red());
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 70.f);
  font.setFauxBold(true);
  auto textBlob = TextBlob::MakeFrom("Hello TGFX", font);
  auto textShape = Shape::MakeFrom(textBlob);
  ASSERT_TRUE(textShape != nullptr);
  Path textPath = textShape->getPath();
  EXPECT_TRUE(!textPath.isEmpty());
  textPath.toggleInverseFillType();
  EXPECT_TRUE(textPath.isInverseFillType());
  textPath.transform(Matrix::MakeTrans(10, 75));
  canvas->clipPath(textPath);
  Path emptyPath = {};
  emptyPath.toggleInverseFillType();
  auto dropShadowFilter = ImageFilter::DropShadow(2, 2, 2, 2, Color::Black());
  paint.setImageFilter(dropShadowFilter);
  canvas->drawPath(emptyPath, paint);
  paint.setImageFilter(nullptr);
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/inversePath_text"));

  surface = Surface::Make(context, 400, 400);
  canvas = surface->getCanvas();
  Path clipPath = {};
  clipPath.addRect(Rect::MakeXYWH(50, 200, 300, 150));
  clipPath.toggleInverseFillType();
  canvas->save();
  canvas->clipPath(clipPath);
  Path path = {};
  path.addRect(Rect::MakeXYWH(50, 50, 170, 100));
  path.addOval(Rect::MakeXYWH(180, 50, 170, 100));
  path.setFillType(PathFillType::InverseEvenOdd);
  paint.setColor(Color::Red());
  canvas->drawPath(path, paint);
  canvas->restore();
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/inversePath_rect"));
  auto uniqueKey = PathRef::GetUniqueKey(path);
  auto cachesBefore = FindResourceByDomainID(context, uniqueKey.domainID());
  EXPECT_EQ(cachesBefore.size(), 1u);
  canvas->clear();
  canvas->clipPath(clipPath);
  auto shape = Shape::MakeFrom(path);
  shape = Shape::ApplyMatrix(std::move(shape), Matrix::MakeTrans(50, 50));
  canvas->translate(-50, -50);
  canvas->drawShape(shape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/inversePath_rect"));
  auto cachesAfter = FindResourceByDomainID(context, uniqueKey.domainID());
  EXPECT_EQ(cachesAfter.size(), 1u);
  EXPECT_TRUE(cachesBefore.front() == cachesAfter.front());
}

TGFX_TEST(PathShapeTest, drawShape) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto width = 300;
  auto height = 200;
  auto surface = Surface::Make(context, width, height);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  Path path = {};
  auto rect = Rect::MakeWH(50, 50);
  path.addRect(rect);
  auto shape = Shape::MakeFrom(path);
  path.reset();
  path.addOval(Rect::MakeWH(100, 100));
  auto shape2 = Shape::MakeFrom(path);
  auto mergedShape = Shape::Merge(shape, shape2, PathOp::Append);
  EXPECT_FALSE(mergedShape->isSimplePath());
  auto transShape = Shape::ApplyMatrix(shape, Matrix::MakeTrans(10, 10));
  mergedShape = Shape::Merge({transShape, shape, shape2});
  EXPECT_EQ(mergedShape->type(), Shape::Type::Append);
  auto appendShape = std::static_pointer_cast<AppendShape>(mergedShape);
  EXPECT_EQ(appendShape->shapes.size(), 3u);

  Paint paint;
  paint.setStyle(PaintStyle::Stroke);
  paint.setColor(Color::Red());
  canvas->drawShape(transShape, paint);
  auto sacaleShape = Shape::ApplyMatrix(shape, Matrix::MakeScale(1.5, 0.5));
  sacaleShape = Shape::ApplyMatrix(sacaleShape, Matrix::MakeTrans(10, 70));
  canvas->setMatrix(Matrix::MakeScale(1.5, 1.5));
  canvas->drawShape(sacaleShape, paint);

  paint.setStyle(PaintStyle::Fill);
  paint.setColor(Color::Blue());
  auto mergeShape1 = Shape::ApplyMatrix(shape, Matrix::MakeTrans(0, 60));
  mergeShape1 = Shape::Merge(mergeShape1, shape);
  mergeShape1 = Shape::ApplyMatrix(mergeShape1, Matrix::MakeTrans(100, 10));
  canvas->setMatrix(Matrix::MakeScale(1, 1));
  canvas->drawShape(mergeShape1, paint);
  paint.setColor(Color::Green());
  auto mergeShape2 = Shape::ApplyMatrix(shape, Matrix::MakeTrans(0, 30));
  mergeShape2 = Shape::Merge(mergeShape2, shape, PathOp::Intersect);
  mergeShape2 = Shape::ApplyMatrix(mergeShape2, Matrix::MakeTrans(170, 10));
  canvas->drawShape(mergeShape2, paint);

  transShape = Shape::ApplyMatrix(shape, Matrix::MakeTrans(200, 90));
  paint.setShader(Shader::MakeLinearGradient(Point{200.f, 90.f}, Point{250, 140},
                                             {Color{1.f, 0.f, 0.f, 1.f}, Color{0.f, 1.f, 0.f, 1.f}},
                                             {}));
  canvas->drawShape(transShape, paint);
  paint.setShader(nullptr);

  paint.setStyle(PaintStyle::Stroke);
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  Font font(typeface, 30.f);
  font.setFauxBold(true);
  auto textBlob = TextBlob::MakeFrom("Hello TGFX", font);
  auto textShape = Shape::MakeFrom(textBlob);
  textShape = Shape::ApplyMatrix(textShape, Matrix::MakeTrans(10, 70));
  auto matrix = Matrix::MakeRotate(10);
  matrix.preConcat(Matrix::MakeScale(2, 1));
  matrix.preConcat(Matrix::MakeTrans(0, 70));
  canvas->setMatrix(matrix);
  canvas->drawShape(textShape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/drawShape"));
}

TGFX_TEST(PathShapeTest, inverseFillType) {
  Path firstPath = {};
  firstPath.addRect(Rect::MakeXYWH(50, 50, 170, 100));
  auto firstShape = Shape::MakeFrom(firstPath);
  EXPECT_FALSE(firstShape->isInverseFillType());
  Path secondPath = {};
  secondPath.addOval(Rect::MakeXYWH(180, 50, 170, 100));
  secondPath.toggleInverseFillType();
  auto secondShape = Shape::MakeFrom(secondPath);
  EXPECT_TRUE(secondShape->isInverseFillType());
  auto shape = Shape::Merge(firstShape, secondShape, PathOp::Append);
  EXPECT_FALSE(shape->isInverseFillType());
  shape = Shape::Merge(firstShape, secondShape, PathOp::Difference);
  EXPECT_FALSE(shape->isInverseFillType());
  shape = Shape::Merge(secondShape, firstShape, PathOp::Difference);
  EXPECT_TRUE(shape->isInverseFillType());
  shape = Shape::Merge(firstShape, secondShape, PathOp::Intersect);
  EXPECT_FALSE(shape->isInverseFillType());
  shape = Shape::Merge(firstShape, secondShape, PathOp::Union);
  EXPECT_TRUE(shape->isInverseFillType());
  shape = Shape::Merge(firstShape, secondShape, PathOp::XOR);
  EXPECT_TRUE(shape->isInverseFillType());

  auto pathEffect = PathEffect::MakeCorner(10);
  shape = Shape::ApplyEffect(firstShape, pathEffect);
  EXPECT_FALSE(shape->isInverseFillType());
  shape = Shape::ApplyMatrix(firstShape, Matrix::MakeScale(2.0f));
  EXPECT_FALSE(shape->isInverseFillType());
  Stroke stroke(10);
  shape = Shape::ApplyStroke(firstShape, &stroke);
  EXPECT_FALSE(shape->isInverseFillType());

  firstShape = Shape::ApplyFillType(firstShape, PathFillType::InverseWinding);
  EXPECT_TRUE(firstShape->isInverseFillType());
  shape = Shape::Merge(firstShape, secondShape, PathOp::Append);
  EXPECT_TRUE(shape->isInverseFillType());
  shape = Shape::Merge(firstShape, secondShape, PathOp::Difference);
  EXPECT_FALSE(shape->isInverseFillType());
  shape = Shape::Merge(firstShape, secondShape, PathOp::Intersect);
  EXPECT_TRUE(shape->isInverseFillType());
  shape = Shape::Merge(firstShape, secondShape, PathOp::Union);
  EXPECT_TRUE(shape->isInverseFillType());
  shape = Shape::Merge(firstShape, secondShape, PathOp::XOR);
  EXPECT_FALSE(shape->isInverseFillType());

  shape = Shape::ApplyEffect(firstShape, pathEffect);
  EXPECT_TRUE(shape->isInverseFillType());
  shape = Shape::ApplyMatrix(firstShape, Matrix::MakeScale(2.0f));
  EXPECT_TRUE(shape->isInverseFillType());
  shape = Shape::ApplyStroke(firstShape, &stroke);
  EXPECT_TRUE(shape->isInverseFillType());
}

TGFX_TEST(PathShapeTest, MergeShapeFillType) {
  // MergeShape always produces EvenOdd fill type regardless of input fill types.
  Path rectPath;
  rectPath.addRect(Rect::MakeXYWH(0, 0, 100, 100));
  Path ovalPath;
  ovalPath.addOval(Rect::MakeXYWH(50, 50, 100, 100));

  // Winding + Winding -> EvenOdd
  auto shape1 = Shape::MakeFrom(rectPath);
  auto shape2 = Shape::MakeFrom(ovalPath);
  EXPECT_EQ(shape1->fillType(), PathFillType::Winding);
  EXPECT_EQ(shape2->fillType(), PathFillType::Winding);

  auto merged = Shape::Merge(shape1, shape2, PathOp::Union);
  EXPECT_EQ(merged->fillType(), PathFillType::EvenOdd);
  merged = Shape::Merge(shape1, shape2, PathOp::Intersect);
  EXPECT_EQ(merged->fillType(), PathFillType::EvenOdd);
  merged = Shape::Merge(shape1, shape2, PathOp::Difference);
  EXPECT_EQ(merged->fillType(), PathFillType::EvenOdd);
  merged = Shape::Merge(shape1, shape2, PathOp::XOR);
  EXPECT_EQ(merged->fillType(), PathFillType::EvenOdd);

  // InverseWinding + Winding
  auto inverseShape1 = Shape::ApplyFillType(shape1, PathFillType::InverseWinding);
  EXPECT_EQ(inverseShape1->fillType(), PathFillType::InverseWinding);

  merged = Shape::Merge(inverseShape1, shape2, PathOp::Union);
  EXPECT_EQ(merged->fillType(), PathFillType::InverseEvenOdd);
  merged = Shape::Merge(inverseShape1, shape2, PathOp::Intersect);
  EXPECT_EQ(merged->fillType(), PathFillType::EvenOdd);
  merged = Shape::Merge(inverseShape1, shape2, PathOp::Difference);
  EXPECT_EQ(merged->fillType(), PathFillType::InverseEvenOdd);
  merged = Shape::Merge(inverseShape1, shape2, PathOp::XOR);
  EXPECT_EQ(merged->fillType(), PathFillType::InverseEvenOdd);

  // Winding + InverseWinding
  auto inverseShape2 = Shape::ApplyFillType(shape2, PathFillType::InverseWinding);
  merged = Shape::Merge(shape1, inverseShape2, PathOp::Union);
  EXPECT_EQ(merged->fillType(), PathFillType::InverseEvenOdd);
  merged = Shape::Merge(shape1, inverseShape2, PathOp::Intersect);
  EXPECT_EQ(merged->fillType(), PathFillType::EvenOdd);
  merged = Shape::Merge(shape1, inverseShape2, PathOp::Difference);
  EXPECT_EQ(merged->fillType(), PathFillType::EvenOdd);
  merged = Shape::Merge(shape1, inverseShape2, PathOp::XOR);
  EXPECT_EQ(merged->fillType(), PathFillType::InverseEvenOdd);

  // InverseWinding + InverseWinding
  merged = Shape::Merge(inverseShape1, inverseShape2, PathOp::Union);
  EXPECT_EQ(merged->fillType(), PathFillType::InverseEvenOdd);
  merged = Shape::Merge(inverseShape1, inverseShape2, PathOp::Intersect);
  EXPECT_EQ(merged->fillType(), PathFillType::InverseEvenOdd);
  merged = Shape::Merge(inverseShape1, inverseShape2, PathOp::Difference);
  EXPECT_EQ(merged->fillType(), PathFillType::EvenOdd);
  merged = Shape::Merge(inverseShape1, inverseShape2, PathOp::XOR);
  EXPECT_EQ(merged->fillType(), PathFillType::EvenOdd);

  // EvenOdd inputs
  auto evenOddShape = Shape::ApplyFillType(shape1, PathFillType::EvenOdd);
  EXPECT_EQ(evenOddShape->fillType(), PathFillType::EvenOdd);
  merged = Shape::Merge(evenOddShape, shape2, PathOp::Union);
  EXPECT_EQ(merged->fillType(), PathFillType::EvenOdd);

  // Append preserves first shape's fillType
  merged = Shape::Merge(shape1, shape2, PathOp::Append);
  EXPECT_EQ(merged->fillType(), PathFillType::Winding);
  merged = Shape::Merge(inverseShape1, shape2, PathOp::Append);
  EXPECT_EQ(merged->fillType(), PathFillType::InverseWinding);
  merged = Shape::Merge(evenOddShape, shape2, PathOp::Append);
  EXPECT_EQ(merged->fillType(), PathFillType::EvenOdd);

  // ApplyFillType on MatrixShape should apply fill type to inner shape and keep matrix outside
  auto matrix = Matrix::MakeTrans(10, 20);
  auto matrixShape = Shape::ApplyMatrix(shape1, matrix);
  auto fillTypeMatrixShape = Shape::ApplyFillType(matrixShape, PathFillType::EvenOdd);
  ASSERT_TRUE(fillTypeMatrixShape != nullptr);
  EXPECT_EQ(fillTypeMatrixShape->type(), Shape::Type::Matrix);
  EXPECT_EQ(fillTypeMatrixShape->fillType(), PathFillType::EvenOdd);
  EXPECT_EQ(fillTypeMatrixShape->getBounds(), matrixShape->getBounds());
}

TGFX_TEST(PathShapeTest, ReverseShape) {
  Path path;
  path.moveTo(0, 0);
  path.lineTo(100, 0);
  path.lineTo(100, 100);
  path.close();

  auto shape = Shape::MakeFrom(path);
  ASSERT_TRUE(shape != nullptr);
  EXPECT_EQ(shape->fillType(), PathFillType::Winding);

  // Apply reverse
  auto reversedShape = Shape::ApplyReverse(shape);
  ASSERT_TRUE(reversedShape != nullptr);
  EXPECT_EQ(reversedShape->fillType(), PathFillType::Winding);
  EXPECT_EQ(reversedShape->getBounds(), shape->getBounds());

  // Reverse with inverse fill type
  auto inverseShape = Shape::ApplyFillType(shape, PathFillType::InverseWinding);
  auto reversedInverse = Shape::ApplyReverse(inverseShape);
  EXPECT_EQ(reversedInverse->fillType(), PathFillType::InverseWinding);

  // Double reverse on ReverseShape should return the inner shape
  auto reversedMatrix = Shape::ApplyReverse(shape);
  auto doubleReversed = Shape::ApplyReverse(reversedMatrix);
  EXPECT_EQ(doubleReversed, shape);

  // Reverse nullptr should return nullptr
  auto nullReversed = Shape::ApplyReverse(nullptr);
  EXPECT_EQ(nullReversed, nullptr);

  // ApplyReverse on MatrixShape should apply reverse to inner shape and keep matrix outside
  auto matrix = Matrix::MakeTrans(10, 20);
  auto matrixShape = Shape::ApplyMatrix(shape, matrix);
  auto reversedMatrixShape = Shape::ApplyReverse(matrixShape);
  ASSERT_TRUE(reversedMatrixShape != nullptr);
  EXPECT_EQ(reversedMatrixShape->type(), Shape::Type::Matrix);
  EXPECT_EQ(reversedMatrixShape->getBounds(), matrixShape->getBounds());
}

TGFX_TEST(PathShapeTest, Path_addArc) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  auto canvas = surface->getCanvas();
  Paint paint;
  paint.setColor(Color::FromRGBA(255, 0, 0, 255));
  for (int i = 1; i <= 8; ++i) {
    canvas->clear();
    Path path;
    path.addArc(Rect::MakeXYWH(50, 50, 100, 100), 0, static_cast<float>(45 * i));
    path.close();
    canvas->drawPath(path, paint);
    EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/Path_addArc" + std::to_string(i)));
  }
  for (int i = 1; i <= 8; ++i) {
    canvas->clear();
    Path path;
    path.addArc(Rect::MakeXYWH(50, 50, 100, 100), -90.f, -static_cast<float>(45 * i));
    path.close();
    canvas->drawPath(path, paint);
    EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/Path_addArc_reversed" + std::to_string(i)));
  }
}

TGFX_TEST(PathShapeTest, Path_complex) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 400);
  auto canvas = surface->getCanvas();
  canvas->translate(200, 200);
  Path path;
  auto rect = Rect::MakeLTRB(-167.200867f, -100.890869f, 167.200867f, 100.890869f);
  path.addRect(rect);
  auto strokeMatrix = Matrix::MakeAll(0.528697968f, 0, -9.44108581f, 0, 0.422670752f, -9.34423828f);
  path.transform(strokeMatrix);
  float dashList[] = {10.f, 17.f, 10.f, 10.f, 17.f, 10.f};
  auto pathEffect = PathEffect::MakeDash(dashList, 6, 0);
  pathEffect->filterPath(&path);
  auto stroke = Stroke();
  stroke.width = 8;
  stroke.cap = LineCap::Round;
  stroke.join = LineJoin::Miter;
  stroke.miterLimit = 4;
  stroke.applyToPath(&path);

  Matrix invertMatrix = {};
  strokeMatrix.invert(&invertMatrix);
  path.transform(invertMatrix);
  path.setFillType(PathFillType::Winding);
  auto shader = Shader::MakeColorShader(Color::Black());
  auto paint = Paint();
  paint.setShader(shader);

  canvas->scale(0.5f, 0.5f);
  canvas->drawPath(path, paint);

  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/Path_complex"));
}

TGFX_TEST(PathShapeTest, DrawPathProvider) {
  class DrawPathProvider : public PathProvider {
   public:
    explicit DrawPathProvider(const std::vector<Point>& pts) : points(pts) {
    }

    Path getPath() const override {
      if (points.size() < 2) {
        return {};
      }

      Path path = {};
      path.moveTo(points[0]);
      for (size_t i = 1; i < points.size(); ++i) {
        path.lineTo(points[i]);
      }
      path.close();
      return path;
    }

    Rect getBounds() const override {
      if (points.size() < 2) {
        return {};
      }

      float minX = points[0].x;
      float minY = points[0].y;
      float maxX = points[0].x;
      float maxY = points[0].y;
      for (const auto& point : points) {
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
      }
      return Rect::MakeXYWH(minX, minY, maxX - minX, maxX - minX);
    }

   private:
    std::vector<Point> points = {};
  };

  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 400);
  auto canvas = surface->getCanvas();

  Paint paint;
  std::vector<Point> pts1 = {{50, 50}, {150, 50}, {150, 150}, {50, 150}};
  auto shape1 = Shape::MakeFrom(std::make_shared<DrawPathProvider>(pts1));
  paint.setColor(Color::Red());
  paint.setStyle(PaintStyle::Stroke);
  canvas->drawShape(shape1, paint);

  std::vector<Point> pts2 = {{300, 0}, {360, 180}, {210, 60}, {390, 60}, {240, 180}};
  auto shape2 = Shape::MakeFrom(std::make_shared<DrawPathProvider>(pts2));
  paint.setColor(Color::Green());
  paint.setStyle(PaintStyle::Fill);
  canvas->drawShape(shape2, paint);

  std::vector<Point> pts3 = {{50, 250},  {250, 250}, {250, 240}, {275, 255},
                             {250, 270}, {250, 260}, {50, 260}};
  auto shape3 = Shape::MakeFrom(std::make_shared<DrawPathProvider>(pts3));
  paint.setColor(Color::Blue());
  paint.setStyle(PaintStyle::Fill);
  canvas->drawShape(shape3, paint);

  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/DrawPathProvider"));
}

TGFX_TEST(PathShapeTest, StrokeShape) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 200);
  auto canvas = surface->getCanvas();
  Paint paint = {};
  paint.setColor(Color::Black());
  auto path = Path();
  path.addRect(Rect::MakeXYWH(10, 10, 50, 50));
  auto shape = Shape::MakeFrom(path);
  Matrix matrix = Matrix::MakeScale(2.0, 2.0);
  shape = Shape::ApplyMatrix(shape, matrix);
  Stroke stroke(10);
  shape = Shape::ApplyStroke(shape, &stroke);
  canvas->drawShape(shape, paint);
  shape = Shape::ApplyMatrix(shape, Matrix::MakeScale(0.2f, 0.6f));
  canvas->translate(150, 0);
  canvas->drawShape(shape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/StrokeShape"));

  surface = Surface::Make(context, 200, 200);
  canvas = surface->getCanvas();
  path.reset();
  path.moveTo(70.0f, 190.0f);
  path.lineTo(100.0f, 74.0f);
  path.lineTo(130.0f, 190.0f);
  stroke.width = 15;
  stroke.miterLimit = 4.0f;
  stroke.join = LineJoin::Miter;
  shape = Shape::MakeFrom(path);
  shape = Shape::ApplyStroke(shape, &stroke);
  auto bounds = shape->getBounds();
  canvas->clipRect(bounds);
  stroke.applyToPath(&path);
  EXPECT_EQ(bounds.top, 44.0f);
  canvas->drawShape(shape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/StrokeShape_miter"));
}

TGFX_TEST(PathShapeTest, ClipAll) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 20, 20);
  auto canvas = surface->getCanvas();
  canvas->clipRect(Rect::MakeXYWH(0, 0, 0, 0));
  Paint paint = {};
  paint.setColor(Color::Black());
  auto path = Path();
  path.addRect(Rect::MakeXYWH(5, 5, 10, 10));
  canvas->drawPath(path, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/ClipAll"));
}

TGFX_TEST(PathShapeTest, RevertRect) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 10, 10);
  auto canvas = surface->getCanvas();
  Path path = {};
  path.addRect(5, 5, 2, 3);
  Paint paint = {};
  canvas->drawPath(path, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/RevertRect"));
}

TGFX_TEST(PathShapeTest, AdaptiveDashEffect) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 400);
  auto canvas = surface->getCanvas();
  canvas->clear();
  canvas->drawColor(Color::White());
  Paint paint = {};
  Stroke stroke(2);
  paint.setStroke(stroke);
  paint.setColor(Color::Black());
  paint.setStyle(PaintStyle::Stroke);
  Path path = {};
  path.addRect(50, 50, 250, 150);
  path.addOval(Rect::MakeXYWH(50, 200, 200, 50));
  path.moveTo(50, 300);
  path.cubicTo(100, 300, 100, 350, 150, 350);
  path.quadTo(200, 350, 200, 300);
  float dashList[] = {40.f, 50.f};
  auto effect = PathEffect::MakeDash(dashList, 2, 20, true);
  effect->filterPath(&path);
  canvas->drawPath(path, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/AdaptiveDashEffect"));
}

TGFX_TEST(PathShapeTest, TrimPathEffect) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 500, 540);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  Paint paint = {};
  paint.setStyle(PaintStyle::Stroke);
  paint.setStroke(Stroke(8));

  // ========== MakeTrim returns nullptr cases ==========
  // NaN values
  EXPECT_TRUE(PathEffect::MakeTrim(NAN, 0.5f) == nullptr);
  EXPECT_TRUE(PathEffect::MakeTrim(0.5f, NAN) == nullptr);
  EXPECT_TRUE(PathEffect::MakeTrim(NAN, NAN) == nullptr);

  // Full path coverage (end - start >= 1.0 in forward direction)
  EXPECT_TRUE(PathEffect::MakeTrim(0.0f, 1.0f) == nullptr);
  EXPECT_TRUE(PathEffect::MakeTrim(0.0f, 1.5f) == nullptr);
  EXPECT_TRUE(PathEffect::MakeTrim(-0.5f, 0.5f) == nullptr);
  EXPECT_TRUE(PathEffect::MakeTrim(0.25f, 1.25f) == nullptr);  // exactly 1.0 difference
  // Reversed full coverage still needs processing to reverse the path
  EXPECT_TRUE(PathEffect::MakeTrim(1.0f, 0.0f) != nullptr);
  EXPECT_TRUE(PathEffect::MakeTrim(0.5f, -0.5f) != nullptr);

  // ========== start == end: empty path ==========
  Path emptyPath = {};
  emptyPath.addRect(0, 0, 100, 100);
  auto emptyEffect = PathEffect::MakeTrim(0.5f, 0.5f);
  EXPECT_TRUE(emptyEffect != nullptr);
  emptyEffect->filterPath(&emptyPath);
  EXPECT_TRUE(emptyPath.isEmpty());

  // ========== Normal trim [start, end] where start < end ==========
  // Row 1: Normal forward trim on rect
  paint.setColor(Color::Blue());
  Path path1 = {};
  path1.addRect(20, 20, 120, 120);
  auto trim1 = PathEffect::MakeTrim(0.0f, 0.5f);
  trim1->filterPath(&path1);
  canvas->drawPath(path1, paint);

  paint.setColor(Color::Red());
  Path path2 = {};
  path2.addRect(140, 20, 240, 120);
  auto trim2 = PathEffect::MakeTrim(0.25f, 0.75f);
  trim2->filterPath(&path2);
  canvas->drawPath(path2, paint);

  paint.setColor(Color::Green());
  Path path3 = {};
  path3.addRect(260, 20, 360, 120);
  auto trim3 = PathEffect::MakeTrim(0.5f, 1.0f);
  trim3->filterPath(&path3);
  canvas->drawPath(path3, paint);

  // ========== Reversed trim (end < start): triggers 1-x + reverse ==========
  // Row 2: Reversed trim - path direction is reversed
  paint.setColor(Color::FromRGBA(255, 128, 0));
  Path path4 = {};
  path4.addRect(20, 140, 120, 240);
  auto trim4 = PathEffect::MakeTrim(0.5f, 0.0f);
  trim4->filterPath(&path4);
  canvas->drawPath(path4, paint);

  paint.setColor(Color::FromRGBA(128, 0, 255));
  Path path5 = {};
  path5.addRect(140, 140, 240, 240);
  auto trim5 = PathEffect::MakeTrim(0.75f, 0.25f);
  trim5->filterPath(&path5);
  canvas->drawPath(path5, paint);

  paint.setColor(Color::FromRGBA(0, 128, 128));
  Path path6 = {};
  path6.addRect(260, 140, 360, 240);
  auto trim6 = PathEffect::MakeTrim(1.0f, 0.5f);
  trim6->filterPath(&path6);
  canvas->drawPath(path6, paint);

  // ========== Values outside [0,1] requiring normalization ==========
  // Row 3: Out-of-range values
  paint.setColor(Color::FromRGBA(255, 0, 128));
  Path path7 = {};
  path7.addRect(20, 260, 120, 360);
  auto trim7 = PathEffect::MakeTrim(1.25f, 1.75f);  // same as [0.25, 0.75]
  trim7->filterPath(&path7);
  canvas->drawPath(path7, paint);

  paint.setColor(Color::FromRGBA(128, 128, 0));
  Path path8 = {};
  path8.addRect(140, 260, 240, 360);
  auto trim8 = PathEffect::MakeTrim(-0.25f, 0.25f);  // same as [0.75, 1] + [0, 0.25]
  trim8->filterPath(&path8);
  canvas->drawPath(path8, paint);

  // ========== WrapAround with seamless connection on closed path ==========
  // Row 3: Closed oval with wrap-around (forward)
  paint.setColor(Color::FromRGBA(0, 128, 255));
  Path path9 = {};
  path9.addOval(Rect::MakeXYWH(260, 260, 100, 100));
  auto trim9 = PathEffect::MakeTrim(0.75f, 1.25f);  // wraps around start point
  trim9->filterPath(&path9);
  canvas->drawPath(path9, paint);

  // Row 3: Closed rect with wrap-around reversed (tests seamless connection in reverse)
  // MakeTrim(0.25, -0.25) normalizes to [0.75, 1.0] + [0.0, 0.25] then reversed
  // With wrap-around on the reversed path, should produce seamless connection
  paint.setColor(Color::FromRGBA(255, 64, 192));
  Path path9b = {};
  path9b.addRect(380, 260, 480, 360);
  auto trim9b = PathEffect::MakeTrim(0.25f, -0.25f);  // reversed with wrap-around
  trim9b->filterPath(&path9b);
  canvas->drawPath(path9b, paint);

  // ========== Multiple contours ==========
  // Row 4: Path with multiple contours (forward)
  paint.setColor(Color::FromRGBA(64, 64, 64));
  Path multiPath = {};
  multiPath.addRect(20, 380, 80, 440);
  multiPath.addRect(100, 380, 160, 440);
  auto trimMulti = PathEffect::MakeTrim(0.0f, 0.5f);
  trimMulti->filterPath(&multiPath);
  canvas->drawPath(multiPath, paint);

  paint.setColor(Color::FromRGBA(192, 64, 64));
  Path multiPath2 = {};
  multiPath2.addRect(180, 380, 240, 440);
  multiPath2.addRect(260, 380, 320, 440);
  auto trimMulti2 = PathEffect::MakeTrim(0.25f, 0.75f);
  trimMulti2->filterPath(&multiPath2);
  canvas->drawPath(multiPath2, paint);

  // ========== Multiple contours with reversed trim ==========
  // Row 4: Path with multiple contours (reversed)
  paint.setColor(Color::FromRGBA(64, 192, 64));
  Path multiPathRev = {};
  multiPathRev.addRect(340, 380, 400, 440);
  multiPathRev.addRect(420, 380, 480, 440);
  auto trimMultiRev =
      PathEffect::MakeTrim(0.5f, 0.0f);  // reversed: same range as [0, 0.5] but reversed
  trimMultiRev->filterPath(&multiPathRev);
  canvas->drawPath(multiPathRev, paint);

  // ========== Verify multi-contour reversal order with stacked trims ==========
  // Row 5: Use two stacked trims to verify contour order is reversed
  // First trim reverses the path, second trim cuts to show only the beginning
  // If contour order is correctly reversed, the second contour (right rect) should appear first
  paint.setColor(Color::FromRGBA(255, 0, 0));
  Path stackedPath1 = {};
  stackedPath1.addRect(20, 460, 80, 520);    // First contour: left rect (perimeter = 180)
  stackedPath1.addRect(100, 460, 220, 520);  // Second contour: right rect (perimeter = 360)
  // Total length = 540, first contour is 33.3%, second is 66.7%
  // Reverse with trim(0.999, 0.001): reverses order, second contour becomes first
  auto reverseEffect = PathEffect::MakeTrim(1.0f, 0.0f);
  ASSERT_TRUE(reverseEffect != nullptr);
  reverseEffect->filterPath(&stackedPath1);
  // Now trim to keep only first 25% - should show part of the originally-second (now-first) contour
  auto cutEffect = PathEffect::MakeTrim(0.0f, 0.75f);
  cutEffect->filterPath(&stackedPath1);
  canvas->drawPath(stackedPath1, paint);

  // For comparison: same path without reversal, trim first 25%
  // Should show part of the originally-first (left) contour
  paint.setColor(Color::FromRGBA(0, 0, 255));
  Path stackedPath2 = {};
  stackedPath2.addRect(240, 460, 300, 520);  // First contour: left rect
  stackedPath2.addRect(320, 460, 440, 520);  // Second contour: right rect
  auto cutOnlyEffect = PathEffect::MakeTrim(0.0f, 0.75f);
  cutOnlyEffect->filterPath(&stackedPath2);
  canvas->drawPath(stackedPath2, paint);

  // ========== Zero length path ==========
  Path zeroPath = {};
  zeroPath.moveTo(0, 0);
  auto zeroEffect = PathEffect::MakeTrim(0.0f, 0.5f);
  zeroEffect->filterPath(&zeroPath);
  EXPECT_TRUE(zeroPath.isEmpty());

  // ========== filterPath with nullptr ==========
  auto nullEffect = PathEffect::MakeTrim(0.0f, 0.5f);
  EXPECT_FALSE(nullEffect->filterPath(nullptr));

  // ========== Preserve fill type ==========
  Path fillTypePath = {};
  fillTypePath.addRect(0, 0, 100, 100);
  fillTypePath.setFillType(PathFillType::EvenOdd);
  auto fillTypeEffect = PathEffect::MakeTrim(0.0f, 0.5f);
  fillTypeEffect->filterPath(&fillTypePath);
  EXPECT_EQ(fillTypePath.getFillType(), PathFillType::EvenOdd);

  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/TrimPathEffect"));
}

TGFX_TEST(PathShapeTest, CornerEffectCompare) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  int surfaceWidth = 800;
  int surfaceHeight = 800;
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  auto canvas = surface->getCanvas();
  Paint normalPaint;
  normalPaint.setStyle(PaintStyle::Stroke);
  normalPaint.setColor(Color::Red());
  normalPaint.setStroke(Stroke(2));
  Paint cornerPaint;
  cornerPaint.setStyle(PaintStyle::Stroke);
  cornerPaint.setColor(Color::White());
  cornerPaint.setStroke(Stroke(2));

  // rectangle
  {
    Path path;
    path.addRect(Rect::MakeWH(200, 100));
    auto effectedShape = Shape::MakeFrom(path);
    effectedShape = tgfx::Shape::ApplyEffect(effectedShape, tgfx::PathEffect::MakeCorner(50));
    canvas->translate(50, 50);
    canvas->drawPath(path, normalPaint);
    canvas->drawShape(effectedShape, cornerPaint);

    canvas->translate(300, 0);
    canvas->drawPath(path, normalPaint);
    canvas->drawRoundRect(Rect::MakeWH(200, 100), 50, 50, cornerPaint);
  }

  // isolated bezier contour
  {
    auto path = SVGPathParser::FromSVGString(
        "M63.6349 2.09663C-0.921635 70.6535 -10.5027 123.902 12.936 235.723L340.451 "
        "345.547C273.528 "
        "257.687 177.2 90.3553 327.269 123.902C514.855 165.834 165.216 -13.8778 63.6349 2.09663Z");
    auto effectedShape = Shape::MakeFrom(*path);
    effectedShape = tgfx::Shape::ApplyEffect(effectedShape, tgfx::PathEffect::MakeCorner(50));
    canvas->translate(0, 200);
    canvas->drawPath(*path, normalPaint);
    canvas->drawShape(effectedShape, cornerPaint);
  }

  // open bezier contour
  {
    auto path = SVGPathParser::FromSVGString(
        "M16.9138 155.924C-1.64829 106.216 -15.1766 1.13521 47.1166 1.13519C47.1166 143.654 "
        "144.961 "
        "149.632 150.939 226.712");
    auto effectedShape = Shape::MakeFrom(*path);
    effectedShape = tgfx::Shape::ApplyEffect(effectedShape, tgfx::PathEffect::MakeCorner(50));
    canvas->translate(-300, 0);
    canvas->drawPath(*path, normalPaint);
    canvas->drawShape(effectedShape, cornerPaint);
  }

  // two circle union
  {
    Path path1;
    path1.addOval(Rect::MakeXYWH(100, 100, 125, 125));
    Path unionPath;
    unionPath.addOval(Rect::MakeXYWH(200, 100, 125, 125));
    unionPath.addPath(path1, PathOp::Union);
    auto effectedShape = Shape::MakeFrom(unionPath);
    effectedShape = tgfx::Shape::ApplyEffect(effectedShape, tgfx::PathEffect::MakeCorner(50));
    canvas->translate(0, 300);
    canvas->drawPath(unionPath, normalPaint);
    canvas->drawShape(effectedShape, cornerPaint);
  }

  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/CornerEffectCompare"));
}

TGFX_TEST(PathShapeTest, CornerTest) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1024, 1024);
  auto canvas = surface->getCanvas();
  canvas->clear();
  Path rectPath = {};
  rectPath.addRect(Rect::MakeXYWH(50, 50, 170, 100));
  auto rectShape = Shape::MakeFrom(rectPath);
  auto pathEffect = PathEffect::MakeCorner(10);
  auto cornerRectshape = Shape::ApplyEffect(rectShape, pathEffect);

  Path trianglePath = {};
  trianglePath.moveTo(500, 500);
  trianglePath.lineTo(550, 600);
  trianglePath.lineTo(450, 600);
  trianglePath.lineTo(500, 500);
  trianglePath.close();
  auto triangleShape = Shape::MakeFrom(trianglePath);
  auto cornerTriShape = Shape::ApplyEffect(triangleShape, pathEffect);
  Paint paint;
  paint.setColor(Color(0, 0, 0));
  canvas->drawShape(cornerRectshape, paint);
  canvas->drawShape(cornerTriShape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/CornerShape"));
  canvas->clear();
  auto doubleCornerRectShape = Shape::ApplyEffect(cornerRectshape, pathEffect);
  auto doubleCornerTriShape = Shape::ApplyEffect(cornerTriShape, pathEffect);
  canvas->drawShape(doubleCornerRectShape, paint);
  canvas->drawShape(doubleCornerTriShape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/CornerShapeDouble"));
  canvas->clear();
  auto tripleCornerRectShape = Shape::ApplyEffect(doubleCornerRectShape, pathEffect);
  auto tripleCornerTriShape = Shape::ApplyEffect(doubleCornerTriShape, pathEffect);
  canvas->drawShape(tripleCornerRectShape, paint);
  canvas->drawShape(tripleCornerTriShape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/CornerShapeTriple"));

  canvas->clear();
  Path closeQuadPath = {};
  closeQuadPath.moveTo(50, 50);
  closeQuadPath.lineTo(80, 50);
  closeQuadPath.quadTo(100, 70, 80, 80);
  closeQuadPath.lineTo(80, 100);
  closeQuadPath.lineTo(50, 100);
  closeQuadPath.lineTo(50, 50);
  closeQuadPath.close();
  auto closeQuadShape = Shape::MakeFrom(closeQuadPath);
  canvas->drawShape(closeQuadShape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/CloseQuadShape"));
  canvas->clear();
  auto cornerCloseQuadShape = Shape::ApplyEffect(closeQuadShape, pathEffect);
  canvas->drawShape(cornerCloseQuadShape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/CloseQuadShapeCorner"));

  canvas->clear();
  Path openQuadPath = {};
  openQuadPath.moveTo(50, 50);
  openQuadPath.lineTo(80, 50);
  openQuadPath.quadTo(100, 70, 80, 80);
  openQuadPath.lineTo(80, 100);
  openQuadPath.lineTo(50, 100);
  auto openQuadShape = Shape::MakeFrom(openQuadPath);
  paint.setStyle(PaintStyle::Stroke);
  canvas->drawShape(openQuadShape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/OpenQuadShape"));
  canvas->clear();
  auto cornerOpenQuadShape = Shape::ApplyEffect(openQuadShape, pathEffect);
  canvas->drawShape(cornerOpenQuadShape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/OpenQuadShapeCorner"));

  canvas->clear();
  Path openConicPath = {};
  openConicPath.moveTo(50, 50);
  openConicPath.lineTo(80, 50);
  openConicPath.cubicTo(100, 50, 150, 80, 80, 80);
  openConicPath.lineTo(80, 100);
  openConicPath.lineTo(50, 100);
  auto openConicShape = Shape::MakeFrom(openConicPath);
  paint.setStyle(PaintStyle::Stroke);
  canvas->drawShape(openConicShape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/OpenConicShape"));
  canvas->clear();
  auto cornerOpenConicShape = Shape::ApplyEffect(openConicShape, pathEffect);
  canvas->drawShape(cornerOpenConicShape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/OpenConicShapeCorner"));

  canvas->clear();
  Path path = {};
  path.moveTo(50, 50);
  path.quadTo(60, 50, 220, 50);
  path.quadTo(220, 70, 220, 150);
  path.quadTo(200, 150, 50, 150);
  path.quadTo(50, 120, 50, 50);
  path.close();
  auto quadShape = Shape::MakeFrom(path);
  canvas->drawShape(quadShape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/QuadRectShape"));

  canvas->clear();
  auto cornerShape = Shape::ApplyEffect(quadShape, pathEffect);
  canvas->drawShape(cornerShape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/QuadRectShapeCorner"));
}

TGFX_TEST(PathShapeTest, RoundRectRadii) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto rect = Rect::MakeWH(250, 150);
  std::array<Point, 4> radii = {Point{20, 20}, Point{60, 60}, Point{10, 10}, Point{0, 0}};
  Path path = {};
  path.addRoundRect(rect, radii);
  auto surface = Surface::Make(context, 400, 200);
  auto canvas = surface->getCanvas();
  canvas->setMatrix(Matrix::MakeTrans(75, 25));
  Paint paint;
  paint.setColor(Color::Blue());
  paint.setStrokeWidth(10.f);
  canvas->drawPath(path, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/roundRectRadii"));

  radii[1] = {60, 20};
  Path path2 = {};
  path2.addRoundRect(rect, radii);
  paint.setColor(Color::Red());
  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(10.f);
  canvas->clear();
  canvas->drawPath(path2, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "PathShapeTest/roundRectRadiiStroke"));
}

}  // namespace tgfx

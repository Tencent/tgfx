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

#include "core/MCState.h"
#include "core/Matrix3DUtils.h"
#include "core/PictureRecords.h"
#include "core/images/SubsetImage.h"
#include "gpu/DrawingManager.h"
#include "gpu/RenderContext.h"
#include "gpu/ops/RRectDrawOp.h"
#include "gpu/ops/RectDrawOp.h"
#include "gtest/gtest.h"
#include "layers/MaskContext.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Matrix3D.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/TextBlob.h"
#include "utils/TestUtils.h"
#include "utils/common.h"

namespace tgfx {

TGFX_TEST(CanvasTest, clip) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto width = 1080;
  auto height = 1776;
  auto texture = context->gpu()->createTexture({width, height, PixelFormat::RGBA_8888});
  ASSERT_TRUE(texture != nullptr);
  auto surface = Surface::MakeFrom(context, texture->getBackendTexture(), ImageOrigin::BottomLeft);
  auto canvas = surface->getCanvas();
  canvas->clear();
  canvas->setMatrix(Matrix::MakeScale(3));
  auto clipPath = Path();
  clipPath.addRect(Rect::MakeLTRB(0, 0, 200, 300));
  auto paint = Paint();
  paint.setColor(Color::FromRGBA(0, 0, 0));
  paint.setStyle(PaintStyle::Stroke);
  paint.setStroke(Stroke(1));
  canvas->drawPath(clipPath, paint);
  canvas->clipPath(clipPath);
  auto drawPath = Path();
  drawPath.addRect(Rect::MakeLTRB(50, 295, 150, 590));
  paint.setColor(Color::FromRGBA(255, 0, 0));
  paint.setStyle(PaintStyle::Fill);
  canvas->drawPath(drawPath, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/Clip"));
}

TGFX_TEST(CanvasTest, DiscardContent) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  int width = 100;
  int height = 100;
  auto surface = Surface::Make(context, width, height);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());
  surface->renderContext->flush();
  auto drawingBuffer = context->drawingManager()->getDrawingBuffer();
  ASSERT_TRUE(drawingBuffer->renderTasks.size() == 1);
  auto task = static_cast<OpsRenderTask*>(drawingBuffer->renderTasks.front().get());
  EXPECT_TRUE(task->drawOps.size() == 0);

  Paint paint;
  paint.setColor(Color{0.8f, 0.8f, 0.8f, 0.8f});
  canvas->drawRect(Rect::MakeWH(50, 50), paint);
  paint.setBlendMode(BlendMode::Src);
  canvas->drawRect(Rect::MakeWH(width, height), paint);
  surface->renderContext->flush();
  ASSERT_TRUE(drawingBuffer->renderTasks.size() == 2);
  task = static_cast<OpsRenderTask*>(drawingBuffer->renderTasks.back().get());
  EXPECT_TRUE(task->drawOps.size() == 0);

  paint.setColor(Color{0.8f, 0.8f, 0.8f, 1.f});
  canvas->drawRect(Rect::MakeWH(50, 50), paint);
  paint.setBlendMode(BlendMode::SrcOver);
  paint.setShader(Shader::MakeLinearGradient(
      Point{0.f, 0.f}, Point{static_cast<float>(width), static_cast<float>(height)},
      {Color{0.f, 1.f, 0.f, 1.f}, Color{0.f, 0.f, 0.f, 1.f}}, {}));
  canvas->drawPaint(paint);
  surface->renderContext->flush();
  ASSERT_TRUE(drawingBuffer->renderTasks.size() == 3);
  task = static_cast<OpsRenderTask*>(drawingBuffer->renderTasks.back().get());
  EXPECT_TRUE(task->drawOps.size() == 1);
  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/DiscardContent"));
}

TGFX_TEST(CanvasTest, merge_draw_call_rect) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  int width = 72;
  int height = 72;
  auto surface = Surface::Make(context, width, height);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());
  Paint paint;
  paint.setColor(Color{0.8f, 0.8f, 0.8f, 1.f});
  auto lumaColorFilter = ColorFilter::Matrix(lumaColorMatrix);
  paint.setColorFilter(lumaColorFilter);
  int tileSize = 8;
  size_t drawCallCount = 0;
  for (int y = 0; y < height; y += tileSize) {
    bool draw = (y / tileSize) % 2 == 1;
    for (int x = 0; x < width; x += tileSize) {
      if (draw) {
        auto rect = Rect::MakeXYWH(static_cast<float>(x), static_cast<float>(y),
                                   static_cast<float>(tileSize), static_cast<float>(tileSize));
        canvas->drawRect(rect, paint);
        drawCallCount++;
      }
      draw = !draw;
    }
  }
  surface->renderContext->flush();
  auto drawingBuffer = context->drawingManager()->getDrawingBuffer();
  EXPECT_TRUE(drawingBuffer->renderTasks.size() == 1);
  auto task = static_cast<OpsRenderTask*>(drawingBuffer->renderTasks.front().get());
  ASSERT_TRUE(task->drawOps.size() == 1);
  EXPECT_EQ(static_cast<RectDrawOp*>(task->drawOps.back().get())->rectCount, drawCallCount);
  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/merge_draw_call_rect"));
}

TGFX_TEST(CanvasTest, merge_draw_call_rrect) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  int width = 72;
  int height = 72;
  auto surface = Surface::Make(context, width, height);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());
  Paint paint;
  paint.setShader(Shader::MakeLinearGradient(
      Point{0.f, 0.f}, Point{static_cast<float>(width), static_cast<float>(height)},
      {Color{0.f, 1.f, 0.f, 1.f}, Color{0.f, 0.f, 0.f, 1.f}}, {}));
  int tileSize = 8;
  size_t drawCallCount = 0;
  for (int y = 0; y < height; y += tileSize) {
    bool draw = (y / tileSize) % 2 == 1;
    for (int x = 0; x < width; x += tileSize) {
      if (draw) {
        auto rect = Rect::MakeXYWH(static_cast<float>(x), static_cast<float>(y),
                                   static_cast<float>(tileSize), static_cast<float>(tileSize));
        Path path;
        auto radius = static_cast<float>(tileSize) / 4.f;
        path.addRoundRect(rect, radius, radius);
        canvas->drawPath(path, paint);
        drawCallCount++;
      }
      draw = !draw;
    }
  }
  surface->renderContext->flush();
  auto drawingBuffer = context->drawingManager()->getDrawingBuffer();
  EXPECT_TRUE(drawingBuffer->renderTasks.size() == 1);
  auto task = static_cast<OpsRenderTask*>(drawingBuffer->renderTasks.front().get());
  ASSERT_TRUE(task->drawOps.size() == 1);
  // AA RRects use RRectDrawOp (EllipseGeometryProcessor).
  EXPECT_EQ(static_cast<RRectDrawOp*>(task->drawOps.back().get())->rectCount, drawCallCount);
  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/merge_draw_call_rrect"));
}

TGFX_TEST(CanvasTest, drawPaint) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 160, 160);
  auto canvas = surface->getCanvas();
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  Font font(typeface, 50.f);
  font.setFauxBold(true);
  auto textBlob = TextBlob::MakeFrom("TGFX", font);
  auto textShape = Shape::MakeFrom(textBlob);
  ASSERT_TRUE(textShape != nullptr);
  auto path = textShape->getPath();
  path.transform(Matrix::MakeTrans(10, 100));
  canvas->clear(Color::Red());
  canvas->save();
  canvas->clipPath(path);
  canvas->drawColor(Color::Red(), BlendMode::DstOut);
  canvas->restore();
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/drawColor"));
  canvas->clear();
  Paint paint = {};
  auto shader = Shader::MakeRadialGradient({100, 100}, 100, {Color::Green(), Color::Blue()}, {});
  paint.setShader(shader);
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);
  auto maskShader = Shader::MakeImageShader(std::move(image), TileMode::Decal, TileMode::Decal);
  auto maskFilter = MaskFilter::MakeShader(std::move(maskShader));
  maskFilter = maskFilter->makeWithMatrix(Matrix::MakeTrans(45, 45));
  paint.setMaskFilter(std::move(maskFilter));
  canvas->translate(-20, -20);
  canvas->drawPaint(paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/drawPaint"));
  canvas->clear();
  path.reset();
  path.toggleInverseFillType();
  auto imageFilter = ImageFilter::DropShadow(-10, -10, 10, 10, Color::Black());
  paint.setImageFilter(imageFilter);
  canvas->drawPath(path, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/drawPaint_shadow"));
}

TGFX_TEST(CanvasTest, saveLayer) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto width = 600;
  auto height = 500;
  auto surface = Surface::Make(context, width, height);
  auto canvas = surface->getCanvas();
  auto saveCount = canvas->saveLayerAlpha(0.8f);
  Paint layerPaint = {};
  layerPaint.setImageFilter(ImageFilter::Blur(30, 30));
  canvas->saveLayer(&layerPaint);
  Paint paint = {};
  paint.setColor(Color::Red());
  auto rect = Rect::MakeXYWH(50, 50, 100, 100);
  canvas->drawRoundRect(rect, 30, 30, paint);
  canvas->restoreToCount(saveCount);
  auto dropShadowFilter = ImageFilter::DropShadow(10, 10, 20, 20, Color::Black());
  paint.setImageFilter(dropShadowFilter);
  paint.setColor(Color::Green());
  canvas->drawRect(Rect::MakeXYWH(200, 50, 100, 100), paint);
  paint.setStrokeWidth(20);
  canvas->drawLine(350, 50, 400, 150, paint);
  canvas->drawRoundRect(Rect::MakeXYWH(450, 50, 100, 100), 30, 30, paint);
  canvas->drawCircle(100, 250, 50, paint);
  canvas->drawOval(Rect::MakeXYWH(200, 200, 150, 100), paint);
  Path path = {};
  path.addArc({0, 0, 150, 100}, 0, 180);
  canvas->translate(400, 180);
  paint.setStyle(PaintStyle::Stroke);
  canvas->drawPath(path, paint);
  paint.setStyle(PaintStyle::Fill);
  canvas->resetMatrix();
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  Font font(typeface, 30.f);
  font.setFauxBold(true);
  paint.setAntiAlias(false);
  canvas->drawSimpleText("Hello TGFX", 50, 400, font, paint);
  paint.setAntiAlias(true);
  auto atlas = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(atlas != nullptr);
  Matrix matrix[2] = {Matrix::I(), Matrix::MakeTrans(150, 0)};
  Rect rects[2] = {Rect::MakeXYWH(0, 0, 110, 50), Rect::MakeXYWH(0, 60, 110, 50)};
  canvas->translate(280, 360);
  canvas->drawAtlas(atlas, matrix, rects, nullptr, 2, {}, &paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/saveLayer"));
}

TGFX_TEST(CanvasTest, NothingToDraw) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 100, 100);
  auto canvas = surface->getCanvas();
  Paint paint;
  paint.setColor(Color::FromRGBA(255, 0, 0, 255));
  canvas->drawRect(Rect::MakeXYWH(0, 0, 50, 50), paint);
  paint.setColor(Color::FromRGBA(0, 0, 0, 0));
  canvas->drawRect(Rect::MakeXYWH(0, 0, 20, 20), paint);
  paint.setColor(Color::FromRGBA(0, 0, 0, 127));
  canvas->drawRect(Rect::MakeXYWH(20, 20, 20, 20), paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/NothingToDraw"));
}

TGFX_TEST(CanvasTest, Picture) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  PictureRecorder recorder = {};
  auto canvas = recorder.beginRecording();
  EXPECT_TRUE(recorder.getRecordingCanvas() != nullptr);
  Path path = {};
  path.addOval(Rect::MakeXYWH(0, 0, 200, 150));
  Paint paint = {};
  paint.setColor(Color::Blue());
  paint.setAlpha(0.8f);
  paint.setBlendMode(BlendMode::Multiply);
  canvas->drawPath(path, paint);
  paint.setBlendMode(BlendMode::SrcOver);
  paint.setAlpha(1.0f);
  auto singleRecordPicture = recorder.finishRecordingAsPicture();
  ASSERT_TRUE(singleRecordPicture != nullptr);
  EXPECT_TRUE(recorder.getRecordingCanvas() == nullptr);

  auto image = MakeImage("resources/apitest/rotation.jpg");
  ASSERT_TRUE(image != nullptr);
  canvas = recorder.beginRecording();
  image = image->makeMipmapped(true);
  auto imageScale = 200.f / static_cast<float>(image->width());
  canvas->scale(imageScale, imageScale);
  canvas->drawImage(image);
  canvas->resetMatrix();
  canvas->translate(200, 0);
  paint.setColor(Color::White());
  canvas->drawRect(Rect::MakeXYWH(10, 10, 100, 100), paint);
  canvas->translate(150, 0);
  path.reset();
  path.addRoundRect(Rect::MakeXYWH(10, 10, 100, 100), 10, 10);
  paint.setColor(Color::Green());
  canvas->drawPath(path, paint);
  path.reset();
  path.addRect(Rect::MakeXYWH(0, 0, 100, 100));
  Matrix matrix = {};
  matrix.postRotate(30, 50, 50);
  path.transform(matrix);
  canvas->resetMatrix();
  canvas->save();
  canvas->translate(450, 150);
  canvas->clipRect(Rect::MakeXYWH(0, 0, 100, 100));
  canvas->drawPath(path, paint);
  canvas->restore();
  canvas->translate(200, 350);
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  Font font(typeface, 50.f);
  font.setFauxBold(true);
  paint.setColor(Color::Red());
  canvas->drawSimpleText("Hello TGFX~", 0, 0, font, paint);
  paint.setColor(Color::White());
  paint.setStyle(PaintStyle::Stroke);
  paint.setStroke(Stroke(1));
  canvas->drawSimpleText("Hello TGFX~", 0, 0, font, paint);
  auto picture = recorder.finishRecordingAsPicture();
  ASSERT_TRUE(picture != nullptr);

  int width = 550;
  int height = 352;
  auto surface = Surface::Make(context, width, height + 20);
  canvas = surface->getCanvas();
  path.reset();
  path.addOval(Rect::MakeWH(width, height + 100));
  canvas->clipPath(path);
  canvas->translate(0, 10);
  canvas->drawPicture(picture);
  canvas->translate(0, static_cast<float>(height + 10));
  paint.setBlendMode(BlendMode::Screen);
  paint.setAlpha(0.8f);
  matrix = Matrix::MakeTrans(0, -180);
  canvas->drawPicture(singleRecordPicture, &matrix, &paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/Picture"));

  image = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(image != nullptr);
  canvas = recorder.beginRecording();
  canvas->drawImage(image);
  auto singleImageRecord = recorder.finishRecordingAsPicture();
  auto pictureImage = Image::MakeFrom(singleImageRecord, image->width(), image->height());
  EXPECT_TRUE(pictureImage == image);
  pictureImage = Image::MakeFrom(singleImageRecord, 200, 150);
  ASSERT_TRUE(pictureImage != nullptr);
  EXPECT_FALSE(pictureImage == image);

  canvas = recorder.beginRecording();
  canvas->clipRect(Rect::MakeXYWH(100, 100, image->width() - 200, image->height() - 200));
  canvas->drawImage(image);
  singleImageRecord = recorder.finishRecordingAsPicture();
  canvas = recorder.beginRecording();
  auto imageFilter = ImageFilter::Blur(10, 10);
  paint.setImageFilter(imageFilter);
  canvas->drawPicture(singleImageRecord, nullptr, &paint);
  paint.setImageFilter(nullptr);
  auto imagePicture = recorder.finishRecordingAsPicture();
  ASSERT_TRUE(imagePicture != nullptr);
  ASSERT_TRUE(imagePicture->drawCount == 1);
  EXPECT_EQ(imagePicture->getFirstDrawRecord()->type(), PictureRecordType::DrawImage);

  surface = Surface::Make(context, image->width() - 200, image->height() - 200);
  canvas = surface->getCanvas();
  canvas->translate(-100, -100);
  canvas->drawPicture(imagePicture);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/PictureImage"));

  matrix = Matrix::MakeTrans(-100, -100);
  pictureImage =
      Image::MakeFrom(singleImageRecord, image->width() - 200, image->height() - 200, &matrix);
  ASSERT_TRUE(pictureImage != nullptr);
  auto subsetImage = std::static_pointer_cast<SubsetImage>(pictureImage);
  EXPECT_TRUE(subsetImage->source == image);
  EXPECT_EQ(singleImageRecord.use_count(), 1);
  pictureImage =
      Image::MakeFrom(singleImageRecord, image->width() - 100, image->height() - 100, &matrix);
  EXPECT_EQ(singleImageRecord.use_count(), 2);
  EXPECT_FALSE(pictureImage == image);
  pictureImage = Image::MakeFrom(singleImageRecord, image->width() - 100, image->height() - 100);
  EXPECT_FALSE(pictureImage == image);
  EXPECT_EQ(singleImageRecord.use_count(), 2);

  canvas = recorder.beginRecording();
  canvas->scale(0.5f, 0.5f);
  canvas->clipRect(Rect::MakeXYWH(100, 100, image->width(), image->height()));
  canvas->drawImage(image, 100, 100);
  singleImageRecord = recorder.finishRecordingAsPicture();
  matrix = Matrix::MakeScale(2.0f);
  matrix.postTranslate(-100, -100);
  pictureImage = Image::MakeFrom(singleImageRecord, image->width(), image->height(), &matrix);
  EXPECT_TRUE(pictureImage == image);

  canvas = recorder.beginRecording();
  paint.reset();
  auto textBlob = TextBlob::MakeFrom("Hello TGFX~", font);
  canvas->drawTextBlob(textBlob, 0, 0, paint);
  auto textRecord = recorder.finishRecordingAsPicture();
  auto bounds = textBlob->getTightBounds();
  matrix = Matrix::MakeTrans(-bounds.left, -bounds.top);
  auto textImage = Image::MakeFrom(textRecord, static_cast<int>(bounds.width()),
                                   static_cast<int>(bounds.height()), &matrix);
  EXPECT_EQ(textRecord.use_count(), 2);
  ASSERT_TRUE(textImage != nullptr);

  surface = Surface::Make(context, textImage->width(), textImage->height());
  canvas = surface->getCanvas();
  canvas->drawImage(textImage);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/PictureImage_Text"));

  canvas = recorder.beginRecording();
  path.reset();
  path.addRect(Rect::MakeXYWH(0, 0, 100, 100));
  matrix.reset();
  matrix.postRotate(30, 50, 50);
  path.transform(matrix);
  canvas->drawPath(path, paint);
  auto patRecord = recorder.finishRecordingAsPicture();
  bounds = patRecord->getBounds();
  matrix = Matrix::MakeTrans(-bounds.left, -bounds.top);
  width = static_cast<int>(bounds.width());
  height = static_cast<int>(bounds.height());
  auto pathImage = Image::MakeFrom(patRecord, width, height, &matrix);
  EXPECT_EQ(patRecord.use_count(), 2);
  ASSERT_TRUE(pathImage != nullptr);

  surface = Surface::Make(context, pathImage->width(), pathImage->height());
  canvas = surface->getCanvas();
  canvas->drawImage(pathImage);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/PictureImage_Path"));
}

TGFX_TEST(CanvasTest, PictureImageShaderOptimization) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto image = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(image != nullptr);

  // Test 1: Rect filled with ImageShader (should be optimized to asImage)
  PictureRecorder recorder;
  auto canvas = recorder.beginRecording();
  auto shader = Shader::MakeImageShader(image);
  Paint paint;
  paint.setShader(shader);
  auto rect = Rect::MakeWH(image->width(), image->height());
  canvas->drawRect(rect, paint);
  auto shaderPicture = recorder.finishRecordingAsPicture();
  ASSERT_TRUE(shaderPicture != nullptr);
  EXPECT_EQ(shaderPicture->drawCount, 1u);

  // Should be optimized to return the original image
  Point offset = {};
  auto extractedImage = shaderPicture->asImage(&offset);
  ASSERT_TRUE(extractedImage != nullptr);
  EXPECT_TRUE(extractedImage == image);
  EXPECT_EQ(offset.x, 0.0f);
  EXPECT_EQ(offset.y, 0.0f);

  // Test 2: Rect with ImageShader but different size (should fail optimization)
  canvas = recorder.beginRecording();
  paint.setShader(shader);
  rect = Rect::MakeWH(image->width() / 2, image->height() / 2);
  canvas->drawRect(rect, paint);
  shaderPicture = recorder.finishRecordingAsPicture();
  extractedImage = shaderPicture->asImage(&offset);
  EXPECT_TRUE(extractedImage == nullptr);

  // Test 3: Rect with ImageShader but non-zero origin (should fail optimization)
  canvas = recorder.beginRecording();
  paint.setShader(shader);
  rect = Rect::MakeXYWH(10, 10, image->width(), image->height());
  canvas->drawRect(rect, paint);
  shaderPicture = recorder.finishRecordingAsPicture();
  extractedImage = shaderPicture->asImage(&offset);
  EXPECT_TRUE(extractedImage == nullptr);

  // Test 4: Rect with ImageShader that has TileMode::Repeat (should fail optimization)
  canvas = recorder.beginRecording();
  shader = Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Repeat);
  paint.setShader(shader);
  rect = Rect::MakeWH(image->width(), image->height());
  canvas->drawRect(rect, paint);
  shaderPicture = recorder.finishRecordingAsPicture();
  extractedImage = shaderPicture->asImage(&offset);
  EXPECT_TRUE(extractedImage == nullptr);

  // Test 5: Rect with ImageShader and clip (should be optimized with subset)
  canvas = recorder.beginRecording();
  shader = Shader::MakeImageShader(image);
  paint.setShader(shader);
  canvas->clipRect(Rect::MakeXYWH(100, 100, image->width() - 200, image->height() - 200));
  rect = Rect::MakeWH(image->width(), image->height());
  canvas->drawRect(rect, paint);
  shaderPicture = recorder.finishRecordingAsPicture();
  auto matrix = Matrix::MakeTrans(-100, -100);
  ISize clipSize = {image->width() - 200, image->height() - 200};
  extractedImage = shaderPicture->asImage(&offset, &matrix, &clipSize);
  ASSERT_TRUE(extractedImage != nullptr);
  auto subsetImage = std::static_pointer_cast<SubsetImage>(extractedImage);
  EXPECT_TRUE(subsetImage->source == image);
  EXPECT_EQ(offset.x, 0.0f);
  EXPECT_EQ(offset.y, 0.0f);
}

TGFX_TEST(CanvasTest, BlendModeTest) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  auto padding = 30;
  auto scale = 1.f;
  auto offset = static_cast<float>(padding + image->width()) * scale;

  BlendMode blendModes[] = {BlendMode::SrcOver,    BlendMode::Darken,      BlendMode::Multiply,
                            BlendMode::PlusDarker, BlendMode::ColorBurn,   BlendMode::Lighten,
                            BlendMode::Screen,     BlendMode::PlusLighter, BlendMode::ColorDodge,
                            BlendMode::Overlay,    BlendMode::SoftLight,   BlendMode::HardLight,
                            BlendMode::Difference, BlendMode::Exclusion,   BlendMode::Hue,
                            BlendMode::Saturation, BlendMode::Color,       BlendMode::Luminosity};

  auto surfaceHeight = (static_cast<float>(padding + image->height())) * scale *
                       ceil(sizeof(blendModes) / sizeof(BlendMode) / 4.0f) * 2;

  auto surface = Surface::Make(context, static_cast<int>(offset * 4),
                               static_cast<int>(surfaceHeight), false, 4);
  auto canvas = surface->getCanvas();

  Paint backPaint;
  backPaint.setColor(Color::FromRGBA(82, 117, 132, 255));
  backPaint.setStyle(PaintStyle::Fill);
  canvas->drawRect(Rect::MakeWH(surface->width(), surface->height()), backPaint);

  for (auto& blendMode : blendModes) {
    Paint paint;
    paint.setBlendMode(blendMode);
    paint.setAntiAlias(true);
    canvas->save();
    canvas->concat(Matrix::MakeScale(scale));
    canvas->drawImage(image, &paint);
    canvas->restore();
    canvas->concat(Matrix::MakeTrans(offset, 0));
    if (canvas->getMatrix().getTranslateX() + static_cast<float>(image->width()) * scale >
        static_cast<float>(surface->width())) {
      canvas->translate(-canvas->getMatrix().getTranslateX(),
                        static_cast<float>(image->height() + padding) * scale);
    }
  }

  Rect bounds = Rect::MakeWH(static_cast<float>(image->width()) * scale,
                             static_cast<float>(image->height()) * scale);

  canvas->translate(-canvas->getMatrix().getTranslateX(),
                    static_cast<float>(image->height() + padding) * scale);

  for (auto blendMode : blendModes) {
    Paint paint;
    paint.setBlendMode(blendMode);
    paint.setStyle(PaintStyle::Fill);
    paint.setColor(Color::FromRGBA(255, 14, 14, 255));
    canvas->drawRect(bounds, paint);
    canvas->concat(Matrix::MakeTrans(offset, 0));
    if (canvas->getMatrix().getTranslateX() + static_cast<float>(image->width()) * scale >
        static_cast<float>(surface->width())) {
      canvas->translate(-canvas->getMatrix().getTranslateX(),
                        static_cast<float>(image->height() + padding) * scale);
    }
  }
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/blendMode"));
}

TGFX_TEST(CanvasTest, BlendFormula) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200 * (1 + static_cast<int>(BlendMode::Screen)), 600);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::FromRGBA(100, 100, 100, 128));
  Path texturePath = {};
  texturePath.addRect(50, 50, 150, 150);
  texturePath.moveTo(50, 50);
  texturePath.lineTo(150, 50);
  texturePath.lineTo(150, 170);
  texturePath.lineTo(50, 120);
  texturePath.lineTo(100, 170);
  for (int i = 0; i < 100; ++i) {
    // make sure the path will be rasterized as coverage
    texturePath.lineTo(90 + i, 50 + i);
  }

  Path trianglePath = {};
  trianglePath.addRect(50, 250, 150, 350);
  trianglePath.transform(Matrix::MakeRotate(1));

  for (int i = 0; i < 100; ++i) {
    // make sure the path will be rasterized as coverage
    texturePath.lineTo(90 + i, 50 + i);
  }
  Paint strokePaint = {};
  strokePaint.setColor(Color::FromRGBA(255, 0, 0, 128));
  strokePaint.setStyle(PaintStyle::Stroke);
  strokePaint.setStroke(Stroke(10));
  Paint fillPaint = {};
  fillPaint.setColor(Color::FromRGBA(255, 0, 0, 128));
  for (int i = 0; i <= static_cast<int>(BlendMode::Screen); ++i) {
    strokePaint.setBlendMode(static_cast<BlendMode>(i));
    canvas->drawPath(texturePath, strokePaint);

    fillPaint.setBlendMode(static_cast<BlendMode>(i));
    canvas->drawPath(trianglePath, fillPaint);

    // rect is not rasterized as coverage
    canvas->drawRect(Rect::MakeXYWH(25, 400, 150, 150), fillPaint);
    canvas->translate(200, 0);
  }
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/BlendFormula"));
}

TGFX_TEST(CanvasTest, ShadowBoundIntersect) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 400);
  auto canvas = surface->getCanvas();

  PictureRecorder shadowRecorder = {};
  auto picCanvas = shadowRecorder.beginRecording();
  Paint dropShadowPaint = {};
  dropShadowPaint.setImageFilter(ImageFilter::DropShadowOnly(0, -8.f, .5f, .5f, Color::Red()));
  picCanvas->saveLayer(&dropShadowPaint);
  picCanvas->translate(2.2f, 2.2f);
  picCanvas->drawRect(Rect::MakeWH(150, 8), Paint());
  picCanvas->restore();
  auto picture = shadowRecorder.finishRecordingAsPicture();
  auto image = Image::MakeFrom(picture, 150, 150);

  canvas->clipRect(Rect::MakeXYWH(0.f, 4.f, 80.f, 3.7f));
  canvas->translate(0.7f, 0.7f);
  canvas->drawImage(image);
  context->flushAndSubmit();
}

TGFX_TEST(CanvasTest, MultiImageRect_SameView) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  int surfaceWidth = 1563;
  int surfaceHeight = 1563;
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto image = MakeImage("resources/assets/GenMesh.png");
  int meshNumH = 5;
  int meshNumV = 5;
  float meshWidth = image->width() / meshNumH;
  float meshHeight = image->height() / meshNumV;
  float scale = 0.9f;
  Paint paint;
  paint.setAntiAlias(false);
  SamplingOptions options;
  options.magFilterMode = FilterMode::Linear;
  options.minFilterMode = FilterMode::Linear;
  for (int i = 0; i < meshNumH; i++) {
    for (int j = 0; j < meshNumV; j++) {
      Rect srcRect = Rect::MakeXYWH(i * meshWidth, j * meshHeight, meshWidth, meshHeight);
      Rect dstRect = Rect::MakeXYWH(i * meshWidth * scale, j * meshHeight * scale,
                                    meshWidth * scale, meshHeight * scale);
      canvas->drawImageRect(image, srcRect, dstRect, options, &paint, SrcRectConstraint::Fast);
    }
  }
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/MultiImageRect_SameView"));
}

TGFX_TEST(CanvasTest, SingleImageRect) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  int surfaceWidth = 1563;
  int surfaceHeight = 1563;
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto image = MakeImage("resources/assets/HappyNewYear.png");
  float scale = 5.211f;
  Rect srcRect = Rect::MakeXYWH(256, 256, 256, 256);
  Rect dstRect = Rect::MakeXYWH(0.0f, 0.0f, srcRect.width() * scale, srcRect.height() * scale);
  SamplingOptions options;
  options.magFilterMode = FilterMode::Linear;
  options.minFilterMode = FilterMode::Linear;
  Paint paint;
  paint.setAntiAlias(false);
  canvas->drawImageRect(image, srcRect, dstRect, options, &paint, SrcRectConstraint::Strict);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/SingleImageRect1"));
  canvas->clear();
  auto mipmapImage = image->makeMipmapped(true);
  options.magFilterMode = FilterMode::Linear;
  options.minFilterMode = FilterMode::Linear;
  options.mipmapMode = MipmapMode::Nearest;
  scale = 0.3f;
  dstRect = Rect::MakeXYWH(0.0f, 0.0f, srcRect.width() * scale, srcRect.height() * scale);
  canvas->drawImageRect(mipmapImage, srcRect, dstRect, options, &paint, SrcRectConstraint::Strict);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/SingleImageRectWithMipmap"));
}

TGFX_TEST(CanvasTest, MultiImageRect_SCALE_LINEAR) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  int surfaceWidth = 1563;
  int surfaceHeight = 1563;
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto image = MakeImage("resources/assets/HappyNewYear.png");
  auto mipmapImage = image->makeMipmapped(true);
  float scale = 0.9f;
  Paint paint;
  paint.setAntiAlias(false);
  constexpr int meshNumH = 4;
  constexpr int meshNumV = 4;
  float meshWidth = image->width() / meshNumH;
  float meshHeight = image->height() / meshNumV;
  SamplingOptions options;
  options.magFilterMode = FilterMode::Linear;
  options.minFilterMode = FilterMode::Linear;
  options.mipmapMode = MipmapMode::None;
  Point offsets[meshNumV][meshNumH] = {
      {{meshWidth, meshHeight}, {meshWidth, 0.0f}, {0.0f, meshHeight * 2}, {meshWidth * 3, 0.0f}},
      {{0.0f, meshHeight},
       {0.0f, 0.0f},
       {meshWidth * 2, meshHeight * 3},
       {meshWidth * 3, meshHeight}},
      {{0.0f, meshHeight * 3},
       {meshWidth * 3, meshHeight * 2},
       {meshWidth * 2, meshHeight * 2},
       {meshWidth * 2, 0.0f}},
      {{meshWidth * 2, meshHeight},
       {meshWidth, meshHeight * 3},
       {meshWidth, meshHeight * 2},
       {meshWidth * 3, meshHeight * 3}}};
  for (int i = 0; i < meshNumH; i++)
    for (int j = 0; j < meshNumV; j++) {
      Rect srcRect = Rect::MakeXYWH(i * meshWidth, j * meshHeight, meshWidth, meshHeight);
      Rect dstRect = Rect::MakeXYWH(offsets[j][i].x * scale, offsets[j][i].y * scale,
                                    meshWidth * scale, meshHeight * scale);
      canvas->drawImageRect(mipmapImage, srcRect, dstRect, options, &paint,
                            SrcRectConstraint::Strict);
    }
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/MultiImageRect_SCALE_LINEAR_NONE1"));
  canvas->clear();
  options.mipmapMode = MipmapMode::Linear;
  for (int i = 0; i < meshNumH; i++)
    for (int j = 0; j < meshNumV; j++) {
      Rect srcRect = Rect::MakeXYWH(i * meshWidth, j * meshHeight, meshWidth, meshHeight);
      Rect dstRect = Rect::MakeXYWH(offsets[j][i].x * scale, offsets[j][i].y * scale,
                                    meshWidth * scale, meshHeight * scale);
      canvas->drawImageRect(mipmapImage, srcRect, dstRect, options, &paint,
                            SrcRectConstraint::Strict);
    }
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/MultiImageRect_SCALE_LINEAR_LINEAR1"));
  canvas->clear();
  options.mipmapMode = MipmapMode::Nearest;
  for (int i = 0; i < meshNumH; i++)
    for (int j = 0; j < meshNumV; j++) {
      Rect srcRect = Rect::MakeXYWH(i * meshWidth, j * meshHeight, meshWidth, meshHeight);
      Rect dstRect = Rect::MakeXYWH(offsets[j][i].x * scale, offsets[j][i].y * scale,
                                    meshWidth * scale, meshHeight * scale);
      canvas->drawImageRect(mipmapImage, srcRect, dstRect, options, &paint,
                            SrcRectConstraint::Strict);
    }
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/MultiImageRect_SCALE_LINEAR_NEAREST1"));
}

TGFX_TEST(CanvasTest, MultiImageRect_NOSCALE_NEAREST) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  int surfaceWidth = 1024;
  int surfaceHeight = 1024;
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto image = MakeImage("resources/assets/HappyNewYear.png");
  auto mipmapImage = image->makeMipmapped(true);
  Paint paint;
  paint.setAntiAlias(false);
  constexpr int meshNumH = 4;
  constexpr int meshNumV = 4;
  float meshWidth = image->width() / meshNumH;
  float meshHeight = image->height() / meshNumV;
  SamplingOptions options;
  options.magFilterMode = FilterMode::Nearest;
  options.minFilterMode = FilterMode::Nearest;
  options.mipmapMode = MipmapMode::None;
  Point offsets[meshNumV][meshNumH] = {
      {{meshWidth, meshHeight}, {meshWidth, 0.0f}, {0.0f, meshHeight * 2}, {meshWidth * 3, 0.0f}},
      {{0.0f, meshHeight},
       {0.0f, 0.0f},
       {meshWidth * 2, meshHeight * 3},
       {meshWidth * 3, meshHeight}},
      {{0.0f, meshHeight * 3},
       {meshWidth * 3, meshHeight * 2},
       {meshWidth * 2, meshHeight * 2},
       {meshWidth * 2, 0.0f}},
      {{meshWidth * 2, meshHeight},
       {meshWidth, meshHeight * 3},
       {meshWidth, meshHeight * 2},
       {meshWidth * 3, meshHeight * 3}}};
  for (int i = 0; i < meshNumH; i++)
    for (int j = 0; j < meshNumV; j++) {
      Rect srcRect = Rect::MakeXYWH(i * meshWidth, j * meshHeight, meshWidth, meshHeight);
      Rect dstRect = Rect::MakeXYWH(offsets[j][i].x, offsets[j][i].y, meshWidth, meshHeight);
      canvas->drawImageRect(mipmapImage, srcRect, dstRect, options, &paint,
                            SrcRectConstraint::Strict);
    }
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/MultiImageRect_NOSCALE_NEAREST_NONE"));

  canvas->clear();
  options.mipmapMode = MipmapMode::Linear;
  for (int i = 0; i < meshNumH; i++)
    for (int j = 0; j < meshNumV; j++) {
      Rect srcRect = Rect::MakeXYWH(i * meshWidth, j * meshHeight, meshWidth, meshHeight);
      Rect dstRect = Rect::MakeXYWH(offsets[j][i].x, offsets[j][i].y, meshWidth, meshHeight);
      canvas->drawImageRect(mipmapImage, srcRect, dstRect, options, &paint,
                            SrcRectConstraint::Strict);
    }
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/MultiImageRect_NOSCALE_NEAREST_LINEAR"));

  canvas->clear();
  options.mipmapMode = MipmapMode::Nearest;
  for (int i = 0; i < meshNumH; i++)
    for (int j = 0; j < meshNumV; j++) {
      Rect srcRect = Rect::MakeXYWH(i * meshWidth, j * meshHeight, meshWidth, meshHeight);
      Rect dstRect = Rect::MakeXYWH(offsets[j][i].x, offsets[j][i].y, meshWidth, meshHeight);
      canvas->drawImageRect(mipmapImage, srcRect, dstRect, options, &paint,
                            SrcRectConstraint::Strict);
    }
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/MultiImageRect_NOSCALE_NEAREST_NEAREST"));
}

TGFX_TEST(CanvasTest, RRectBlendMode) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());
  Paint paint;
  paint.setColor(Color::FromRGBA(255, 0, 0, 255));
  paint.setBlendMode(BlendMode::Darken);
  auto path = Path();
  path.addRoundRect(Rect::MakeXYWH(25, 25, 150, 150), 20, 20);
  canvas->drawPath(path, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/RRectBlendMode"));
}

TGFX_TEST(CanvasTest, MatrixShapeStroke) {
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
  paint.setStroke(Stroke(1.0f));

  auto path = Path();
  path.addRoundRect(Rect::MakeXYWH(0, 0, 8, 8), 2, 2);
  auto shape = Shape::MakeFrom(path);
  shape = Shape::ApplyMatrix(shape, Matrix::MakeScale(20, 20));
  canvas->translate(20, 20);
  canvas->drawShape(shape, paint);

  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/MatrixShapeStroke"));
}

TGFX_TEST(CanvasTest, uninvertibleStateMatrix) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 128, 128);
  auto canvas = surface->getCanvas();

  auto path = Path();
  path.addRect(-5.f, -5.f, 10.f, 10.f);

  Paint paint;
  paint.setStyle(PaintStyle::Stroke);
  paint.setStroke(Stroke(0.f));

  auto matrix = Matrix::MakeScale(1E-8f, 1E-8f);
  EXPECT_TRUE(matrix.invertNonIdentity(nullptr));
  EXPECT_FALSE(matrix.invertible());

  canvas->concat(matrix);
  canvas->drawPath(path, paint);
}

TGFX_TEST(CanvasTest, FlushSemaphore) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 128, 128);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());
  BackendSemaphore backendSemaphore = {};
  context->flush(&backendSemaphore);
  EXPECT_TRUE(backendSemaphore.isInitialized());
  auto semaphore = context->gpu()->importBackendSemaphore(backendSemaphore);
  EXPECT_TRUE(semaphore != nullptr);
}

TGFX_TEST(CanvasTest, ScaleMatrixShader) {
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 100, 100);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  auto paint = Paint();
  auto shader = Shader::MakeImageShader(image);
  auto rect = Rect::MakeXYWH(25, 25, 50, 50);
  rect.scale(10, 10);
  shader = shader->makeWithMatrix(Matrix::MakeScale(10, 10));
  paint.setShader(shader);
  canvas->scale(0.1f, 0.1f);
  canvas->drawRect(rect, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/ScaleMatrixShader"));
}

TGFX_TEST(CanvasTest, Matrix3DShapeStroke) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 300);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  auto origin = Point::Make(100, 100);
  auto originTranslateMatrix = Matrix3D::MakeTranslate(origin.x, origin.y, 0.f);
  Size pathSize = {100, 100};
  auto anchor = Point::Make(0.5f, 0.5f);
  auto invOffsetToAnchorMatrix =
      Matrix3D::MakeTranslate(anchor.x * pathSize.width, anchor.y * pathSize.height, 0);
  auto perspectiveMatrix = Matrix3D::I();
  constexpr float eyeDistance = 1200.f;
  constexpr float farZ = -1000.f;
  constexpr float shift = 10.f;
  const float nearZ = eyeDistance - shift;
  const float m22 = (2 - (farZ + nearZ) / eyeDistance) / (farZ - nearZ);
  perspectiveMatrix.setRowColumn(2, 2, m22);
  const float m23 = -1.f + nearZ / eyeDistance - perspectiveMatrix.getRowColumn(2, 2) * nearZ;
  perspectiveMatrix.setRowColumn(2, 3, m23);
  perspectiveMatrix.setRowColumn(3, 2, -1.f / eyeDistance);
  auto modelMatrix = Matrix3D::MakeScale(2, 2, 1);
  modelMatrix.postRotate({0, 0, 1}, 45);
  modelMatrix.postRotate({1, 0, 0}, 45);
  modelMatrix.postRotate({0, 1, 0}, 45);
  modelMatrix.postTranslate(0, 0, -20);
  auto offsetToAnchorMatrix =
      Matrix3D::MakeTranslate(-anchor.x * pathSize.width, -anchor.y * pathSize.height, 0);
  auto transform = originTranslateMatrix * invOffsetToAnchorMatrix * perspectiveMatrix *
                   modelMatrix * offsetToAnchorMatrix;

  auto path = Path();
  path.addRoundRect(Rect::MakeXYWH(0.f, 0.f, pathSize.width, pathSize.height), 20, 20);
  auto rawShape = Shape::MakeFrom(path);

  Paint paint1;
  paint1.setAntiAlias(true);
  paint1.setColor(Color::FromRGBA(0, 255, 0, 255));
  paint1.setStyle(PaintStyle::Fill);
  canvas->save();
  canvas->concat(Matrix3DUtils::GetMayLossyMatrix(transform));
  canvas->drawShape(rawShape, paint1);
  canvas->restore();

  auto mappedShape = Shape::ApplyMatrix3D(rawShape, transform);
  Paint paint2;
  paint2.setAntiAlias(true);
  paint2.setColor(Color::FromRGBA(255, 0, 0, 255));
  paint2.setStyle(PaintStyle::Stroke);
  paint2.setStroke(Stroke(2.0f));
  canvas->drawShape(mappedShape, paint2);

  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/Matrix3DShapeStroke"));
}

TGFX_TEST(CanvasTest, LumaFilter) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 3024, 4032);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  Paint paint{};
  paint.setColorFilter(ColorFilter::Luma());
  auto shader = Shader::MakeColorShader(Color::FromRGBA(125, 0, 255));
  paint.setShader(shader);
  canvas->drawPaint(paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/LumaFilterToSRGB"));
  ColorMatrix33 matrix{};
  NamedPrimaries::Rec601.toXYZD50(&matrix);
  surface = Surface::Make(context, 3024, 4032, false, 1, false, 0,
                          ColorSpace::MakeRGB(NamedTransferFunction::Rec601, matrix));
  ASSERT_TRUE(surface != nullptr);
  canvas = surface->getCanvas();
  canvas->drawPaint(paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/LumaFilterToRec601"));
  NamedPrimaries::Rec2020.toXYZD50(&matrix);
  surface = Surface::Make(context, 3024, 4032, false, 1, false, 0,
                          ColorSpace::MakeRGB(NamedTransferFunction::Rec2020, matrix));
  ASSERT_TRUE(surface != nullptr);
  canvas = surface->getCanvas();
  canvas->drawPaint(paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/LumaFilterToRec2020"));
}

TGFX_TEST(CanvasTest, ConvertColorSpace) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface =
      Surface::Make(context, 1024, 1024, false, 1, false, 0, ColorSpace::SRGB()->makeColorSpin());
  auto canvas = surface->getCanvas();
  const TransferFunction tfs[] = {
      NamedTransferFunction::SRGB,
      NamedTransferFunction::TwoDotTwo,
      NamedTransferFunction::Linear,
      NamedTransferFunction::Rec2020,
      {-3.0f, 2.0f, 2.0f, 1 / 0.17883277f, 0.28466892f, 0.55991073f, 3.0f}};

  const ColorMatrix33 gamuts[] = {NamedGamut::SRGB, NamedGamut::AdobeRGB, NamedGamut::DisplayP3,
                                  NamedGamut::Rec2020, NamedGamut::XYZ};
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  const int width = image->width();
  const int height = image->height();
  int tfNum = sizeof(tfs) / sizeof(TransferFunction);
  int gamutNum = sizeof(gamuts) / sizeof(ColorMatrix33);
  for (int i = 0; i < tfNum; i++) {
    for (int j = 0; j < gamutNum; j++) {
      auto midCS = ColorSpace::MakeRGB(tfs[i], gamuts[j]);
      auto offscreen = Surface::Make(context, width, height, false, 1, false, 0, midCS);
      offscreen->getCanvas()->drawImage(image);
      canvas->drawImage(offscreen->makeImageSnapshot(), static_cast<float>(i * width),
                        static_cast<float>(j * height));
    }
  }
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/ConvertColorSpace"));
}

TGFX_TEST(CanvasTest, ColorSpace) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1024, 1024, false, 1, false, 0, ColorSpace::DisplayP3());
  auto canvas = surface->getCanvas();
  canvas->drawColor(Color::FromRGBA(0, 255, 0, 255, ColorSpace::DisplayP3()), BlendMode::SrcOver);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/DrawP3ColorToP3"));
  canvas->clear();
  Paint paint;
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  auto imageShader = Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Repeat);
  paint.setShader(imageShader);
  canvas->drawPaint(paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/DrawImageShaderToP3"));
  canvas->clear();
  auto colorShader = Shader::MakeColorShader(Color::Green());
  paint.setShader(colorShader);
  canvas->drawPaint(paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/DrawSRGBColorShaderToP3"));
  canvas->clear();
  auto linearGradient = Shader::MakeLinearGradient(Point::Make(0, 0), Point::Make(1024, 0),
                                                   {Color::Green(), Color::Red()}, {});
  paint.setShader(linearGradient);
  canvas->drawPaint(paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/DrawSRGBLinearShaderToP3"));
  canvas->clear();
  auto conicGradient =
      Shader::MakeConicGradient(Point::Make(512, 512), 0, 360, {Color::Green(), Color::Red()}, {});
  paint.setShader(conicGradient);
  canvas->drawPaint(paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/DrawSRGBConicShaderToP3"));
  canvas->clear();
  auto diamondGradient =
      Shader::MakeDiamondGradient(Point::Make(512, 512), 500, {Color::Green(), Color::Red()}, {});
  paint.setShader(diamondGradient);
  canvas->drawPaint(paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/DrawSRGBDiamondShaderToP3"));
  canvas->clear();
  auto radialGradient =
      Shader::MakeRadialGradient(Point::Make(512, 512), 500, {Color::Green(), Color::Red()}, {});
  paint.setShader(radialGradient);
  canvas->drawPaint(paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/DrawSRGBRadialShaderToP3"));
  canvas->clear();
  auto blendFilter = ColorFilter::Blend(Color::FromRGBA(0, 0, 125, 125), BlendMode::SrcOver);
  paint.setColorFilter(blendFilter);
  canvas->drawPaint(paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/DrawSRGBBlendFilterToP3"));
  canvas->clear();
  auto image1 = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image1 != nullptr);
  auto maskShader = Shader::MakeImageShader(std::move(image1), TileMode::Decal, TileMode::Decal);
  auto maskFilter = MaskFilter::MakeShader(std::move(maskShader));
  maskFilter = maskFilter->makeWithMatrix(Matrix::MakeTrans(462, 462));
  paint.setMaskFilter(maskFilter);
  auto imageFilter = ImageFilter::DropShadow(-10, -10, 10, 10, Color::Green());
  paint.setImageFilter(imageFilter);
  canvas->drawPaint(paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/DrawSRGBDropShadowFilterToP3"));
  canvas->clear();
  PictureRecorder record;
  auto recordCanvas = record.beginRecording();
  recordCanvas->drawColor(Color::Green(), BlendMode::SrcOver);
  auto picture = record.finishRecordingAsPicture();
  canvas->drawPicture(picture);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/DrawRecordSRGBColorToP3UseDrawPicture"));
  canvas->clear();
  auto pictureImage = Image::MakeFrom(picture, 1024, 1024);
  canvas->drawImage(pictureImage);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/DrawRecordSRGBColorToP3UseDrawImage"));
}

TGFX_TEST(CanvasTest, ScalePictureImage) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/rotation.jpg");
  PictureRecorder recorder;
  auto canvas = recorder.beginRecording();
  auto filter = ImageFilter::DropShadow(10, 10, 0, 0, Color::Black());
  auto paint = Paint();
  paint.setImageFilter(filter);
  canvas->clipRect(Rect::MakeLTRB(100, 100, 600, 800));
  canvas->scale(0.15f, 0.15f);
  canvas->drawImage(image, 0, 0, &paint);
  auto picture = recorder.finishRecordingAsPicture();
  auto bounds = picture->getBounds();
  bounds.roundOut();
  auto pictureMatrix = Matrix::MakeTrans(-bounds.left, -bounds.top);
  image = Image::MakeFrom(picture, static_cast<int>(bounds.width()),
                          static_cast<int>(bounds.height()), &pictureMatrix);
  auto surface = Surface::Make(context, 1100, 1400);
  canvas = surface->getCanvas();
  canvas->drawImage(image);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/pic_scaled_image_origin"));
  auto scaledImage = ScaleImage(image, 0.55f);
  canvas->clear();
  canvas->drawImage(scaledImage);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/pic_scaled_image"));
  canvas->clear();
  SamplingOptions sampling(FilterMode::Linear, MipmapMode::Linear);
  scaledImage = ScaleImage(scaledImage, 2.f, sampling);
  EXPECT_EQ(scaledImage->width(), 400);
  EXPECT_EQ(scaledImage->height(), 566);
  canvas->drawImage(scaledImage);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/pic_scaled_scale_up"));
  canvas->clear();
  canvas->clipRect(Rect::MakeXYWH(100, 100, 500, 500));
  canvas->drawImage(scaledImage);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/pic_scaled_pic_clip"));
}

TGFX_TEST(CanvasTest, ScaleTest) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 250, 250);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  EXPECT_TRUE(image != nullptr);
  auto subsetImage = image->makeSubset(Rect::MakeXYWH(20, 20, 50, 50));
  EXPECT_TRUE(subsetImage != nullptr);
  auto scaledImage = ScaleImage(subsetImage, 0.9f);
  EXPECT_TRUE(scaledImage != nullptr);
  EXPECT_TRUE(scaledImage->type() == Image::Type::Subset);
  canvas->drawImage(scaledImage, 10, 10);
  scaledImage = ScaleImage(subsetImage, 0.51f);
  EXPECT_TRUE(scaledImage != nullptr);
  EXPECT_TRUE(scaledImage->type() == Image::Type::Scaled);
  canvas->drawImage(scaledImage, 70, 10);
  image = MakeImage("resources/apitest/rgbaaa.png");
  EXPECT_TRUE(image != nullptr);
  image = image->makeRGBAAA(512, 512, 512, 0);
  image = image->makeSubset(Rect::MakeXYWH(20, 20, 300, 300));
  EXPECT_TRUE(image != nullptr);
  auto scaledImage2 = ScaleImage(image, 0.25f);
  EXPECT_TRUE(scaledImage2 != nullptr);
  EXPECT_TRUE(scaledImage2->type() == Image::Type::RGBAAA);
  canvas->drawImage(scaledImage2, 10, 100);
  scaledImage2 = ScaleImage(image, 0.3f);
  EXPECT_TRUE(scaledImage2 != nullptr);
  EXPECT_TRUE(scaledImage2->type() == Image::Type::Scaled);
  canvas->drawImage(scaledImage2, 150, 100);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/ScaleTest"));
}

TGFX_TEST(CanvasTest, PictureMaskPath) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);

  // Helper lambda to extract mask path from picture
  auto getMaskPath = [](const std::shared_ptr<Picture>& picture, Path* maskPath) -> bool {
    return MaskContext::GetMaskPath(picture, maskPath);
  };

  // Test 1: Simple rect - should return valid mask path
  PictureRecorder recorder = {};
  auto canvas = recorder.beginRecording();
  Paint paint = {};
  paint.setColor(Color::White());
  canvas->drawRect(Rect::MakeXYWH(10.f, 20.f, 80.f, 60.f), paint);
  auto picture = recorder.finishRecordingAsPicture();
  ASSERT_TRUE(picture != nullptr);

  Path maskPath = {};
  EXPECT_TRUE(getMaskPath(picture, &maskPath));
  EXPECT_EQ(maskPath.getBounds(), Rect::MakeXYWH(10.f, 20.f, 80.f, 60.f));

  // Test 2: RRect - should return valid mask path
  canvas = recorder.beginRecording();
  RRect rrect = {};
  rrect.setRectXY(Rect::MakeWH(100.f, 80.f), 10.f, 10.f);
  canvas->drawRRect(rrect, paint);
  picture = recorder.finishRecordingAsPicture();
  EXPECT_TRUE(getMaskPath(picture, &maskPath));
  EXPECT_EQ(maskPath.getBounds(), Rect::MakeWH(100.f, 80.f));

  // Test 3: Path - should return valid mask path
  canvas = recorder.beginRecording();
  Path circlePath = {};
  circlePath.addOval(Rect::MakeWH(80.f, 80.f));
  canvas->drawPath(circlePath, paint);
  picture = recorder.finishRecordingAsPicture();
  EXPECT_TRUE(getMaskPath(picture, &maskPath));
  EXPECT_EQ(maskPath.getBounds(), Rect::MakeWH(80.f, 80.f));

  // Test 4: With matrix transformation
  canvas = recorder.beginRecording();
  canvas->translate(20.f, 30.f);
  canvas->drawRect(Rect::MakeWH(50.f, 40.f), paint);
  picture = recorder.finishRecordingAsPicture();
  EXPECT_TRUE(getMaskPath(picture, &maskPath));
  EXPECT_EQ(maskPath.getBounds(), Rect::MakeXYWH(20.f, 30.f, 50.f, 40.f));

  // Test 5: With clip - path should be clipped
  canvas = recorder.beginRecording();
  canvas->clipRect(Rect::MakeWH(60.f, 60.f));
  canvas->drawRect(Rect::MakeWH(100.f, 100.f), paint);
  picture = recorder.finishRecordingAsPicture();
  EXPECT_TRUE(getMaskPath(picture, &maskPath));
  EXPECT_EQ(maskPath.getBounds(), Rect::MakeWH(60.f, 60.f));

  // Test 6: Semi-transparent color - should NOT return mask path
  canvas = recorder.beginRecording();
  paint.setAlpha(0.5f);
  canvas->drawRect(Rect::MakeWH(100.f, 100.f), paint);
  picture = recorder.finishRecordingAsPicture();
  EXPECT_FALSE(getMaskPath(picture, &maskPath));
  paint.setAlpha(1.0f);

  // Test 7: With color filter - should NOT return mask path
  canvas = recorder.beginRecording();
  paint.setColorFilter(ColorFilter::Blend(Color::Red(), BlendMode::Multiply));
  canvas->drawRect(Rect::MakeWH(100.f, 100.f), paint);
  picture = recorder.finishRecordingAsPicture();
  EXPECT_FALSE(getMaskPath(picture, &maskPath));
  paint.setColorFilter(nullptr);

  // Test 8: With mask filter - should NOT return mask path
  canvas = recorder.beginRecording();
  auto maskShader = Shader::MakeColorShader(Color::White());
  paint.setMaskFilter(MaskFilter::MakeShader(maskShader));
  canvas->drawRect(Rect::MakeWH(100.f, 100.f), paint);
  picture = recorder.finishRecordingAsPicture();
  EXPECT_FALSE(getMaskPath(picture, &maskPath));
  paint.setMaskFilter(nullptr);

  // Test 9: Draw image - should NOT return mask path
  canvas = recorder.beginRecording();
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  canvas->drawImage(image);
  picture = recorder.finishRecordingAsPicture();
  EXPECT_FALSE(getMaskPath(picture, &maskPath));

  // Test 10: Inverse fill path - should return mask path
  canvas = recorder.beginRecording();
  Path inversePath = {};
  inversePath.addRect(Rect::MakeWH(50.f, 50.f));
  inversePath.addRect(Rect::MakeLTRB(10.f, 10.f, 60.f, 60.f));
  inversePath.setFillType(PathFillType::InverseWinding);
  ASSERT_TRUE(inversePath.isInverseFillType());
  canvas->drawPath(inversePath, paint);
  picture = recorder.finishRecordingAsPicture();
  EXPECT_TRUE(getMaskPath(picture, &maskPath));

  // Test 11: Multiple draws - should combine paths
  canvas = recorder.beginRecording();
  canvas->drawRect(Rect::MakeXYWH(0.f, 0.f, 50.f, 50.f), paint);
  canvas->drawRect(Rect::MakeXYWH(60.f, 60.f, 50.f, 50.f), paint);
  picture = recorder.finishRecordingAsPicture();
  EXPECT_TRUE(getMaskPath(picture, &maskPath));
  EXPECT_EQ(maskPath.getBounds(), Rect::MakeWH(110.f, 110.f));

  // Test 12: Transparent draw - should abort
  canvas = recorder.beginRecording();
  paint.setAlpha(0.5f);
  canvas->drawRect(Rect::MakeWH(100.f, 100.f), paint);
  picture = recorder.finishRecordingAsPicture();
  EXPECT_FALSE(getMaskPath(picture, &maskPath));

  // Test 13: With stroke
  paint.setAlpha(1.0f);
  canvas = recorder.beginRecording();
  paint.setStyle(PaintStyle::Stroke);
  paint.setStroke(Stroke(10.0f));
  canvas->drawRect(Rect::MakeWH(80.f, 80.f), paint);
  picture = recorder.finishRecordingAsPicture();
  EXPECT_TRUE(getMaskPath(picture, &maskPath));
  EXPECT_EQ(maskPath.getBounds(), Rect::MakeXYWH(-5, -5, 90, 90));

  // Verify by reading pixels - draw the mask path and check pixel coverage
  paint.reset();
  paint.setColor(Color::Red());
  surface->getCanvas()->clear();
  surface->getCanvas()->drawPath(maskPath, paint);

  Bitmap bitmap = {};
  bitmap.allocPixels(200, 200);
  auto pixels = bitmap.lockPixels();
  ASSERT_TRUE(surface->readPixels(bitmap.info(), pixels));
  bitmap.unlockPixels();

  // Check that pixel at (2, 2) is red (inside the stroke area, near top-left corner)
  auto colorStroke = bitmap.getColor(2, 2);
  EXPECT_EQ(colorStroke, Color::Red());

  // Check that pixel at (40, 40) is transparent (inside the rect, outside the stroke area)
  auto colorCenter = bitmap.getColor(40, 40);
  EXPECT_EQ(colorCenter, Color::Transparent());

  // Check that pixel at (100, 100) is transparent (outside the stroke bounds)
  auto colorOutside = bitmap.getColor(100, 100);
  EXPECT_EQ(colorOutside, Color::Transparent());
}

TGFX_TEST(CanvasTest, DrawImage) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto image = MakeImage("resources/apitest/imageReplacement.jpg");
  ASSERT_TRUE(image != nullptr);
  auto imageWidth = static_cast<float>(image->width());
  auto imageHeight = static_cast<float>(image->height());
  auto offsetX = 50.0f;
  auto padding = 25.0f;

  auto surfaceWidth = static_cast<int>(offsetX + imageWidth + padding);
  auto surfaceHeight = static_cast<int>(padding + imageHeight + padding + imageHeight + padding);
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto maskShader = Shader::MakeLinearGradient(Point{0, 0}, Point{imageWidth, 0},
                                               {Color::White(), Color::Transparent()}, {});
  Paint paint;
  paint.setMaskFilter(MaskFilter::MakeShader(maskShader));

  // Top: use offset parameter, mask should stay in canvas coordinate
  canvas->drawImage(image, offsetX, padding, &paint);
  // Bottom: use canvas matrix, mask moves with the image
  canvas->save();
  canvas->translate(offsetX, padding + imageHeight + padding);
  canvas->drawImage(image, &paint);
  canvas->restore();

  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/DrawImage"));
}

TGFX_TEST(CanvasTest, DrawTextBlob) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  ASSERT_TRUE(typeface != nullptr);
  auto font = Font(typeface, 50);
  auto textBlob = TextBlob::MakeFrom("TGFX", font);
  ASSERT_TRUE(textBlob != nullptr);
  auto textBounds = textBlob->getTightBounds();
  auto textWidth = textBounds.width();
  auto textHeight = textBounds.height();
  auto offsetX = 50.0f;
  auto padding = 25.0f;

  auto surfaceWidth = static_cast<int>(offsetX + textWidth + padding);
  auto surfaceHeight = static_cast<int>(padding + textHeight + padding + textHeight + padding);
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto gradientShader = Shader::MakeLinearGradient(Point{0, 0}, Point{textWidth, 0},
                                                   {Color::Red(), Color::Blue()}, {});
  Paint paint;
  paint.setShader(gradientShader);

  // Top: use offset parameter, shader should stay in canvas coordinate
  canvas->drawTextBlob(textBlob, offsetX, padding - textBounds.top, paint);
  // Bottom: use canvas matrix, shader moves with the text
  canvas->save();
  canvas->translate(offsetX, padding + textHeight + padding - textBounds.top);
  canvas->drawTextBlob(textBlob, 0, 0, paint);
  canvas->restore();

  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/DrawTextBlob"));
}

TGFX_TEST(CanvasTest, CMYKWithoutICCProfile) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/mandrill_128.jpg");
  auto surface = Surface::Make(context, image->width(), image->height());
  auto canvas = surface->getCanvas();
  canvas->drawImage(image);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/CMYKWithoutICCProfile"));
}

TGFX_TEST(CanvasTest, NonAARRectOp) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 500);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  // NonAARRectOp is used when antiAlias is false and no stroke.
  Paint paint;
  paint.setAntiAlias(false);

  // Single filled RRect with uniform radii.
  paint.setColor(Color::Red());
  RRect rrect1 = {};
  rrect1.setRectXY(Rect::MakeXYWH(50, 50, 120, 80), 15, 15);
  canvas->drawRRect(rrect1, paint);

  // Different colors and radii.
  paint.setColor(Color::Green());
  RRect rrect2 = {};
  rrect2.setRectXY(Rect::MakeXYWH(200, 50, 150, 100), 30, 20);
  canvas->drawRRect(rrect2, paint);

  // Ellipse-like (large corner radii).
  paint.setColor(Color::Blue());
  RRect rrect3 = {};
  rrect3.setRectXY(Rect::MakeXYWH(50, 160, 100, 80), 50, 40);
  canvas->drawRRect(rrect3, paint);

  // Small corner radii.
  paint.setColor(Color::FromRGBA(255, 165, 0, 255));
  RRect rrect4 = {};
  rrect4.setRectXY(Rect::MakeXYWH(200, 160, 150, 100), 5, 5);
  canvas->drawRRect(rrect4, paint);

  // With transformation - rotation.
  canvas->save();
  canvas->translate(100, 350);
  canvas->rotate(15);
  paint.setColor(Color::FromRGBA(128, 0, 128, 255));
  RRect rrect5 = {};
  rrect5.setRectXY(Rect::MakeXYWH(-50, -30, 100, 60), 10, 10);
  canvas->drawRRect(rrect5, paint);
  canvas->restore();

  // With transformation - scale.
  canvas->save();
  canvas->translate(280, 350);
  canvas->scale(1.5f, 0.8f);
  paint.setColor(Color::FromRGBA(0, 128, 128, 255));
  RRect rrect6 = {};
  rrect6.setRectXY(Rect::MakeXYWH(-40, -25, 80, 50), 12, 12);
  canvas->drawRRect(rrect6, paint);
  canvas->restore();

  // Verify RRectDrawOp with non-AA is used by checking the Op type.
  surface->renderContext->flush();
  auto drawingBuffer = context->drawingManager()->getDrawingBuffer();
  ASSERT_TRUE(drawingBuffer->renderTasks.size() >= 1);
  auto task = static_cast<OpsRenderTask*>(drawingBuffer->renderTasks.front().get());
  ASSERT_TRUE(task->drawOps.size() >= 1);
  // All non-AA filled RRects should be batched into RRectDrawOp.
  EXPECT_EQ(task->drawOps.back().get()->type(), DrawOp::Type::RRectDrawOp);

  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/NonAARRectOp"));
}

TGFX_TEST(CanvasTest, NonAARRectOpWithShader) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 350);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  // Test NonAARRectOp with image shader to verify UV coordinates are correct.
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);
  auto shader = Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Repeat);

  // Draw two RRects without AA.
  // Both use device coordinates for UV, so textures should tile continuously.
  Paint paint;
  paint.setAntiAlias(false);
  paint.setShader(shader);
  RRect rrect = {};
  rrect.setRectXY(Rect::MakeXYWH(50, 50, 200, 120), 30, 30);
  canvas->drawRRect(rrect, paint);

  // Bottom: Also non-AA
  Paint paint2;
  paint2.setAntiAlias(false);
  paint2.setShader(shader);
  RRect rrect2 = {};
  rrect2.setRectXY(Rect::MakeXYWH(50, 180, 200, 120), 30, 30);
  canvas->drawRRect(rrect2, paint2);

  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/NonAARRectOpWithShader"));
}

TGFX_TEST(CanvasTest, NonAARRectOpStroke) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 500, 400);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  Paint paint;
  paint.setAntiAlias(false);
  paint.setStyle(PaintStyle::Stroke);

  // Draw stroked RRects with various stroke widths and corner radii.
  // Top row: different stroke widths with same corner radius.
  paint.setColor(Color::Red());
  paint.setStroke(Stroke(4));
  RRect rrect1 = {};
  rrect1.setRectXY(Rect::MakeXYWH(50, 50, 100, 80), 20, 20);
  canvas->drawRRect(rrect1, paint);

  paint.setColor(Color::Green());
  paint.setStroke(Stroke(8));
  RRect rrect2 = {};
  rrect2.setRectXY(Rect::MakeXYWH(180, 50, 100, 80), 20, 20);
  canvas->drawRRect(rrect2, paint);

  paint.setColor(Color::Blue());
  paint.setStroke(Stroke(16));
  RRect rrect3 = {};
  rrect3.setRectXY(Rect::MakeXYWH(310, 50, 100, 80), 20, 20);
  canvas->drawRRect(rrect3, paint);

  // Middle row: different corner radii with same stroke width.
  paint.setColor(Color::FromRGBA(255, 128, 0));
  paint.setStroke(Stroke(8));
  RRect rrect4 = {};
  rrect4.setRectXY(Rect::MakeXYWH(50, 180, 100, 80), 10, 10);
  canvas->drawRRect(rrect4, paint);

  paint.setColor(Color::FromRGBA(128, 0, 255));
  RRect rrect5 = {};
  rrect5.setRectXY(Rect::MakeXYWH(180, 180, 100, 80), 30, 30);
  canvas->drawRRect(rrect5, paint);

  paint.setColor(Color::FromRGBA(0, 128, 128));
  RRect rrect6 = {};
  rrect6.setRectXY(Rect::MakeXYWH(310, 180, 100, 80), 50, 40);
  canvas->drawRRect(rrect6, paint);

  // Bottom: stroke that covers entire corner (thick stroke with small radius).
  paint.setColor(Color::FromRGBA(128, 128, 0));
  paint.setStroke(Stroke(20));
  RRect rrect7 = {};
  rrect7.setRectXY(Rect::MakeXYWH(100, 300, 150, 60), 10, 10);
  canvas->drawRRect(rrect7, paint);

  // Bottom right: stroke on a plain rect (no corner radius).
  paint.setColor(Color::FromRGBA(0, 64, 128));
  paint.setStroke(Stroke(6));
  RRect rrect8 = {};
  rrect8.setRectXY(Rect::MakeXYWH(300, 300, 120, 60), 0, 0);
  canvas->drawRRect(rrect8, paint);

  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/NonAARRectOpStroke"));
}

}  // namespace tgfx

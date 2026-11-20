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

#include "core/PictureRecords.h"
#include "core/images/SubsetImage.h"
#include "gpu/ops/ShapeDrawOp.h"
#include "gpu/tasks/OpsRenderTask.h"
#include "gtest/gtest.h"
#include "layers/MaskContext.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/TextBlob.h"
#include "utils/TestUtils.h"

namespace tgfx {
TGFX_TEST(PictureTest, General) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "PictureTest/General"));

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
  TGFX_PRIVATE_ACCESS(
      ASSERT_TRUE(imagePicture->drawCount == 1);
      EXPECT_EQ(imagePicture->getFirstDrawRecord()->type(), PictureRecordType::DrawImage));

  surface = Surface::Make(context, image->width() - 200, image->height() - 200);
  canvas = surface->getCanvas();
  canvas->translate(-100, -100);
  canvas->drawPicture(imagePicture);
  EXPECT_TRUE(Baseline::Compare(surface, "PictureTest/Image"));

  matrix = Matrix::MakeTrans(-100, -100);
  pictureImage =
      Image::MakeFrom(singleImageRecord, image->width() - 200, image->height() - 200, &matrix);
  ASSERT_TRUE(pictureImage != nullptr);
  TGFX_PRIVATE_ACCESS(auto subsetImage = std::static_pointer_cast<SubsetImage>(pictureImage);
                      EXPECT_TRUE(subsetImage->source == image));
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
  EXPECT_TRUE(Baseline::Compare(surface, "PictureTest/Image_Text"));

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
  EXPECT_TRUE(Baseline::Compare(surface, "PictureTest/Image_Path"));
}

TGFX_TEST(PictureTest, ImageShaderOptimization) {
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
  TGFX_PRIVATE_ACCESS(EXPECT_EQ(shaderPicture->drawCount, 1u));

  // Should be optimized to return the original image
  Point offset = {};
  TGFX_PRIVATE_ACCESS(auto extractedImage = shaderPicture->asImage(&offset);
                      ASSERT_TRUE(extractedImage != nullptr); EXPECT_TRUE(extractedImage == image);
                      EXPECT_EQ(offset.x, 0.0f); EXPECT_EQ(offset.y, 0.0f));

  // Test 2: Rect with ImageShader but different size (should fail optimization)
  canvas = recorder.beginRecording();
  paint.setShader(shader);
  rect = Rect::MakeWH(image->width() / 2, image->height() / 2);
  canvas->drawRect(rect, paint);
  shaderPicture = recorder.finishRecordingAsPicture();
  TGFX_PRIVATE_ACCESS(extractedImage = shaderPicture->asImage(&offset);
                      EXPECT_TRUE(extractedImage == nullptr));

  // Test 3: Rect with ImageShader but non-zero origin (should fail optimization)
  canvas = recorder.beginRecording();
  paint.setShader(shader);
  rect = Rect::MakeXYWH(10, 10, image->width(), image->height());
  canvas->drawRect(rect, paint);
  shaderPicture = recorder.finishRecordingAsPicture();
  TGFX_PRIVATE_ACCESS(extractedImage = shaderPicture->asImage(&offset);
                      EXPECT_TRUE(extractedImage == nullptr));

  // Test 4: Rect with ImageShader that has TileMode::Repeat (should fail optimization)
  canvas = recorder.beginRecording();
  shader = Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Repeat);
  paint.setShader(shader);
  rect = Rect::MakeWH(image->width(), image->height());
  canvas->drawRect(rect, paint);
  shaderPicture = recorder.finishRecordingAsPicture();
  TGFX_PRIVATE_ACCESS(extractedImage = shaderPicture->asImage(&offset);
                      EXPECT_TRUE(extractedImage == nullptr));

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
  TGFX_PRIVATE_ACCESS(extractedImage = shaderPicture->asImage(&offset, &matrix, &clipSize);
                      ASSERT_TRUE(extractedImage != nullptr);
                      auto subsetImage = std::static_pointer_cast<SubsetImage>(extractedImage);
                      EXPECT_TRUE(subsetImage->source == image));
  EXPECT_EQ(offset.x, 0.0f);
  EXPECT_EQ(offset.y, 0.0f);
}

TGFX_TEST(PictureTest, ScaleImage) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "PictureTest/scaled_image_origin"));
  auto scaledImage = ScaleImage(image, 0.55f);
  canvas->clear();
  canvas->drawImage(scaledImage);
  EXPECT_TRUE(Baseline::Compare(surface, "PictureTest/scaled_image"));
  canvas->clear();
  SamplingOptions sampling(FilterMode::Linear, MipmapMode::Linear);
  scaledImage = ScaleImage(scaledImage, 2.f, sampling);
  EXPECT_EQ(scaledImage->width(), 400);
  EXPECT_EQ(scaledImage->height(), 566);
  canvas->drawImage(scaledImage);
  EXPECT_TRUE(Baseline::Compare(surface, "PictureTest/scaled_scale_up"));
  canvas->clear();
  canvas->clipRect(Rect::MakeXYWH(100, 100, 500, 500));
  canvas->drawImage(scaledImage);
  EXPECT_TRUE(Baseline::Compare(surface, "PictureTest/scaled_clip"));
}

TGFX_TEST(PictureTest, MaskPath) {
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
  canvas->drawRRect(RRect::MakeRectXY(Rect::MakeWH(100.f, 80.f), 10.f, 10.f), paint);
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

/**
 * Verifies the complexity guards in MaskContext that protect against runaway PathOp::Union cost
 * when the source picture is too large or contains very dense paths. All three guards must abort
 * extraction so callers can fall back to the MaskFilter shader path.
 */
TGFX_TEST(PictureTest, MaskPathComplexAbort) {
  auto getMaskPath = [](const std::shared_ptr<Picture>& picture, Path* maskPath) -> bool {
    return MaskContext::GetMaskPath(picture, maskPath);
  };
  Paint paint = {};
  paint.setColor(Color::Black());

  // Guard 1: drawCount budget at the top level. 31 draws exceeds the 30-op cap.
  {
    PictureRecorder recorder = {};
    auto* canvas = recorder.beginRecording();
    for (int i = 0; i < 31; ++i) {
      canvas->drawRect(Rect::MakeXYWH(static_cast<float>(i), 0.f, 1.f, 1.f), paint);
    }
    auto picture = recorder.finishRecordingAsPicture();
    ASSERT_TRUE(picture != nullptr);
    Path maskPath = {};
    EXPECT_FALSE(getMaskPath(picture, &maskPath));
  }

  // Guard 2: accumulated-verb circuit breaker. A single drawPath with thousands of segments
  // stays under the drawCount budget but blows past the projected-verb cap before Union runs.
  {
    Path densePath = {};
    densePath.moveTo(0.f, 0.f);
    for (int i = 1; i < 4000; ++i) {
      densePath.lineTo(static_cast<float>(i), static_cast<float>(i & 1));
    }
    densePath.close();
    PictureRecorder recorder = {};
    auto* canvas = recorder.beginRecording();
    canvas->drawPath(densePath, paint);
    auto picture = recorder.finishRecordingAsPicture();
    ASSERT_TRUE(picture != nullptr);
    TGFX_PRIVATE_ACCESS(EXPECT_LE(picture->drawCount, 30u));
    Path maskPath = {};
    EXPECT_FALSE(getMaskPath(picture, &maskPath));
  }

  // Guard 3: drawCount budget propagates into nested pictures. A small outer picture that
  // embeds an oversized inner picture must abort, otherwise the inner replay would bypass the
  // top-level cap.
  {
    PictureRecorder innerRecorder = {};
    auto* innerCanvas = innerRecorder.beginRecording();
    for (int i = 0; i < 31; ++i) {
      innerCanvas->drawRect(Rect::MakeXYWH(static_cast<float>(i), 0.f, 1.f, 1.f), paint);
    }
    auto innerPicture = innerRecorder.finishRecordingAsPicture();
    ASSERT_TRUE(innerPicture != nullptr);

    PictureRecorder outerRecorder = {};
    auto* outerCanvas = outerRecorder.beginRecording();
    outerCanvas->drawPicture(innerPicture);
    auto outerPicture = outerRecorder.finishRecordingAsPicture();
    ASSERT_TRUE(outerPicture != nullptr);
    Path maskPath = {};
    EXPECT_FALSE(getMaskPath(outerPicture, &maskPath));
  }

  // Sanity: a small simple picture still succeeds, ensuring the new guards don't over-trigger.
  {
    PictureRecorder recorder = {};
    auto* canvas = recorder.beginRecording();
    canvas->drawRect(Rect::MakeWH(50.f, 50.f), paint);
    auto picture = recorder.finishRecordingAsPicture();
    ASSERT_TRUE(picture != nullptr);
    Path maskPath = {};
    EXPECT_TRUE(getMaskPath(picture, &maskPath));
  }
}

TGFX_TEST(PictureTest, RRectPlayback) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 400);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  const auto rRect = RRect::MakeRectXY(Rect::MakeXYWH(5, 5, 100, 100), 20, 20, 0.5f);
  PictureRecorder recorder = {};
  Paint strokePaint;
  strokePaint.setStyle(PaintStyle::Stroke);
  strokePaint.setStroke(Stroke(10));
  recorder.beginRecording()->drawRRect(rRect, strokePaint);
  auto picture = recorder.finishRecordingAsPicture();
  ASSERT_TRUE(picture != nullptr);
  EXPECT_EQ(picture->getBounds(), Rect::MakeXYWH(0, 0, 110, 110));

  // Recording the same RRect under a scale that forces the path fallback measures the same
  // stroke reach.
  PictureRecorder fallbackRecorder = {};
  auto* fallbackCanvas = fallbackRecorder.beginRecording();
  fallbackCanvas->scale(20, 20);
  fallbackCanvas->drawRRect(rRect, strokePaint);
  auto fallbackPicture = fallbackRecorder.finishRecordingAsPicture();
  ASSERT_TRUE(fallbackPicture != nullptr);
  EXPECT_EQ(fallbackPicture->getBounds(), Rect::MakeXYWH(0, 0, 2200, 2200));

  // Replaying the dedicated RRect record under that scale uses the same bounds.
  PictureRecorder playbackRecorder = {};
  auto* playbackCanvas = playbackRecorder.beginRecording();
  playbackCanvas->scale(20, 20);
  playbackCanvas->drawPicture(picture);
  auto playbackPicture = playbackRecorder.finishRecordingAsPicture();
  ASSERT_TRUE(playbackPicture != nullptr);
  EXPECT_EQ(playbackPicture->getBounds(), Rect::MakeXYWH(0, 0, 2200, 2200));

  // A stroked smoothed RRect recorded and played back under the identity matrix.
  canvas->drawPicture(picture);
  TGFX_PRIVATE_ACCESS({
    auto* task = FlushAndGetLastRenderTask(surface);
    EXPECT_EQ(task->drawOps.back()->type(), DrawOp::Type::RRectDrawOp);
  });
  // Direct drawing and picture playback under a scale that pushes the stroke reach beyond the
  // distance limit both fall back to a shape.
  canvas->scale(20, 20);
  canvas->drawRRect(rRect, strokePaint);
  TGFX_PRIVATE_ACCESS({
    auto* task = FlushAndGetLastRenderTask(surface);
    EXPECT_EQ(task->drawOps.back()->type(), DrawOp::Type::ShapeDrawOp);
  });
  canvas->drawPicture(picture);
  TGFX_PRIVATE_ACCESS({
    auto* task = FlushAndGetLastRenderTask(surface);
    EXPECT_EQ(task->drawOps.back()->type(), DrawOp::Type::ShapeDrawOp);
  });
}
}  // namespace tgfx

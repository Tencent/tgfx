/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "core/images/ResourceImage.h"
#include "core/images/SubsetImage.h"
#include "core/images/TransformImage.h"
#include "gpu/DrawingManager.h"
#include "gpu/Texture.h"
#include "gpu/opengl/GLCaps.h"
#include "gpu/opengl/GLSampler.h"
#include "gpu/ops/FillRectOp.h"
#include "gpu/ops/RRectOp.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/ImageReader.h"
#include "tgfx/core/Mask.h"
#include "tgfx/core/PathEffect.h"
#include "tgfx/core/Recorder.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/opengl/GLFunctions.h"
#include "utils/TestUtils.h"
#include "utils/TextShaper.h"

namespace tgfx {
TGFX_TEST(CanvasTest, clip) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto width = 1080;
  auto height = 1776;
  GLTextureInfo textureInfo;
  CreateGLTexture(context, width, height, &textureInfo);
  auto surface = Surface::MakeFrom(context, {textureInfo, width, height}, ImageOrigin::BottomLeft);
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
  auto gl = GLFunctions::Get(context);
  gl->deleteTextures(1, &textureInfo.id);
  device->unlock();
}

TGFX_TEST(CanvasTest, TileMode) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/rotation.jpg");
  image = image->makeMipmapped(true);
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, image->width() / 2, image->height() / 2);
  auto canvas = surface->getCanvas();
  Paint paint;
  auto shader = Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Mirror)
                    ->makeWithMatrix(Matrix::MakeScale(0.125f));
  paint.setShader(shader);
  canvas->translate(100, 100);
  auto drawRect = Rect::MakeXYWH(0, 0, surface->width() - 200, surface->height() - 200);
  canvas->drawRect(drawRect, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/tile_mode_normal"));
  canvas->clear();
  image = image->makeSubset(Rect::MakeXYWH(300, 1000, 2400, 2000));
  shader = Shader::MakeImageShader(image, TileMode::Mirror, TileMode::Repeat)
               ->makeWithMatrix(Matrix::MakeScale(0.125f));
  paint.setShader(shader);
  canvas->drawRect(drawRect, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/tile_mode_subset"));
  canvas->clear();
  image = MakeImage("resources/apitest/rgbaaa.png");
  ASSERT_TRUE(image != nullptr);
  image = image->makeRGBAAA(512, 512, 512, 0);
  ASSERT_TRUE(image != nullptr);
  shader = Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Mirror);
  paint.setShader(shader);
  canvas->drawRect(drawRect, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/tile_mode_rgbaaa"));
  device->unlock();
}

TGFX_TEST(CanvasTest, merge_draw_call_rect) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  int width = 72;
  int height = 72;
  auto surface = Surface::Make(context, width, height);
  // clear the pending ClearOp.
  context->flush();
  auto canvas = surface->getCanvas();
  canvas->clearRect(Rect::MakeWH(surface->width(), surface->height()), Color::White());
  Paint paint;
  paint.setColor(Color{0.8f, 0.8f, 0.8f, 1.f});
  auto lumaColorFilter = ColorFilter::Matrix({0,
                                              0,
                                              0,
                                              0,
                                              0,  // red
                                              0,
                                              0,
                                              0,
                                              0,
                                              0,  // green
                                              0,
                                              0,
                                              0,
                                              0,
                                              0,  // blue
                                              0.21260000000000001f,
                                              0.71519999999999995f,
                                              0.0722f,
                                              0,
                                              0});
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
  auto* drawingManager = context->drawingManager();
  EXPECT_TRUE(drawingManager->renderTasks.size() == 1);
  auto task = std::static_pointer_cast<OpsRenderTask>(drawingManager->renderTasks[0]);
  ASSERT_TRUE(task->ops.size() == 2);
  EXPECT_EQ(static_cast<FillRectOp*>(task->ops[1].get())->rectPaints.size(), drawCallCount);
  context->flush();
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/merge_draw_call_rect"));
  device->unlock();
}

TGFX_TEST(CanvasTest, merge_draw_call_rrect) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  int width = 72;
  int height = 72;
  auto surface = Surface::Make(context, width, height);
  // clear the pending ClearOp.
  context->flush();
  auto canvas = surface->getCanvas();
  canvas->clearRect(Rect::MakeWH(width, height), Color::White());
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
  auto* drawingManager = context->drawingManager();
  EXPECT_TRUE(drawingManager->renderTasks.size() == 1);
  auto task = std::static_pointer_cast<OpsRenderTask>(drawingManager->renderTasks[0]);
  ASSERT_TRUE(task->ops.size() == 2);
  EXPECT_EQ(static_cast<RRectOp*>(task->ops[1].get())->rRectPaints.size(), drawCallCount);
  context->flush();
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/merge_draw_call_rrect"));
  device->unlock();
}

TGFX_TEST(CanvasTest, merge_draw_clear_op) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  int width = 72;
  int height = 72;
  auto surface = Surface::Make(context, width, height);
  // clear the pending ClearOp.
  context->flush();
  auto canvas = surface->getCanvas();
  canvas->clearRect(Rect::MakeWH(width, height), Color::White());
  canvas->save();
  Path path;
  path.addRect(Rect::MakeXYWH(0.f, 0.f, 10.f, 10.f));
  canvas->clipPath(path);
  canvas->clearRect(Rect::MakeWH(width, height), Color::White());
  canvas->restore();
  Paint paint;
  paint.setColor(Color{0.8f, 0.8f, 0.8f, 1.f});
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

  auto* drawingManager = context->drawingManager();
  EXPECT_TRUE(drawingManager->renderTasks.size() == 1);
  auto task = std::static_pointer_cast<OpsRenderTask>(drawingManager->renderTasks[0]);
  EXPECT_TRUE(task->ops.size() == drawCallCount + 1);
  context->flush();
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/merge_draw_clear_op"));
  device->unlock();
}

TGFX_TEST(CanvasTest, textShape) {
  auto serifTypeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  ASSERT_TRUE(serifTypeface != nullptr);
  std::string text =
      "ffi fl\n"
      "x²-y²\n"
      "🤡👨🏼‍🦱👨‍👨‍👧‍👦\n"
      "🇨🇳🇫🇮\n"
      "#️⃣#*️⃣*\n"
      "1️⃣🔟";
  auto positionedGlyphs = TextShaper::Shape(text, serifTypeface);

  float fontSize = 25.f;
  float lineHeight = fontSize * 1.2f;
  float height = 0;
  float width = 0;
  float x;
  struct TextRun {
    std::vector<GlyphID> ids;
    std::vector<Point> positions;
    Font font;
  };
  std::vector<TextRun> textRuns;
  Path path;
  TextRun* run = nullptr;
  auto count = positionedGlyphs.glyphCount();
  auto newline = [&]() {
    x = 0;
    height += lineHeight;
    path.moveTo({0, height});
  };
  newline();
  for (size_t i = 0; i < count; ++i) {
    auto typeface = positionedGlyphs.getTypeface(i);
    if (run == nullptr || run->font.getTypeface() != typeface) {
      run = &textRuns.emplace_back();
      run->font = Font(typeface, fontSize);
    }
    auto index = positionedGlyphs.getStringIndex(i);
    auto length = (i + 1 == count ? text.length() : positionedGlyphs.getStringIndex(i + 1)) - index;
    auto name = text.substr(index, length);
    if (name == "\n") {
      newline();
      continue;
    }
    auto glyphID = positionedGlyphs.getGlyphID(i);
    run->ids.emplace_back(glyphID);
    run->positions.push_back(Point{x, height});
    x += run->font.getAdvance(glyphID);
    path.lineTo({x, height});
    if (width < x) {
      width = x;
    }
  }
  height += lineHeight;

  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto surface =
      Surface::Make(context, static_cast<int>(ceil(width)), static_cast<int>(ceil(height)));
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clearRect(Rect::MakeWH(surface->width(), surface->height()), Color::White());

  Paint strokePaint;
  strokePaint.setColor(Color{1.f, 0.f, 0.f, 1.f});
  strokePaint.setStrokeWidth(2.f);
  strokePaint.setStyle(PaintStyle::Stroke);
  canvas->drawPath(path, strokePaint);

  Paint paint;
  paint.setColor(Color::Black());
  for (const auto& textRun : textRuns) {
    canvas->drawGlyphs(textRun.ids.data(), textRun.positions.data(), textRun.ids.size(),
                       textRun.font, paint);
  }
  context->flush();
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/text_shape"));
  device->unlock();
}

TGFX_TEST(CanvasTest, filterMode) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);
  int width = image->width() * 2;
  int height = image->height() * 2;
  auto surface = Surface::Make(context, width, height);
  auto canvas = surface->getCanvas();
  canvas->setMatrix(Matrix::MakeScale(2.f));
  canvas->drawImage(image, SamplingOptions(FilterMode::Nearest));
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/filter_mode_nearest"));
  canvas->clear();
  canvas->drawImage(image, SamplingOptions(FilterMode::Linear));
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/filter_mode_linear"));
  device->unlock();
}

TGFX_TEST(CanvasTest, rasterized) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto defaultCacheLimit = context->cacheLimit();
  context->setCacheLimit(0);
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  auto scaleImage = image->makeScaled(1.0f, 1.0f);
  EXPECT_TRUE(scaleImage == image);
  image = MakeImage("resources/apitest/rotation.jpg");
  auto rasterImage = image->makeScaled(0.15f, 0.15f);
  rasterImage = rasterImage->makeRasterized();
  EXPECT_FALSE(rasterImage->hasMipmaps());
  EXPECT_FALSE(rasterImage == image);
  EXPECT_EQ(rasterImage->width(), 454);
  EXPECT_EQ(rasterImage->height(), 605);
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, 1100, 1400);
  auto canvas = surface->getCanvas();
  canvas->drawImage(rasterImage, 100, 100);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/rasterized"));
  auto rasterImageUniqueKey = std::static_pointer_cast<ResourceImage>(rasterImage)->uniqueKey;
  auto texture = Resource::Find<Texture>(context, rasterImageUniqueKey);
  EXPECT_TRUE(texture != nullptr);
  EXPECT_EQ(texture->width(), 454);
  EXPECT_EQ(texture->height(), 605);
  auto source = std::static_pointer_cast<TransformImage>(image)->source;
  auto imageUniqueKey = std::static_pointer_cast<ResourceImage>(source)->uniqueKey;
  texture = Resource::Find<Texture>(context, imageUniqueKey);
  EXPECT_TRUE(texture == nullptr);
  canvas->clear();
  image = image->makeMipmapped(true);
  EXPECT_TRUE(image->hasMipmaps());
  SamplingOptions sampling(FilterMode::Linear, MipmapMode::Linear);
  image = image->makeScaled(0.15f, 0.15f);
  rasterImage = image->makeRasterized(true, sampling);
  EXPECT_TRUE(rasterImage->hasMipmaps());
  canvas->drawImage(rasterImage, 100, 100);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/rasterized_mipmap"));
  texture = Resource::Find<Texture>(context, rasterImageUniqueKey);
  EXPECT_TRUE(texture == nullptr);
  rasterImageUniqueKey = std::static_pointer_cast<ResourceImage>(rasterImage)->uniqueKey;
  texture = Resource::Find<Texture>(context, rasterImageUniqueKey);
  EXPECT_TRUE(texture != nullptr);
  canvas->clear();
  rasterImage = image->makeMipmapped(false);
  EXPECT_FALSE(rasterImage->hasMipmaps());
  rasterImage = rasterImage->makeScaled(2.0f, 2.0f);
  rasterImage = rasterImage->makeRasterized(false, sampling);
  EXPECT_FALSE(rasterImage->hasMipmaps());
  rasterImage = rasterImage->makeMipmapped(true);
  EXPECT_EQ(rasterImage->width(), 907);
  EXPECT_EQ(rasterImage->height(), 1210);
  canvas->drawImage(rasterImage, 100, 100);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/rasterized_scale_up"));
  context->setCacheLimit(defaultCacheLimit);
  device->unlock();
}

TGFX_TEST(CanvasTest, mipmap) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto codec = MakeImageCodec("resources/apitest/rotation.jpg");
  ASSERT_TRUE(codec != nullptr);
  Bitmap bitmap(codec->width(), codec->height(), false, false);
  ASSERT_FALSE(bitmap.isEmpty());
  Pixmap pixmap(bitmap);
  auto result = codec->readPixels(pixmap.info(), pixmap.writablePixels());
  pixmap.reset();
  ASSERT_TRUE(result);
  auto imageBuffer = bitmap.makeBuffer();
  auto image = Image::MakeFrom(imageBuffer);
  ASSERT_TRUE(image != nullptr);
  auto imageMipmapped = image->makeMipmapped(true);
  ASSERT_TRUE(imageMipmapped != nullptr);
  float scale = 0.03f;
  auto width = codec->width();
  auto height = codec->height();
  auto imageWidth = static_cast<float>(width) * scale;
  auto imageHeight = static_cast<float>(height) * scale;
  auto imageMatrix = Matrix::MakeScale(scale);
  auto surface =
      Surface::Make(context, static_cast<int>(imageWidth), static_cast<int>(imageHeight));
  auto canvas = surface->getCanvas();
  canvas->setMatrix(imageMatrix);
  // 绘制没有 mipmap 的 texture 时，使用 MipmapMode::Linear 会回退到 MipmapMode::None。
  canvas->drawImage(image, SamplingOptions(FilterMode::Linear, MipmapMode::Linear));
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/mipmap_none"));
  canvas->clear();
  canvas->drawImage(imageMipmapped, SamplingOptions(FilterMode::Linear, MipmapMode::Nearest));
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/mipmap_nearest"));
  canvas->clear();
  canvas->drawImage(imageMipmapped, SamplingOptions(FilterMode::Linear, MipmapMode::Linear));
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/mipmap_linear"));
  surface = Surface::Make(context, static_cast<int>(imageWidth * 4.f),
                          static_cast<int>(imageHeight * 4.f));
  canvas = surface->getCanvas();
  Paint paint;
  paint.setShader(Shader::MakeImageShader(imageMipmapped, TileMode::Mirror, TileMode::Repeat,
                                          SamplingOptions(FilterMode::Linear, MipmapMode::Linear))
                      ->makeWithMatrix(imageMatrix));
  canvas->drawRect(Rect::MakeWH(surface->width(), surface->height()), paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/mipmap_linear_texture_effect"));
  device->unlock();
}

TGFX_TEST(CanvasTest, hardwareMipmap) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto codec = MakeImageCodec("resources/apitest/rotation.jpg");
  ASSERT_TRUE(codec != nullptr);
  Bitmap bitmap(codec->width(), codec->height(), false);
  ASSERT_FALSE(bitmap.isEmpty());
  Pixmap pixmap(bitmap);
  auto result = codec->readPixels(pixmap.info(), pixmap.writablePixels());
  pixmap.reset();
  ASSERT_TRUE(result);
  auto image = Image::MakeFrom(bitmap);
  auto imageMipmapped = image->makeMipmapped(true);
  ASSERT_TRUE(imageMipmapped != nullptr);
  float scale = 0.03f;
  auto width = codec->width();
  auto height = codec->height();
  auto imageWidth = static_cast<float>(width) * scale;
  auto imageHeight = static_cast<float>(height) * scale;
  auto imageMatrix = Matrix::MakeScale(scale);
  auto surface =
      Surface::Make(context, static_cast<int>(imageWidth), static_cast<int>(imageHeight));
  auto canvas = surface->getCanvas();
  canvas->setMatrix(imageMatrix);
  canvas->drawImage(imageMipmapped, SamplingOptions(FilterMode::Linear, MipmapMode::Linear));
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/mipmap_linear_hardware"));
  device->unlock();
}

TGFX_TEST(CanvasTest, path) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 700, 500);
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
  paint.setShader(Shader::MakeLinearGradient(
      Point{0.f, 0.f}, Point{static_cast<float>(25), static_cast<float>(100)},
      {Color{0.f, 1.f, 0.f, 1.f}, Color{1.f, 0.f, 0.f, 0.f}}, {}));
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
  matrix.postTranslate(500, 10);
  canvas->setMatrix(matrix);
  canvas->drawPath(path, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/path"));
  device->unlock();
}

TGFX_TEST(CanvasTest, shape) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto width = 400;
  auto height = 500;
  auto surface = Surface::Make(context, width, height);
  auto canvas = surface->getCanvas();
  canvas->clearRect(Rect::MakeWH(surface->width(), surface->height()), Color::White());
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);
  Paint paint;
  paint.setStyle(PaintStyle::Stroke);
  paint.setStroke(Stroke(0));
  EXPECT_TRUE(paint.nothingToDraw());
  paint.setStrokeWidth(2);
  paint.setColor(Color{1.f, 0.f, 0.f, 1.f});
  auto point = Point::Make(width / 2, height / 2);
  auto radius = image->width() / 2;
  auto rect = Rect::MakeWH(radius * 2, radius * 2);
  canvas->drawCircle(point, radius + 30, paint);
  canvas->setMatrix(Matrix::MakeTrans(point.x - radius, point.y - radius));
  canvas->drawRoundRect(rect, 10, 10, paint);

  canvas->setMatrix(Matrix::MakeTrans(point.x - radius, point.y - radius));
  canvas->rotate(45, radius, radius);
  canvas->drawImage(image, SamplingOptions(FilterMode::Linear));
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/shape"));
  device->unlock();
}

TGFX_TEST(CanvasTest, image) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 500, false, 1, false, RenderFlags::DisableCache);
  auto canvas = surface->getCanvas();
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);
  EXPECT_FALSE(image->isFullyDecoded());
  EXPECT_FALSE(image->isTextureBacked());
  EXPECT_FALSE(image->hasMipmaps());
  auto rotatedImage = image->makeOriented(Orientation::RightTop);
  EXPECT_NE(rotatedImage, image);
  rotatedImage = rotatedImage->makeOriented(Orientation::LeftBottom);
  EXPECT_EQ(rotatedImage, image);
  canvas->drawImage(image);
  auto decodedImage = image->makeDecoded(context);
  EXPECT_FALSE(decodedImage == image);
  context->flush();
  decodedImage = image->makeDecoded(context);
  EXPECT_FALSE(decodedImage == image);
  auto textureImage = image->makeTextureImage(context);
  ASSERT_TRUE(textureImage != nullptr);
  EXPECT_TRUE(textureImage->isTextureBacked());
  EXPECT_TRUE(textureImage->isFullyDecoded());
  decodedImage = image->makeDecoded(context);
  EXPECT_TRUE(decodedImage == image);
  textureImage = nullptr;
  decodedImage = image->makeDecoded(context);
  EXPECT_FALSE(decodedImage == image);
  context->flush();
  decodedImage = image->makeDecoded(context);
  EXPECT_FALSE(decodedImage == image);

  surface = Surface::Make(context, 400, 500);
  canvas = surface->getCanvas();
  canvas->drawImage(image);
  textureImage = image->makeTextureImage(context);
  canvas->drawImage(textureImage, 200, 0);
  auto subset = image->makeSubset(Rect::MakeWH(120, 120));
  EXPECT_TRUE(subset == nullptr);
  subset = image->makeSubset(Rect::MakeXYWH(-10, -10, 50, 50));
  EXPECT_TRUE(subset == nullptr);
  subset = image->makeSubset(Rect::MakeXYWH(15, 15, 80, 90));
  ASSERT_TRUE(subset != nullptr);
  EXPECT_EQ(subset->width(), 80);
  EXPECT_EQ(subset->height(), 90);
  canvas->drawImage(subset, 115, 15);
  decodedImage = image->makeDecoded(context);
  EXPECT_TRUE(decodedImage == image);
  decodedImage = image->makeDecoded();
  EXPECT_FALSE(decodedImage == image);
  ASSERT_TRUE(decodedImage != nullptr);
  EXPECT_TRUE(decodedImage->isFullyDecoded());
  EXPECT_FALSE(decodedImage->isTextureBacked());
  canvas->drawImage(decodedImage, 315, 0);
  auto data = Data::MakeFromFile(ProjectPath::Absolute("resources/apitest/rotation.jpg"));
  auto rotationImage = Image::MakeFromEncoded(std::move(data));
  EXPECT_EQ(rotationImage->width(), 3024);
  EXPECT_EQ(rotationImage->height(), 4032);
  EXPECT_FALSE(rotationImage->hasMipmaps());
  rotationImage = rotationImage->makeMipmapped(true);
  EXPECT_TRUE(rotationImage->hasMipmaps());
  auto matrix = Matrix::MakeScale(0.05f);
  matrix.postTranslate(0, 120);
  rotationImage = rotationImage->makeOriented(Orientation::BottomRight);
  rotationImage = rotationImage->makeOriented(Orientation::BottomRight);
  canvas->drawImage(rotationImage, matrix);
  subset = rotationImage->makeSubset(Rect::MakeXYWH(500, 800, 2000, 2400));
  ASSERT_TRUE(subset != nullptr);
  matrix.postTranslate(160, 30);
  canvas->drawImage(subset, matrix);
  subset = subset->makeSubset(Rect::MakeXYWH(400, 500, 1600, 1900));
  ASSERT_TRUE(subset != nullptr);
  matrix.postTranslate(110, -30);
  canvas->drawImage(subset, matrix);
  subset = subset->makeOriented(Orientation::RightTop);
  textureImage = subset->makeTextureImage(context);
  ASSERT_TRUE(textureImage != nullptr);
  matrix.postTranslate(0, 110);
  SamplingOptions sampling(FilterMode::Linear, MipmapMode::None);
  canvas->setMatrix(matrix);
  canvas->drawImage(textureImage, sampling);
  canvas->resetMatrix();
  auto rgbAAA = subset->makeRGBAAA(500, 500, 500, 0);
  EXPECT_TRUE(rgbAAA != nullptr);
  image = MakeImage("resources/apitest/rgbaaa.png");
  EXPECT_EQ(image->width(), 1024);
  EXPECT_EQ(image->height(), 512);
  image = image->makeMipmapped(true);
  rgbAAA = image->makeRGBAAA(512, 512, 512, 0);
  EXPECT_EQ(rgbAAA->width(), 512);
  EXPECT_EQ(rgbAAA->height(), 512);
  matrix = Matrix::MakeScale(0.25);
  matrix.postTranslate(0, 330);
  canvas->drawImage(rgbAAA, matrix);
  subset = rgbAAA->makeSubset(Rect::MakeXYWH(100, 100, 300, 200));
  matrix.postTranslate(140, 5);
  canvas->drawImage(subset, matrix);
  auto originImage = subset->makeOriented(Orientation::BottomLeft);
  EXPECT_TRUE(originImage != nullptr);
  matrix.postTranslate(0, 70);
  canvas->drawImage(originImage, matrix);
  rgbAAA = image->makeRGBAAA(512, 512, 0, 0);
  EXPECT_EQ(rgbAAA->width(), 512);
  EXPECT_EQ(rgbAAA->height(), 512);
  matrix.postTranslate(110, -75);
  canvas->drawImage(rgbAAA, matrix);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/drawImage"));
  device->unlock();
}

TGFX_TEST(CanvasTest, scaleImage) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1286, 558, false, 1, false, RenderFlags::DisableCache);
  auto canvas = surface->getCanvas();
  auto image = MakeImage("resources/apitest/rgbaaa.png");
  EXPECT_EQ(image->width(), 1024);
  EXPECT_EQ(image->height(), 512);
  image = image->makeScaled(0.5f, 0.5f);
  image = image->makeOriented(Orientation::RightTop);
  EXPECT_EQ(image->width(), 256);
  EXPECT_EQ(image->height(), 512);
  image = image->makeSubset(Rect::MakeXYWH(50, 50, 206, 462));
  EXPECT_EQ(image->width(), 206);
  EXPECT_EQ(image->height(), 462);
  image = image->makeScaled(3.0f, 3.0f);
  EXPECT_EQ(image->width(), 618);
  EXPECT_EQ(image->height(), 1386);
  image = image->makeSubset(Rect::MakeXYWH(60, 100, 558, 1286));
  image = image->makeOriented(Orientation::RightTop);
  image = image->makeScaled(0.25f, 0.25f);
  EXPECT_EQ(image->width(), 322);
  EXPECT_EQ(image->height(), 140);
  auto matrix = Matrix::MakeScale(2.f);
  matrix.postTranslate(20, 30);
  canvas->drawImage(image, matrix);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/scaleImage"));
  device->unlock();
}

static GLTextureInfo CreateRectangleTexture(Context* context, int width, int heigh) {
  auto gl = GLFunctions::Get(context);
  GLTextureInfo sampler = {};
  gl->genTextures(1, &(sampler.id));
  if (sampler.id == 0) {
    return {};
  }
  sampler.target = GL_TEXTURE_RECTANGLE;
  gl->bindTexture(sampler.target, sampler.id);
  gl->texParameteri(sampler.target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->texParameteri(sampler.target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl->texParameteri(sampler.target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->texParameteri(sampler.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  const auto& textureFormat = GLCaps::Get(context)->getTextureFormat(PixelFormat::RGBA_8888);
  gl->texImage2D(sampler.target, 0, static_cast<int>(textureFormat.internalFormatTexImage), width,
                 heigh, 0, textureFormat.externalFormat, GL_UNSIGNED_BYTE, nullptr);
  return sampler;
}

TGFX_TEST(CanvasTest, rectangleTextureAsBlendDst) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);
  auto sampler = CreateRectangleTexture(context, 110, 110);
  ASSERT_TRUE(sampler.id > 0);
  auto backendTexture = BackendTexture(sampler, 110, 110);
  auto surface = Surface::MakeFrom(context, backendTexture, ImageOrigin::TopLeft);
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);
  canvas->drawImage(image);
  image = MakeImage("resources/apitest/image_as_mask.png");
  ASSERT_TRUE(image != nullptr);
  Paint paint = {};
  paint.setBlendMode(BlendMode::Multiply);
  canvas->drawImage(image, &paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/hardware_render_target_blend"));
  GLFunctions::Get(context)->deleteTextures(1, &(sampler.id));
  device->unlock();
}

TGFX_TEST(CanvasTest, NothingToDraw) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
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
  device->unlock();
}

TGFX_TEST(CanvasTest, Picture) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto context = device->lockContext();
  ASSERT_TRUE(context != nullptr);

  Recorder recorder = {};
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
  auto matrix = Matrix::I();
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

  auto bounds = picture->getBounds();
  auto surface = Surface::Make(context, static_cast<int>(bounds.width()),
                               static_cast<int>(bounds.height() + 20));
  canvas = surface->getCanvas();
  path.reset();
  path.addOval(Rect::MakeWH(bounds.width(), bounds.height() + 100));
  canvas->clipPath(path);
  canvas->translate(0, 10);
  canvas->drawPicture(picture);
  canvas->translate(0, bounds.height() + 10);
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
  matrix = Matrix::MakeTrans(-100, -100);
  pictureImage =
      Image::MakeFrom(singleImageRecord, image->width() - 200, image->height() - 200, &matrix);
  ASSERT_TRUE(pictureImage != nullptr);
  EXPECT_TRUE(pictureImage->isComplex());
  auto subsetImage = std::static_pointer_cast<SubsetImage>(pictureImage);
  EXPECT_TRUE(subsetImage->source == image);
  EXPECT_EQ(singleImageRecord.use_count(), 1);
  pictureImage =
      Image::MakeFrom(singleImageRecord, image->width() - 100, image->height() - 100, &matrix);
  EXPECT_FALSE(pictureImage->isComplex());
  EXPECT_EQ(singleImageRecord.use_count(), 2);
  EXPECT_FALSE(pictureImage == image);
  pictureImage = Image::MakeFrom(singleImageRecord, image->width() - 100, image->height() - 100);
  EXPECT_FALSE(pictureImage->isComplex());
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
  pictureImage = Image::MakeFrom(singleImageRecord, image->width(), image->height(), &matrix, true);
  EXPECT_FALSE(pictureImage == image);

  surface = Surface::Make(context, pictureImage->width(), pictureImage->height());
  canvas = surface->getCanvas();
  canvas->drawImage(pictureImage);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/PictureImage"));

  canvas = recorder.beginRecording();
  paint.reset();
  canvas->drawSimpleText("Hello TGFX~", 0, 0, font, paint);
  auto textRecord = recorder.finishRecordingAsPicture();
  bounds = textRecord->getBounds();
  matrix = Matrix::MakeTrans(-bounds.left, -bounds.top);
  auto width = static_cast<int>(bounds.width());
  auto height = static_cast<int>(bounds.height());
  auto textImage = Image::MakeFrom(textRecord, width, height, &matrix, true);
  EXPECT_EQ(textRecord.use_count(), 1);
  ASSERT_TRUE(textImage != nullptr);
  EXPECT_FALSE(textImage->isComplex());

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
  auto pathImage = Image::MakeFrom(patRecord, width, height, &matrix, true);
  EXPECT_EQ(patRecord.use_count(), 1);
  ASSERT_TRUE(pathImage != nullptr);
  EXPECT_FALSE(pathImage->isComplex());

  surface = Surface::Make(context, pathImage->width(), pathImage->height());
  canvas = surface->getCanvas();
  canvas->drawImage(pathImage);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/PictureImage_Path"));

  device->unlock();
}
}  // namespace tgfx

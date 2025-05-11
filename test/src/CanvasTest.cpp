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

#include <tgfx/layers/DisplayList.h>
#include <tgfx/layers/Layer.h>
#include <tgfx/layers/ShapeLayer.h>
#include <tgfx/layers/SolidColor.h>
#include "core/PathRef.h"
#include "core/Records.h"
#include "core/images/ResourceImage.h"
#include "core/images/SubsetImage.h"
#include "core/images/TransformImage.h"
#include "core/shapes/AppendShape.h"
#include "core/shapes/ProviderShape.h"
#include "gpu/DrawingManager.h"
#include "gpu/RenderContext.h"
#include "gpu/Texture.h"
#include "gpu/opengl/GLCaps.h"
#include "gpu/opengl/GLSampler.h"
#include "gpu/ops/RRectDrawOp.h"
#include "gpu/ops/RectDrawOp.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Fill.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/ImageReader.h"
#include "tgfx/core/Mask.h"
#include "tgfx/core/Recorder.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/opengl/GLFunctions.h"
#include "utils/TestUtils.h"
#include "utils/TextShaper.h"
#include "utils/common.h"

namespace tgfx {
TGFX_TEST(CanvasTest, clip) {
  ContextScope scope;
  auto context = scope.getContext();
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
}

TGFX_TEST(CanvasTest, TileMode) {
  ContextScope scope;
  auto context = scope.getContext();
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
  auto* drawingManager = context->drawingManager();
  ASSERT_TRUE(drawingManager->renderTasks.size() == 1);
  auto task = static_cast<OpsRenderTask*>(drawingManager->renderTasks.front().get());
  EXPECT_TRUE(task->ops.size() == 1);

  Paint paint;
  paint.setColor(Color{0.8f, 0.8f, 0.8f, 0.8f});
  canvas->drawRect(Rect::MakeWH(50, 50), paint);
  paint.setBlendMode(BlendMode::Src);
  canvas->drawRect(Rect::MakeWH(width, height), paint);
  surface->renderContext->flush();
  ASSERT_TRUE(drawingManager->renderTasks.size() == 2);
  task = static_cast<OpsRenderTask*>(drawingManager->renderTasks.back().get());
  EXPECT_TRUE(task->ops.size() == 1);

  paint.setColor(Color{0.8f, 0.8f, 0.8f, 1.f});
  canvas->drawRect(Rect::MakeWH(50, 50), paint);
  paint.setBlendMode(BlendMode::SrcOver);
  paint.setShader(Shader::MakeLinearGradient(
      Point{0.f, 0.f}, Point{static_cast<float>(width), static_cast<float>(height)},
      {Color{0.f, 1.f, 0.f, 1.f}, Color{0.f, 0.f, 0.f, 1.f}}, {}));
  canvas->drawPaint(paint);
  surface->renderContext->flush();
  ASSERT_TRUE(drawingManager->renderTasks.size() == 3);
  task = static_cast<OpsRenderTask*>(drawingManager->renderTasks.back().get());
  EXPECT_TRUE(task->ops.size() == 1);
  context->flush();
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
  auto* drawingManager = context->drawingManager();
  EXPECT_TRUE(drawingManager->renderTasks.size() == 1);
  auto task = static_cast<OpsRenderTask*>(drawingManager->renderTasks.front().get());
  ASSERT_TRUE(task->ops.size() == 2);
  EXPECT_EQ(static_cast<RectDrawOp*>(task->ops.back().get())->rectCount, drawCallCount);
  context->flush();
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
  auto* drawingManager = context->drawingManager();
  EXPECT_TRUE(drawingManager->renderTasks.size() == 1);
  auto task = static_cast<OpsRenderTask*>(drawingManager->renderTasks.front().get());
  ASSERT_TRUE(task->ops.size() == 2);
  EXPECT_EQ(static_cast<RRectDrawOp*>(task->ops.back().get())->rectCount, drawCallCount);
  context->flush();
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/merge_draw_call_rrect"));
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

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface =
      Surface::Make(context, static_cast<int>(ceil(width)), static_cast<int>(ceil(height)));
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

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
}

TGFX_TEST(CanvasTest, filterMode) {
  ContextScope scope;
  auto context = scope.getContext();
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
  Path path = {};
  auto success = textBlob->getPath(&path);
  EXPECT_TRUE(success);
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

TGFX_TEST(CanvasTest, rasterizedImage) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto defaultCacheLimit = context->cacheLimit();
  context->setCacheLimit(0);
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  auto rasterImage = image->makeRasterized();
  EXPECT_TRUE(rasterImage == image);
  image = MakeImage("resources/apitest/rotation.jpg");
  rasterImage = image->makeRasterized(0.15f);
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
  rasterImage = image->makeRasterized(0.15f, sampling);
  EXPECT_TRUE(rasterImage->hasMipmaps());
  canvas->drawImage(rasterImage, 100, 100);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/rasterized_mipmap"));
  texture = Resource::Find<Texture>(context, rasterImageUniqueKey);
  EXPECT_TRUE(texture == nullptr);
  rasterImageUniqueKey = std::static_pointer_cast<ResourceImage>(rasterImage)->uniqueKey;
  texture = Resource::Find<Texture>(context, rasterImageUniqueKey);
  EXPECT_TRUE(texture != nullptr);
  canvas->clear();
  rasterImage = rasterImage->makeMipmapped(false);
  EXPECT_FALSE(rasterImage->hasMipmaps());
  rasterImage = rasterImage->makeRasterized(2.0f, sampling);
  EXPECT_FALSE(rasterImage->hasMipmaps());
  rasterImage = rasterImage->makeMipmapped(true);
  EXPECT_EQ(rasterImage->width(), 907);
  EXPECT_EQ(rasterImage->height(), 1210);
  canvas->drawImage(rasterImage, 100, 100);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/rasterized_scale_up"));
  context->setCacheLimit(defaultCacheLimit);
}

TGFX_TEST(CanvasTest, mipmap) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto codec = MakeImageCodec("resources/apitest/rotation.jpg");
  ASSERT_TRUE(codec != nullptr);
  Bitmap bitmap(codec->width(), codec->height(), false, false);
  ASSERT_FALSE(bitmap.isEmpty());
  Pixmap pixmap(bitmap);
  auto result = codec->readPixels(pixmap.info(), pixmap.writablePixels());
  pixmap.reset();
  EXPECT_TRUE(result);
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
}

TGFX_TEST(CanvasTest, TileModeFallback) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto caps = (Caps*)context->caps();
  caps->npotTextureTileSupport = false;
  auto image = MakeImage("resources/apitest/rotation.jpg");
  ASSERT_TRUE(image != nullptr);
  image = image->makeMipmapped(true);
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, image->width() / 2, image->height() / 2);
  auto canvas = surface->getCanvas();
  Paint paint;
  SamplingOptions sampling(FilterMode::Linear, MipmapMode::Nearest);
  auto shader = Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Mirror, sampling)
                    ->makeWithMatrix(Matrix::MakeScale(0.125f));
  paint.setShader(shader);
  canvas->translate(100, 100);
  auto drawRect = Rect::MakeXYWH(0, 0, surface->width() - 200, surface->height() - 200);
  canvas->drawRect(drawRect, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/TileModeFallback"));
  caps->npotTextureTileSupport = true;
}

TGFX_TEST(CanvasTest, hardwareMipmap) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto codec = MakeImageCodec("resources/apitest/rotation.jpg");
  ASSERT_TRUE(codec != nullptr);
  Bitmap bitmap(codec->width(), codec->height(), false);
  ASSERT_FALSE(bitmap.isEmpty());
  Pixmap pixmap(bitmap);
  auto result = codec->readPixels(pixmap.info(), pixmap.writablePixels());
  pixmap.reset();
  EXPECT_TRUE(result);
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
}

TGFX_TEST(CanvasTest, path) {
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

  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/path"));
}

TGFX_TEST(CanvasTest, simpleShape) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/shape"));
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

TGFX_TEST(CanvasTest, inversePath) {
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
  Path textPath = {};
  auto success = textBlob->getPath(&textPath);
  EXPECT_TRUE(success);
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
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/inversePath_text"));

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
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/inversePath_rect"));
  auto uniqueKey = PathRef::GetUniqueKey(path);
  auto cachesBefore = FindResourceByDomainID(context, uniqueKey.domainID());
  EXPECT_EQ(cachesBefore.size(), 1u);
  canvas->clear();
  canvas->clipPath(clipPath);
  auto shape = Shape::MakeFrom(path);
  shape = Shape::ApplyMatrix(std::move(shape), Matrix::MakeTrans(50, 50));
  canvas->translate(-50, -50);
  canvas->drawShape(shape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/inversePath_rect"));
  auto cachesAfter = FindResourceByDomainID(context, uniqueKey.domainID());
  EXPECT_EQ(cachesAfter.size(), 1u);
  EXPECT_TRUE(cachesBefore.front() == cachesAfter.front());
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

TGFX_TEST(CanvasTest, drawShape) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/drawShape"));
}

TGFX_TEST(CanvasTest, inverseFillType) {
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

  firstShape = Shape::ApplyInverse(firstShape);
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
  EXPECT_FALSE(shape->isInverseFillType());
  shape = Shape::ApplyMatrix(firstShape, Matrix::MakeScale(2.0f));
  EXPECT_TRUE(shape->isInverseFillType());
  shape = Shape::ApplyStroke(firstShape, &stroke);
  EXPECT_TRUE(shape->isInverseFillType());
}

TGFX_TEST(CanvasTest, image) {
  ContextScope scope;
  auto context = scope.getContext();
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
}

TGFX_TEST(CanvasTest, atlas) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1300, 740, false, 1, false, RenderFlags::DisableCache);
  auto canvas = surface->getCanvas();
  auto imageCodec = MakeImageCodec("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(imageCodec != nullptr);
  EXPECT_EQ(imageCodec->width(), 1280);
  EXPECT_EQ(imageCodec->height(), 720);
  EXPECT_EQ(imageCodec->orientation(), Orientation::TopLeft);
  auto rowBytes = static_cast<size_t>(imageCodec->width()) * 4;
  Buffer buffer(rowBytes * static_cast<size_t>(imageCodec->height()));
  auto pixels = buffer.data();
  ASSERT_TRUE(pixels != nullptr);
  auto RGBAInfo = ImageInfo::Make(imageCodec->width(), imageCodec->height(), ColorType::RGBA_8888,
                                  AlphaType::Premultiplied);
  EXPECT_TRUE(imageCodec->readPixels(RGBAInfo, pixels));
  auto pixelsData = Data::MakeWithCopy(buffer.data(), buffer.size());
  ASSERT_TRUE(pixelsData != nullptr);
  auto image = Image::MakeFrom(RGBAInfo, std::move(pixelsData));
  ASSERT_TRUE(image != nullptr);
  Matrix matrix[4] = {Matrix::I(), Matrix::MakeTrans(660, 0), Matrix::MakeTrans(0, 380),
                      Matrix::MakeTrans(660, 380)};
  Rect rect[4] = {Rect::MakeXYWH(0, 0, 640, 360), Rect::MakeXYWH(640, 0, 640, 360),
                  Rect::MakeXYWH(0, 360, 640, 360), Rect::MakeXYWH(640, 360, 640, 360)};
  canvas->drawAtlas(std::move(image), matrix, rect, nullptr, 4);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/altas"));
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
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto sampler = CreateRectangleTexture(context, 110, 110);
  ASSERT_TRUE(sampler.id > 0);
  auto backendTexture = BackendTexture(sampler, 110, 110);
  auto surface = Surface::MakeFrom(context, backendTexture, ImageOrigin::TopLeft, 4);
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
}

TGFX_TEST(CanvasTest, YUVImage) {
  int width = 1440;
  size_t height = 1280;
  size_t lineSize = 1440;
  size_t yDataSize = lineSize * height;
  auto data = Data::MakeFromFile(ProjectPath::Absolute("resources/apitest/yuv_data/data.yuv"));
  ASSERT_TRUE(data != nullptr);
  EXPECT_TRUE(data->size() == yDataSize * 2);
  const uint8_t* dataAddress[3];
  dataAddress[0] = data->bytes();
  dataAddress[1] = data->bytes() + yDataSize;
  dataAddress[2] = data->bytes() + yDataSize + yDataSize / 2;
  const size_t lineSizes[3] = {lineSize, lineSize / 2, lineSize / 2};
  auto yuvData = YUVData::MakeFrom(width, static_cast<int>(height), (const void**)dataAddress,
                                   lineSizes, YUVData::I420_PLANE_COUNT);
  ASSERT_TRUE(yuvData != nullptr);
  auto image = Image::MakeI420(std::move(yuvData));
  ASSERT_TRUE(image != nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, image->width(), image->height());
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->drawImage(image);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/YUVImage"));
  canvas->clear();
  auto rgbaa = image->makeRGBAAA(width / 2, static_cast<int>(height), width / 2, 0);
  ASSERT_TRUE(rgbaa != nullptr);
  canvas->setMatrix(Matrix::MakeTrans(static_cast<float>(width / 4), 0.f));
  canvas->drawImage(rgbaa);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/YUVImage_RGBAA"));
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
  canvas = recorder.beginRecording();
  auto imageFilter = ImageFilter::Blur(10, 10);
  paint.setImageFilter(imageFilter);
  canvas->drawPicture(singleImageRecord, nullptr, &paint);
  paint.setImageFilter(nullptr);
  auto imagePicture = recorder.finishRecordingAsPicture();
  ASSERT_TRUE(imagePicture != nullptr);
  ASSERT_TRUE(imagePicture->drawCount == 1);
  EXPECT_EQ(imagePicture->firstDrawRecord()->type(), RecordType::DrawImage);

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
  canvas->drawSimpleText("Hello TGFX~", 0, 0, font, paint);
  auto textRecord = recorder.finishRecordingAsPicture();
  bounds = textRecord->getBounds();
  matrix = Matrix::MakeTrans(-bounds.left, -bounds.top);
  auto width = static_cast<int>(bounds.width());
  auto height = static_cast<int>(bounds.height());
  auto textImage = Image::MakeFrom(textRecord, width, height, &matrix);
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
    canvas->drawImage(image, Matrix::MakeScale(scale), &paint);
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

TGFX_TEST(CanvasTest, Path_addArc) {
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
    EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/Path_addArc" + std::to_string(i)));
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

TGFX_TEST(CanvasTest, Path_complex) {
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

  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/Path_complex"));
}

TGFX_TEST(CanvasTest, DrawPathProvider) {
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

  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/DrawPathProvider"));
}

TGFX_TEST(CanvasTest, StrokeShape) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/StrokeShape"));

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
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/StrokeShape_miter"));
}

TGFX_TEST(CanvasTest, ClipAll) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/ClipAll"));
}

TGFX_TEST(CanvasTest, RevertRect) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 10, 10);
  auto canvas = surface->getCanvas();
  Path path = {};
  path.addRect(5, 5, 2, 3);
  Paint paint = {};
  canvas->drawPath(path, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/RevertRect"));
}

TGFX_TEST(CanvasTest, AdaptiveDashEffect) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/AdaptiveDashEffect"));
}

TGFX_TEST(CanvasTest, BlendFormula) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200 * (1 + static_cast<int>(BlendMode::Screen)), 400);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::FromRGBA(100, 100, 100, 128));
  Path linePath = {};
  linePath.addRect(50, 50, 150, 150);
  linePath.moveTo(50, 50);
  linePath.lineTo(150, 50);
  linePath.lineTo(150, 170);
  linePath.lineTo(50, 120);
  linePath.lineTo(100, 170);
  for (int i = 0; i < 100; ++i) {
    // make sure the path will be rasterized as coverage
    linePath.lineTo(90 + i, 50 + i);
  }
  Paint linePaint = {};
  linePaint.setColor(Color::FromRGBA(255, 0, 0, 128));
  linePaint.setStyle(PaintStyle::Stroke);
  linePaint.setStroke(Stroke(10));
  Paint rectPaint = {};
  rectPaint.setColor(Color::FromRGBA(255, 0, 0, 128));
  for (int i = 0; i <= static_cast<int>(BlendMode::Screen); ++i) {
    // auto i = BlendMode::DstOut;
    linePaint.setBlendMode(static_cast<BlendMode>(i));
    canvas->drawPath(linePath, linePaint);
    // rect is not rasterized as coverage
    rectPaint.setBlendMode(static_cast<BlendMode>(i));
    canvas->drawRect(Rect::MakeXYWH(25, 200, 150, 150), rectPaint);
    canvas->translate(200, 0);
  }
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/BlendFormula"));
}

/// @brief 曲线线段（兼容直线）
struct Curve {
  /// @brief 起点
  tgfx::Point from;
  /// @brief 终点
  tgfx::Point to;
  /// @brief 起点的控制点
  tgfx::Point control_from;
  /// @brief 终点的控制点
  tgfx::Point control_to;
  /// @brief 是否为贝塞尔曲线
  bool is_bezier;
};

/// @brief 用于构建路径的参数
struct CurvesParam {
  /// @brief 首尾相接的曲线线段集合
  std::vector<Curve> curves;
  /// @brief 是否闭合
  bool is_closed;
};

struct VectorVertex {
  float x;  // 归一化的点
  float y;
  float cornerRadius = 0;
};

// 用来计算圆角的数据结构
struct VectorVertexDegree {
  VectorVertex vertex;  // 顶点
  float degree = 0.0f;  // 顶点角度, 弧度制; 0 表示顶点处不是拐角
};

VectorVertex calculate_star_vertex(const tgfx::Point& size, const uint32_t count,
                                   const float corner_radius, float ratio, const uint32_t index,
                                   bool is_inner_corner) {
  float a = size.x / 2;
  float b = size.y / 2;

  if (is_inner_corner) {
    std::cout << "Hello " << "\t" << __FUNCTION__ << " " << __FILE_NAME__ << ":" << __LINE__
              << " \n";
    float theta = static_cast<float>(M_PI_2 - 2 * M_PI * (index + 0.5) / count);  // 内圈顶点
    float x = a + a * ratio * cos(theta);
    float y = b - b * ratio * sin(theta);
    return VectorVertex{.x = x, .y = y, .cornerRadius = corner_radius};
  } else {
    std::cout << "Hello " << "\t" << __FUNCTION__ << " " << __FILE_NAME__ << ":" << __LINE__
              << " \n";
    float theta = static_cast<float>(M_PI_2 - 2 * M_PI * index / count);  // 外圈顶点
    float x = a + a * cos(theta);
    float y = b - b * sin(theta);
    return VectorVertex{.x = x, .y = y, .cornerRadius = corner_radius};
  }
}

extern inline float calculate_distance_between_points(const VectorVertex& a,
                                                      const VectorVertex& b) {
  float dx = a.x - b.x;
  float dy = a.y - b.y;
  return std::sqrt(dx * dx + dy * dy);
}

extern inline float calculate_distance_between_points(const tgfx::Point& a, const tgfx::Point& b) {
  float dx = a.x - b.x;
  float dy = a.y - b.y;
  return std::sqrt(dx * dx + dy * dy);
}

float calculate_corner_degree(const VectorVertex& prev_vertex, const VectorVertex& current_vertex,
                              const VectorVertex& next_vertex) {
  // 计算三边长度
  float ab = calculate_distance_between_points(prev_vertex, current_vertex);
  float bc = calculate_distance_between_points(next_vertex, current_vertex);
  float ac = calculate_distance_between_points(prev_vertex, next_vertex);

  // 余弦定理，求出 cos B 点的值
  float cos_b =
      static_cast<float>((std::pow(ab, 2) + std::pow(bc, 2) - std::pow(ac, 2)) / (2 * ab * bc));
  cos_b = std::min(1.0f, std::max(-1.0f, cos_b));  // 防止 cos_b 超出范围
  // 求出 B 点的角度
  float degree = std::acos(cos_b);
  return degree;
}

void get_vertex_degrees_from_vertices(const std::vector<VectorVertex>& vertices,
                                      std::vector<VectorVertexDegree>& vertex_degrees) {
  for (size_t i = 0; i < vertices.size(); i++) {
    auto prev_vertex = vertices[(i - 1 + vertices.size()) % vertices.size()];
    auto current_vertex = vertices[i];
    auto next_vertex = vertices[(i + 1) % vertices.size()];
    // 计算当前顶点的角度
    float degree = calculate_corner_degree(prev_vertex, current_vertex, next_vertex);
    vertex_degrees.push_back(VectorVertexDegree{current_vertex, degree});
  }
}

void calculate_star_vertex_degrees(const tgfx::Point& size, const uint32_t count, const float ratio,
                                   const float corner_radius,
                                   std::vector<VectorVertexDegree>& vertex_degrees) {
  // 计算多边形的顶点集
  std::vector<VectorVertex> vertices;
  for (uint32_t i = 0; i < count; i++) {
    vertices.push_back(
        calculate_star_vertex(size, count, corner_radius, ratio, i, false));  // 外圈顶点
    vertices.push_back(
        calculate_star_vertex(size, count, corner_radius, ratio, i, true));  // 内圈顶点
  }
  // 获取顶点和角度的结构体集
  get_vertex_degrees_from_vertices(vertices, vertex_degrees);
}

bool has_corner_radius(const VectorVertex& vertex) {
  return vertex.cornerRadius > 0;
}

bool is_equal(float a, float b) {
  return std::abs(a - b) < 0.00001;
}

float calculate_corner_length(float corner_radius, float degree) {
  return is_equal(degree, 0.0f) ? 0.0f : corner_radius / std::tan(degree / 2);
}

// 计算线段与线段倒圆角的实际渲染半径，算法参考文档：https://iwiki.woa.com/p/4012033729
// prev_vertex 对应 A，current_vertex 对应 B，next_vertex 对应 C
float calculate_line_to_line_corner_radius(const VectorVertexDegree& prev_vertex_degree,
                                           const VectorVertexDegree& current_vertex_degree,
                                           const VectorVertexDegree& next_vertex_degree) {
  auto prev_vertex = prev_vertex_degree.vertex;
  auto current_vertex = current_vertex_degree.vertex;
  auto next_vertex = next_vertex_degree.vertex;
  // 计AB、BC边长度
  float ab = calculate_distance_between_points(prev_vertex, current_vertex);
  float bc = calculate_distance_between_points(next_vertex, current_vertex);

  float corner_r_a = prev_vertex.cornerRadius;     // A 点的设定倒角半径
  float corner_r_b = current_vertex.cornerRadius;  // B 点的设定倒角半径
  float corner_r_c = next_vertex.cornerRadius;     // C 点的设定倒角半径

  // 计算倒角拐点到顶点的距离
  float corner_length_a = calculate_corner_length(corner_r_a, prev_vertex_degree.degree);
  float corner_length_b = calculate_corner_length(corner_r_b, current_vertex_degree.degree);
  float corner_length_c = calculate_corner_length(corner_r_c, next_vertex_degree.degree);

  // 实际拐角长度不大于最短边长
  float render_corner_length = std::min(corner_length_b, std::min(ab, bc));
  // 如果相邻点拐角长度与当前点拐角长度和大于边长，则当前点的渲染拐角长度按照比例竞争
  if (has_corner_radius(prev_vertex) && corner_length_a + corner_length_b > ab) {
    render_corner_length =
        std::min(corner_length_b / (corner_length_a + corner_length_b) * ab, render_corner_length);
  }
  if (has_corner_radius(next_vertex) && corner_length_c + corner_length_b > bc) {
    render_corner_length =
        std::min(corner_length_b / (corner_length_c + corner_length_b) * bc, render_corner_length);
  }

  // 计算实际渲染半径
  return render_corner_length * std::tan(current_vertex_degree.degree / 2);
}

void calculate_polygon_corner_radii(std::vector<VectorVertexDegree>& vertex_degrees,
                                    std::vector<float>& radii) {
  for (size_t current_vertex_index = 0; current_vertex_index < vertex_degrees.size();
       current_vertex_index++) {
    auto previous_vertex_degree =
        vertex_degrees[(current_vertex_index - 1 + vertex_degrees.size()) % vertex_degrees.size()];
    auto current_vertex_degree = vertex_degrees[current_vertex_index];
    auto next_vertex_degree = vertex_degrees[(current_vertex_index + 1) % vertex_degrees.size()];
    // 计算当前顶点的渲染半径
    radii.push_back(calculate_line_to_line_corner_radius(
        previous_vertex_degree, current_vertex_degree, next_vertex_degree));
  }
}

// 计算控制点与节点间的距离: h=4/3*(1-cos(θ/2))/sin(θ/2)
float calculate_arc_sector_control_distance(const float radius, const float degree) {
  return static_cast<float>(4.0 / 3.0 * (1 - std::cos(degree / 2)) / std::sin(degree / 2)) * radius;
}

// 计算倒角 bezier 曲线（prev_vertex 为 A 点，current_vertex 为 B 点，next_vertex 为 C 点）
Curve fit_corner_curve(const VectorVertexDegree& prev_vertex_degree,
                       const VectorVertexDegree& current_vertex_degree,
                       const VectorVertexDegree& next_vertex_degree) {
  Curve curve;

  // 计算当前角点（B 点）实际渲染半径
  float render_corner_radius = calculate_line_to_line_corner_radius(
      prev_vertex_degree, current_vertex_degree, next_vertex_degree);
  if (is_equal(render_corner_radius, 0.f)) {  // 如果渲染半径为 0，直接返回
    return curve;
  }

  // 计算当前角点（B 点）角度
  float degree_b = current_vertex_degree.degree;
  // 计算当前角点（B 点）到倒角拐点的距离
  float corner_length = calculate_corner_length(render_corner_radius, degree_b);
  // 计算控制点和节点间的距离
  float control_distance = calculate_arc_sector_control_distance(
      render_corner_radius, static_cast<float>(M_PI - degree_b));

  // 计算 AB、BC 线段的长度
  float ab =
      calculate_distance_between_points(prev_vertex_degree.vertex, current_vertex_degree.vertex);
  float bc =
      calculate_distance_between_points(next_vertex_degree.vertex, current_vertex_degree.vertex);
  // 计算节点 P0, P1 （P0 在 AB 线段上， P1 在 BC 线段上）
  curve.from = {
      current_vertex_degree.vertex.x +
          (prev_vertex_degree.vertex.x - current_vertex_degree.vertex.x) * corner_length / ab,
      current_vertex_degree.vertex.y +
          (prev_vertex_degree.vertex.y - current_vertex_degree.vertex.y) * corner_length / ab};
  curve.to = {
      current_vertex_degree.vertex.x +
          (next_vertex_degree.vertex.x - current_vertex_degree.vertex.x) * corner_length / bc,
      current_vertex_degree.vertex.y +
          (next_vertex_degree.vertex.y - current_vertex_degree.vertex.y) * corner_length / bc};
  // 计算控制点 C0, C1
  curve.control_from = {curve.from.x + (current_vertex_degree.vertex.x - curve.from.x) *
                                           control_distance / corner_length,
                        curve.from.y + (current_vertex_degree.vertex.y - curve.from.y) *
                                           control_distance / corner_length};
  curve.control_to = {
      curve.to.x + (current_vertex_degree.vertex.x - curve.to.x) * control_distance / corner_length,
      curve.to.y +
          (current_vertex_degree.vertex.y - curve.to.y) * control_distance / corner_length};
  curve.is_bezier = true;

  return curve;
}

tgfx::Point calculate_point_on_segment_coordinates(const tgfx::Point& start, const tgfx::Point& end,
                                                   const float distance_from_start) {
  float segment_length = calculate_distance_between_points(start, end);
  float ratio = distance_from_start / segment_length;
  return tgfx::Point::Make(start.x + (end.x - start.x) * ratio,
                           start.y + (end.y - start.y) * ratio);
}

Curve get_polygon_line_curve(const VectorVertexDegree& from, const VectorVertexDegree& to,
                             const float from_render_radius, const float to_render_radius) {
  auto from_point = tgfx::Point::Make(from.vertex.x, from.vertex.y);
  auto to_point = tgfx::Point::Make(to.vertex.x, to.vertex.y);
  // 计算拐角长度
  float from_corner_length = calculate_corner_length(from_render_radius, from.degree);
  float to_corner_length = calculate_corner_length(to_render_radius, to.degree);
  return Curve{
      .from = calculate_point_on_segment_coordinates(from_point, to_point, from_corner_length),
      .to = calculate_point_on_segment_coordinates(to_point, from_point, to_corner_length),
      .is_bezier = false};
}

CurvesParam calculate_star_param(const tgfx::Point& size, const uint32_t count, const float ratio,
                                 const float corner_radius) {
  CurvesParam param;
  // 计算顶点与顶点角度的数据结构集
  std::vector<VectorVertexDegree> vertex_degrees;
  calculate_star_vertex_degrees(size, count, ratio, corner_radius, vertex_degrees);
  // 计算各顶点渲染半径
  std::vector<float> corner_render_radii;
  calculate_polygon_corner_radii(vertex_degrees, corner_render_radii);

  // 构建多边形路径
  uint32_t vertices_count = static_cast<uint32_t>(vertex_degrees.size());
  if (has_corner_radius(vertex_degrees[0].vertex)) {
    // 有圆角情况
    for (uint32_t current_vertex_index = 0; current_vertex_index < vertices_count;
         current_vertex_index++) {
      auto prev_vertex_degree =
          vertex_degrees[(current_vertex_index - 1 + vertices_count) % vertices_count];
      auto current_vertex_degree = vertex_degrees[current_vertex_index];
      auto next_vertex_degree = vertex_degrees[(current_vertex_index + 1) % vertices_count];
      auto current_corner_radius = corner_render_radii[current_vertex_index];
      auto next_corner_radius = corner_render_radii[(current_vertex_index + 1) % vertices_count];
      // 计算倒角路径
      param.curves.push_back(
          fit_corner_curve(prev_vertex_degree, current_vertex_degree, next_vertex_degree));
      // 计算直线路径
      param.curves.push_back(get_polygon_line_curve(current_vertex_degree, next_vertex_degree,
                                                    current_corner_radius, next_corner_radius));
    }
  } else {
    // 无圆角情况
    for (uint32_t current_vertex_index = 0; current_vertex_index < vertices_count;
         current_vertex_index++) {
      auto current_vertex_degree = vertex_degrees[current_vertex_index];
      auto next_vertex_degree = vertex_degrees[(current_vertex_index + 1) % vertices_count];
      // 计算直线路径
      param.curves.push_back(
          get_polygon_line_curve(current_vertex_degree, next_vertex_degree, 0, 0));
    }
  }

  param.is_closed = true;
  return param;
}

std::shared_ptr<tgfx::Shape> create_curves_shape(const std::vector<CurvesParam>& params) {
  tgfx::Path path;

  // 每一组 curves 是一个封闭路径
  for (const auto& [curves, is_closed] : params) {
    // 每一个 curve 是一条线段
    for (size_t i = 0; i < curves.size(); i++) {
      const auto& [from, to, control_from, control_to, is_bezier] = curves[i];

      if (i == 0) {
        path.moveTo(from.x, from.y);
      }

      if (is_bezier) {
        path.cubicTo(control_from.x, control_from.y, control_to.x, control_to.y, to.x, to.y);
      } else {
        path.lineTo(to.x, to.y);
      }
    }

    if (is_closed) {
      path.close();
    }
  }

  return tgfx::Shape::MakeFrom(path);
}
//
// float computePathArea(const Path& path) {
//
//   bool hasFirstPoint = false;
//   float area = 0.0f;
//
//   for (;;) {
//     SkPath::Verb verb = iter.next(pts);
//     if (verb == SkPath::kDone_Verb) break;
//
//
//   return area * 0.5f;
// }

bool isPathCCW(const Path& path) {
  return computePathArea(path) > 0;
}

TGFX_TEST(CanvasTest, dash2) {

  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 400);

  auto vector = tgfx::Point::Make(194.f, 253.00001525878906f);
  auto point_count = 5;
  auto inner_radius = 0.81f;
  auto corner_radius = 1.f;
  auto params =
      calculate_star_param(vector, static_cast<uint32_t>(point_count), inner_radius, corner_radius);
  auto shape = create_curves_shape({params});

  auto path = shape->getPath();
  float dash_array[] = {2, 2};
  auto effect = PathEffect::MakeDash(dash_array, 2, 1, true);
  effect->filterPath(&path);
  auto dashPath = path;
  Stroke stroke(2.0f);
  stroke.applyToPath(&path);
  PathIterator iter = [](PathVerb verb, const Point points[4], void*) {
    switch (verb) {
      case PathVerb::Move:
        firstPoint = pts[0];
      prevPoint = pts[0];
      hasFirstPoint = true;
      break;
      case PathVerb::Line:
        if (hasFirstPoint) {
          area += (prevPoint.x() * pts[0].y() - pts[0].x() * prevPoint.y());
          prevPoint = pts[0];
        }
      break;
      case PathVerb::Close:
        if (hasFirstPoint) {
          area += (prevPoint.x() * firstPoint.y() - firstPoint.x() * prevPoint.y());
        }
      break;
      // 对于曲线，可以用分段线近似，或者忽略
      default:
        // 复杂曲线可用细分近似
          break;
    }
  }
  };
  // path.decompose(iter);

  auto SVGStream = MemoryWriteStream::Make();
  auto exporter = SVGExporter::Make(SVGStream, context, Rect::MakeWH(200, 200),
                                    SVGExportFlags::DisablePrettyXML);
  auto* canvas = exporter->getCanvas();
  auto shapePath = shape->getPath();
  // shapePath.decompose(iter);

  path.addPath(dashPath, PathOp::Difference);
  canvas->drawPath(path, Paint());

  exporter->close();

  auto SVGString = SVGStream->readString();
  LOGE(SVGString.c_str());

  LOGE("----");



  // auto shapeLayer = tgfx::ShapeLayer::Make();
  // shapeLayer->setShape(shape);
  // shapeLayer->setLineWidth(1.0f);
  // shapeLayer->setStrokeAlign(tgfx::StrokeAlign::Outside);
  // shapeLayer->setStrokeStyle(tgfx::SolidColor::Make(tgfx::Color::Black()));
  // shapeLayer->setLineDashAdaptive(true);
  // shapeLayer->setLineDashPattern({2, 2});
  // shapeLayer->setLineDashPhase(1);
  //
  // DisplayList displayList;
  // displayList.root()->addChild(shapeLayer);
   canvas = surface->getCanvas();
  Paint paint;
  paint.setColor(tgfx::Color::Black());
  canvas->drawPath(path, paint);
  // canvas->drawPath(path, paint);
  // displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/dash2"));
}

}  // namespace tgfx

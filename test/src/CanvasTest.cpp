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
#include "core/PictureRecords.h"
#include "core/images/CodecImage.h"
#include "core/images/RasterizedImage.h"
#include "core/images/SubsetImage.h"
#include "core/images/TransformImage.h"
#include "core/shapes/AppendShape.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"
#include "gpu/RenderContext.h"
#include "gpu/opengl/GLFunctions.h"
#include "gpu/opengl/GLGPU.h"
#include "gpu/ops/RRectDrawOp.h"
#include "gpu/ops/RectDrawOp.h"
#include "gpu/resources/TextureView.h"
#include "gtest/gtest.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/core/Surface.h"
#include "tgfx/platform/ImageReader.h"
#include "tgfx/svg/SVGPathParser.h"
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
  EXPECT_EQ(static_cast<RRectDrawOp*>(task->drawOps.back().get())->rectCount, drawCallCount);
  context->flushAndSubmit();
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
  context->flushAndSubmit();
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
  auto defaultExpirationFrames = context->resourceExpirationFrames();
  context->setResourceExpirationFrames(1);
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  auto rasterImage = image->makeRasterized();
  EXPECT_TRUE(rasterImage == image);
  image = MakeImage("resources/apitest/rotation.jpg");
  rasterImage = ScaleImage(image, 0.15f)->makeRasterized();
  EXPECT_FALSE(rasterImage->hasMipmaps());
  EXPECT_FALSE(rasterImage == image);
  EXPECT_EQ(rasterImage->width(), 454);
  EXPECT_EQ(rasterImage->height(), 605);
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, 1100, 1400);
  auto canvas = surface->getCanvas();
  canvas->drawImage(rasterImage, 100, 100);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/rasterized"));
  auto rasterImageUniqueKey =
      std::static_pointer_cast<RasterizedImage>(rasterImage)->getTextureKey();
  auto textureView = Resource::Find<TextureView>(context, rasterImageUniqueKey);
  ASSERT_TRUE(textureView != nullptr);
  EXPECT_TRUE(textureView != nullptr);
  EXPECT_EQ(textureView->width(), 454);
  EXPECT_EQ(textureView->height(), 605);
  auto source = std::static_pointer_cast<TransformImage>(image)->source;
  auto imageUniqueKey = std::static_pointer_cast<RasterizedImage>(source)->getTextureKey();
  textureView = Resource::Find<TextureView>(context, imageUniqueKey);
  EXPECT_TRUE(textureView == nullptr);
  canvas->clear();
  image = image->makeMipmapped(true);
  EXPECT_TRUE(image->hasMipmaps());
  SamplingOptions sampling(FilterMode::Linear, MipmapMode::Linear);
  auto scaledImage = ScaleImage(image, 0.15f, sampling);
  rasterImage = scaledImage->makeRasterized();
  EXPECT_TRUE(rasterImage->hasMipmaps());
  canvas->drawImage(rasterImage, 100, 100);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/rasterized_mipmap"));
  textureView = Resource::Find<TextureView>(context, rasterImageUniqueKey);
  EXPECT_TRUE(textureView == nullptr);
  rasterImageUniqueKey = std::static_pointer_cast<RasterizedImage>(rasterImage)->getTextureKey();
  textureView = Resource::Find<TextureView>(context, rasterImageUniqueKey);
  EXPECT_TRUE(textureView != nullptr);
  canvas->clear();
  scaledImage = scaledImage->makeMipmapped(false);
  EXPECT_FALSE(scaledImage->hasMipmaps());
  rasterImage = scaledImage->makeScaled(907, 1210, sampling)->makeRasterized();
  EXPECT_FALSE(rasterImage->hasMipmaps());
  rasterImage = rasterImage->makeMipmapped(true);
  EXPECT_TRUE(rasterImage->hasMipmaps());
  EXPECT_EQ(rasterImage->width(), 907);
  EXPECT_EQ(rasterImage->height(), 1210);
  canvas->drawImage(rasterImage, 100, 100);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/rasterized_scale_up"));
  context->setResourceExpirationFrames(defaultExpirationFrames);
}

TGFX_TEST(CanvasTest, mipmap) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto codec = MakeImageCodec("resources/apitest/rotation.jpg");
  ASSERT_TRUE(codec != nullptr);
  Bitmap bitmap(codec->width(), codec->height(), false, false, codec->colorSpace());
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

static GLTextureInfo CreateRectangleTexture(Context* context, int width, int height) {
  auto gl = static_cast<GLGPU*>(context->gpu())->functions();
  GLTextureInfo glInfo = {};
  gl->genTextures(1, &(glInfo.id));
  if (glInfo.id == 0) {
    return {};
  }
  glInfo.target = GL_TEXTURE_RECTANGLE;
  gl->bindTexture(glInfo.target, glInfo.id);
  auto gpu = static_cast<GLGPU*>(context->gpu());
  const auto& textureFormat = gpu->caps()->getTextureFormat(PixelFormat::RGBA_8888);
  gl->texImage2D(glInfo.target, 0, static_cast<int>(textureFormat.internalFormatTexImage), width,
                 height, 0, textureFormat.externalFormat, textureFormat.externalType, nullptr);
  return glInfo;
}

TGFX_TEST(CanvasTest, TileModeFallback) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto codec = MakeImageCodec("resources/apitest/rotation.jpg");
  ASSERT_TRUE(codec != nullptr);
  Bitmap bitmap(codec->width(), codec->height(), false, false, codec->colorSpace());
  ASSERT_FALSE(bitmap.isEmpty());
  auto pixels = bitmap.lockPixels();
  ASSERT_TRUE(pixels != nullptr);
  auto result = codec->readPixels(bitmap.info(), pixels);
  ASSERT_TRUE(result);
  auto gpu = static_cast<GLGPU*>(context->gpu());
  auto gl = gpu->functions();
  GLTextureInfo glInfo = CreateRectangleTexture(context, bitmap.width(), bitmap.height());
  ASSERT_TRUE(glInfo.id != 0);
  const auto& textureFormat =
      gpu->caps()->getTextureFormat(ColorTypeToPixelFormat(bitmap.colorType()));
  gl->texImage2D(glInfo.target, 0, static_cast<int>(textureFormat.internalFormatTexImage),
                 bitmap.width(), bitmap.height(), 0, textureFormat.externalFormat,
                 textureFormat.externalType, pixels);
  bitmap.unlockPixels();
  BackendTexture backendTexture(glInfo, bitmap.width(), bitmap.height());
  auto image = Image::MakeFrom(context, backendTexture, ImageOrigin::TopLeft, bitmap.colorSpace());
  ASSERT_TRUE(image != nullptr);
  image = image->makeOriented(codec->orientation());
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
  gl->deleteTextures(1, &glInfo.id);
}

TGFX_TEST(CanvasTest, hardwareMipmap) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto codec = MakeImageCodec("resources/apitest/rotation.jpg");
  ASSERT_TRUE(codec != nullptr);
  Bitmap bitmap(codec->width(), codec->height(), false, true, codec->colorSpace());
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
  context->flushAndSubmit();
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
  EXPECT_TRUE(decodedImage == image);
  context->flushAndSubmit();
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
  canvas->setMatrix(matrix);
  canvas->drawImage(rotationImage);
  subset = rotationImage->makeSubset(Rect::MakeXYWH(500, 800, 2000, 2400));
  ASSERT_TRUE(subset != nullptr);
  matrix.postTranslate(160, 30);
  canvas->setMatrix(matrix);
  canvas->drawImage(subset);
  subset = subset->makeSubset(Rect::MakeXYWH(400, 500, 1600, 1900));
  ASSERT_TRUE(subset != nullptr);
  matrix.postTranslate(110, -30);
  canvas->setMatrix(matrix);
  canvas->drawImage(subset);
  subset = subset->makeOriented(Orientation::RightTop);
  textureImage = subset->makeTextureImage(context);
  ASSERT_TRUE(textureImage != nullptr);
  matrix.postTranslate(0, 110);
  SamplingOptions sampling(FilterMode::Linear, MipmapMode::None);
  canvas->setMatrix(matrix);
  canvas->drawImage(textureImage, sampling);
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
  canvas->setMatrix(matrix);
  canvas->drawImage(rgbAAA);
  subset = rgbAAA->makeSubset(Rect::MakeXYWH(100, 100, 300, 200));
  matrix.postTranslate(140, 5);
  canvas->setMatrix(matrix);
  canvas->drawImage(subset);
  auto originImage = subset->makeOriented(Orientation::BottomLeft);
  EXPECT_TRUE(originImage != nullptr);
  matrix.postTranslate(0, 70);
  canvas->setMatrix(matrix);
  canvas->drawImage(originImage);
  rgbAAA = image->makeRGBAAA(512, 512, 0, 0);
  EXPECT_EQ(rgbAAA->width(), 512);
  EXPECT_EQ(rgbAAA->height(), 512);
  matrix.postTranslate(110, -75);
  canvas->setMatrix(matrix);
  canvas->drawImage(rgbAAA);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/drawImage"));
}

TGFX_TEST(CanvasTest, drawImageRect) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);

  int width = 400;
  int height = 400;
  auto surface = Surface::Make(context, width, height);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  Rect srcRect = Rect::MakeWH(image->width(), image->height());
  Rect dstRect = Rect::MakeXYWH(0, 0, width / 2, height / 2);
  canvas->drawImageRect(image, srcRect, dstRect, SamplingOptions(FilterMode::Linear));

  srcRect = Rect::MakeXYWH(20, 20, 60, 60);
  dstRect = Rect::MakeXYWH(width / 2, 0, width / 2, height / 2);
  canvas->drawImageRect(image, srcRect, dstRect, SamplingOptions(FilterMode::Nearest));

  srcRect = Rect::MakeXYWH(40, 40, 40, 40);
  dstRect = Rect::MakeXYWH(0, height / 2, width, height / 2);
  canvas->drawImageRect(image, srcRect, dstRect, SamplingOptions(FilterMode::Linear));

  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/drawImageRect"));
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
                                  AlphaType::Premultiplied, 0, imageCodec->colorSpace());
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

TGFX_TEST(CanvasTest, rectangleTextureAsBlendDst) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto glInfo = CreateRectangleTexture(context, 110, 110);
  ASSERT_TRUE(glInfo.id > 0);
  auto backendTexture = BackendTexture(glInfo, 110, 110);
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
  auto gl = static_cast<GLGPU*>(context->gpu())->functions();
  gl->deleteTextures(1, &(glInfo.id));
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

  auto bounds = picture->getTightBounds();
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
  canvas->drawSimpleText("Hello TGFX~", 0, 0, font, paint);
  auto textRecord = recorder.finishRecordingAsPicture();
  bounds = textRecord->getTightBounds();
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

class ColorModifier : public BrushModifier {
 public:
  explicit ColorModifier(Color color) : color(color) {
  }

  Brush transform(const Brush& brush) const override {
    auto newBrush = brush;
    newBrush.color = color;
    newBrush.color.alpha *= brush.color.alpha;
    return newBrush;
  }

 private:
  Color color = {};
};

TGFX_TEST(CanvasTest, BrushModifier) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  // Record a rectangle with default fill
  PictureRecorder recorder = {};
  auto canvas = recorder.beginRecording();
  Paint paint;
  paint.setColor(Color::Red());
  paint.setAlpha(0.5f);
  canvas->drawRect(Rect::MakeXYWH(10, 10, 100, 100), paint);
  auto picture = recorder.finishRecordingAsPicture();
  ASSERT_TRUE(picture != nullptr);
  auto surface = Surface::Make(context, 120, 120);
  canvas = surface->getCanvas();
  canvas->clear(Color::White());
  canvas->scale(0.8f, 0.8f);
  canvas->translate(15, 15);
  ColorModifier colorModifier(Color::Green());
  picture->playback(canvas, &colorModifier);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/BrushModifier"));
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

TGFX_TEST(CanvasTest, CornerEffectCompare) {
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

  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/CornerEffectCompare"));
}

TGFX_TEST(CanvasTest, CornerTest) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/CornerShape"));
  canvas->clear();
  auto doubleCornerRectShape = Shape::ApplyEffect(cornerRectshape, pathEffect);
  auto doubleCornerTriShape = Shape::ApplyEffect(cornerTriShape, pathEffect);
  canvas->drawShape(doubleCornerRectShape, paint);
  canvas->drawShape(doubleCornerTriShape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/CornerShapeDouble"));
  canvas->clear();
  auto tripleCornerRectShape = Shape::ApplyEffect(doubleCornerRectShape, pathEffect);
  auto tripleCornerTriShape = Shape::ApplyEffect(doubleCornerTriShape, pathEffect);
  canvas->drawShape(tripleCornerRectShape, paint);
  canvas->drawShape(tripleCornerTriShape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/CornerShapeTriple"));

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
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/CloseQuadShape"));
  canvas->clear();
  auto cornerCloseQuadShape = Shape::ApplyEffect(closeQuadShape, pathEffect);
  canvas->drawShape(cornerCloseQuadShape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/CloseQuadShapeCorner"));

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
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/OpenQuadShape"));
  canvas->clear();
  auto cornerOpenQuadShape = Shape::ApplyEffect(openQuadShape, pathEffect);
  canvas->drawShape(cornerOpenQuadShape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/OpenQuadShapeCorner"));

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
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/OpenConicShape"));
  canvas->clear();
  auto cornerOpenConicShape = Shape::ApplyEffect(openConicShape, pathEffect);
  canvas->drawShape(cornerOpenConicShape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/OpenConicShapeCorner"));

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
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/QuadRectShape"));

  canvas->clear();
  auto cornerShape = Shape::ApplyEffect(quadShape, pathEffect);
  canvas->drawShape(cornerShape, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/QuadRectShapeCorner"));
}

TGFX_TEST(CanvasTest, textEmojiMixedBlendModes1) {
  auto serifTypeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  ASSERT_TRUE(serifTypeface != nullptr);

  std::string mixedText = "Hello TGFX! 🎨🎉😊🌟✨🚀💻❤️";

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  int surfaceWidth = 1200;
  int surfaceHeight = 800;
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  // Create gradient background
  canvas->clear(Color::White());
  auto backgroundPaint = Paint();
  auto colors = {Color::FromRGBA(255, 200, 200, 255), Color::FromRGBA(200, 200, 255, 255)};
  auto positions = {0.0f, 1.0f};
  auto shader = Shader::MakeLinearGradient(
      Point::Make(0, 0), Point::Make(surfaceWidth, surfaceHeight), colors, positions);
  backgroundPaint.setShader(shader);
  canvas->drawRect(Rect::MakeWH(surfaceWidth, surfaceHeight), backgroundPaint);

  float fontSize = 32.f;
  float lineHeight = fontSize * 1.5f;
  float startY = 60.f;

  // Test different blend modes
  std::vector<BlendMode> blendModes = {
      BlendMode::SrcOver,   BlendMode::SrcIn,     BlendMode::Src,        BlendMode::Overlay,
      BlendMode::Darken,    BlendMode::Lighten,   BlendMode::ColorDodge, BlendMode::ColorBurn,
      BlendMode::HardLight, BlendMode::SoftLight, BlendMode::Difference, BlendMode::Exclusion};

  std::vector<std::string> blendModeNames = {"SrcOver",   "Multiply",  "Screen",     "Overlay",
                                             "Darken",    "Lighten",   "ColorDodge", "ColorBurn",
                                             "HardLight", "SoftLight", "Difference", "Exclusion"};

  for (size_t modeIndex = 0; modeIndex < blendModes.size(); ++modeIndex) {
    auto blendMode = blendModes[modeIndex];
    auto modeName = blendModeNames[modeIndex];

    float y = startY + modeIndex * lineHeight;
    float x = 20.f;

    // Draw blend mode label
    Paint labelPaint;
    labelPaint.setColor(Color::Black());
    auto labelFont = Font(serifTypeface, 16.f);
    canvas->drawSimpleText(modeName, x, y - 8, labelFont, labelPaint);

    // Process text using TextShaper for proper emoji handling
    auto positionedGlyphs = TextShaper::Shape(mixedText, serifTypeface);

    struct TextRun {
      std::vector<GlyphID> ids;
      std::vector<Point> positions;
      Font font;
    };
    std::vector<TextRun> textRuns;
    TextRun* run = nullptr;
    auto count = positionedGlyphs.glyphCount();
    float textX = x + 120;

    for (size_t i = 0; i < count; ++i) {
      auto typeface = positionedGlyphs.getTypeface(i);
      if (run == nullptr || run->font.getTypeface() != typeface) {
        textRuns.emplace_back();
        run = &textRuns.back();
        run->font = Font(typeface, fontSize);
      }
      auto glyphID = positionedGlyphs.getGlyphID(i);
      run->ids.emplace_back(glyphID);
      run->positions.push_back(Point{textX, y});
      textX += run->font.getAdvance(glyphID);
    }

    // Draw mixed text with current blend mode using proper glyph rendering
    Paint textPaint;
    textPaint.setColor(Color::FromRGBA(255, 100, 50, 200));
    textPaint.setBlendMode(blendMode);

    for (const auto& textRun : textRuns) {
      canvas->drawGlyphs(textRun.ids.data(), textRun.positions.data(), textRun.ids.size(),
                         textRun.font, textPaint);
    }
  }

  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/textEmojiMixedBlendModes"));
}

TGFX_TEST(CanvasTest, textEmojiMixedBlendModes2) {
  auto serifTypeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  ASSERT_TRUE(serifTypeface != nullptr);

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  int surfaceWidth = 600;
  int surfaceHeight = 400;
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  // Create colorful background with circles
  canvas->clear(Color::FromRGBA(240, 240, 255, 255));

  // Test emoji and text with different blend modes in layers
  std::vector<std::pair<std::string, BlendMode>> textBlendPairs = {{"🎨Art", BlendMode::SrcOver},
                                                                   {"🎨Art", BlendMode::SrcIn},
                                                                   {"🎭Mix", BlendMode::Src},
                                                                   {"🚀Fast", BlendMode::SrcATop},
                                                                   {"🎪Fun", BlendMode::SrcOut}};

  float fontSize = 36.f;

  for (size_t i = 0; i < textBlendPairs.size(); ++i) {
    auto& pair = textBlendPairs[i];
    auto& text = pair.first;
    auto blendMode = pair.second;

    float x = 50 + (i % 3) * 180;
    float y = 120 + (i / 3) * 120;

    // Process text using TextShaper for proper emoji handling
    auto positionedGlyphs = TextShaper::Shape(text, serifTypeface);

    struct TextRun {
      std::vector<GlyphID> ids;
      std::vector<Point> positions;
      Font font;
    };
    std::vector<TextRun> textRuns;
    TextRun* run = nullptr;
    auto count = positionedGlyphs.glyphCount();
    float textX = x;

    for (size_t j = 0; j < count; ++j) {
      auto typeface = positionedGlyphs.getTypeface(j);
      if (run == nullptr || run->font.getTypeface() != typeface) {
        textRuns.emplace_back();
        run = &textRuns.back();
        run->font = Font(typeface, fontSize);
      }
      auto glyphID = positionedGlyphs.getGlyphID(j);
      run->ids.emplace_back(glyphID);
      run->positions.push_back(Point{textX, y});
      textX += run->font.getAdvance(glyphID);
    }

    Paint textPaint;
    textPaint.setColor(Color::FromRGBA(255, 50, 100, 220));
    textPaint.setBlendMode(blendMode);

    for (const auto& textRun : textRuns) {
      canvas->drawGlyphs(textRun.ids.data(), textRun.positions.data(), textRun.ids.size(),
                         textRun.font, textPaint);
    }
  }

  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/textEmojiMixedBlendModes2"));
}

TGFX_TEST(CanvasTest, complexEmojiTextBlending) {
  auto serifTypeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  ASSERT_TRUE(serifTypeface != nullptr);

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  int surfaceWidth = 800;
  int surfaceHeight = 600;
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  // Create complex background pattern
  canvas->clear(Color::White());

  // Draw gradient rectangles as background
  for (int i = 0; i < 8; ++i) {
    for (int j = 0; j < 6; ++j) {
      Paint rectPaint;
      auto hue = (i * 45 + j * 30) % 360;
      // Convert HSL to RGB approximation
      auto r = static_cast<uint8_t>(128 + 100 * sin(hue * 3.14159f / 180.0f));
      auto g = static_cast<uint8_t>(128 + 100 * sin((hue + 120) * 3.14159f / 180.0f));
      auto b = static_cast<uint8_t>(128 + 100 * sin((hue + 240) * 3.14159f / 180.0f));
      auto color = Color::FromRGBA(r, g, b, 77);  // 0.3f * 255
      rectPaint.setColor(color);
      canvas->drawRect(Rect::MakeXYWH(i * 100, j * 100, 100, 100), rectPaint);
    }
  }

  // Complex text with various emoji sequences
  std::vector<std::string> complexTexts = {"👨‍👩‍👧‍👦Family测试",
                                           "🏳️‍🌈Flag🇨🇳China",
                                           "👨🏼‍🦱Hair👩🏾‍💻Code",
                                           "🤡🎭🎪🎨艺术Art",
                                           "🌍🌎🌏World世界",
                                           "🎵🎶🎼音乐Music"};

  std::vector<BlendMode> complexBlendModes = {BlendMode::Multiply,   BlendMode::Screen,
                                              BlendMode::Overlay,    BlendMode::SoftLight,
                                              BlendMode::Difference, BlendMode::ColorBurn};

  float fontSize = 28.f;

  for (size_t i = 0; i < complexTexts.size(); ++i) {
    auto& text = complexTexts[i];
    auto blendMode = complexBlendModes[i];

    float x = 20 + (i % 2) * 380;
    float y = 80 + (i / 2) * 100;

    // Process text using TextShaper for proper emoji handling
    auto positionedGlyphs = TextShaper::Shape(text, serifTypeface);

    struct TextRun {
      std::vector<GlyphID> ids;
      std::vector<Point> positions;
      Font font;
    };
    std::vector<TextRun> textRuns;
    TextRun* run = nullptr;
    auto count = positionedGlyphs.glyphCount();
    float textX = x;

    for (size_t j = 0; j < count; ++j) {
      auto typeface = positionedGlyphs.getTypeface(j);
      if (run == nullptr || run->font.getTypeface() != typeface) {
        textRuns.emplace_back();
        run = &textRuns.back();
        run->font = Font(typeface, fontSize);
      }
      auto glyphID = positionedGlyphs.getGlyphID(j);
      run->ids.emplace_back(glyphID);
      run->positions.push_back(Point{textX, y});
      textX += run->font.getAdvance(glyphID);
    }

    // Draw text with blend mode
    Paint textPaint;
    textPaint.setColor(Color::FromRGBA(40, 80, 160, 255));
    textPaint.setBlendMode(blendMode);

    for (const auto& textRun : textRuns) {
      canvas->drawGlyphs(textRun.ids.data(), textRun.positions.data(), textRun.ids.size(),
                         textRun.font, textPaint);
    }

    // Draw blend mode label
    Paint labelPaint;
    labelPaint.setColor(Color::Black());
    auto labelFont = Font(serifTypeface, 12.f);
    std::string label = "BlendMode: " + std::to_string(static_cast<int>(blendMode));
    canvas->drawSimpleText(label, x, y + 15, labelFont, labelPaint);
  }

  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/complexEmojiTextBlending"));
}

TGFX_TEST(CanvasTest, emojiTextStrokeBlending) {
  auto serifTypeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  ASSERT_TRUE(serifTypeface != nullptr);
  auto emojiTypeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoColorEmoji.ttf"));
  ASSERT_TRUE(emojiTypeface != nullptr);

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  int surfaceWidth = 700;
  int surfaceHeight = 500;
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  // Rainbow gradient background
  canvas->clear(Color::Black());
  auto colors = {Color::FromRGBA(255, 0, 0, 255),   Color::FromRGBA(255, 127, 0, 255),
                 Color::FromRGBA(255, 255, 0, 255), Color::FromRGBA(0, 255, 0, 255),
                 Color::FromRGBA(0, 0, 255, 255),   Color::FromRGBA(75, 0, 130, 255),
                 Color::FromRGBA(148, 0, 211, 255)};
  auto positions = {0.0f, 0.16f, 0.33f, 0.5f, 0.66f, 0.83f, 1.0f};
  auto shader = Shader::MakeLinearGradient(Point::Make(0, 0), Point::Make(0, surfaceHeight), colors,
                                           positions);
  Paint bgPaint;
  bgPaint.setShader(shader);
  canvas->drawRect(Rect::MakeWH(surfaceWidth, surfaceHeight), bgPaint);

  // Test stroke and fill with different blend modes
  std::string emojiText = "🎨🌈🎭🎪🚀";
  std::string normalText = "ArtRainbowMask";

  float fontSize = 48.f;

  std::vector<BlendMode> strokeBlendModes = {BlendMode::SrcOver, BlendMode::Multiply,
                                             BlendMode::Screen, BlendMode::Overlay,
                                             BlendMode::Difference};

  for (size_t i = 0; i < strokeBlendModes.size(); ++i) {
    auto blendMode = strokeBlendModes[i];
    float y = 80 + i * 80;

    // Process emoji text using TextShaper
    auto emojiPositionedGlyphs = TextShaper::Shape(emojiText, emojiTypeface);
    struct TextRun {
      std::vector<GlyphID> ids;
      std::vector<Point> positions;
      Font font;
    };
    std::vector<TextRun> emojiTextRuns;
    TextRun* emojiRun = nullptr;
    auto emojiCount = emojiPositionedGlyphs.glyphCount();
    float emojiX = 50;

    for (size_t j = 0; j < emojiCount; ++j) {
      auto typeface = emojiPositionedGlyphs.getTypeface(j);
      if (emojiRun == nullptr || emojiRun->font.getTypeface() != typeface) {
        emojiTextRuns.emplace_back();
        emojiRun = &emojiTextRuns.back();
        emojiRun->font = Font(typeface, fontSize);
      }
      auto glyphID = emojiPositionedGlyphs.getGlyphID(j);
      emojiRun->ids.emplace_back(glyphID);
      emojiRun->positions.push_back(Point{emojiX, y});
      emojiX += emojiRun->font.getAdvance(glyphID);
    }

    Paint emojiPaint;
    emojiPaint.setBlendMode(blendMode);

    for (const auto& textRun : emojiTextRuns) {
      canvas->drawGlyphs(textRun.ids.data(), textRun.positions.data(), textRun.ids.size(),
                         textRun.font, emojiPaint);
    }

    // Process normal text using TextShaper
    auto normalPositionedGlyphs = TextShaper::Shape(normalText, serifTypeface);
    std::vector<TextRun> normalTextRuns;
    TextRun* normalRun = nullptr;
    auto normalCount = normalPositionedGlyphs.glyphCount();
    float normalX = 350;

    for (size_t j = 0; j < normalCount; ++j) {
      auto typeface = normalPositionedGlyphs.getTypeface(j);
      if (normalRun == nullptr || normalRun->font.getTypeface() != typeface) {
        normalTextRuns.emplace_back();
        normalRun = &normalTextRuns.back();
        normalRun->font = Font(typeface, fontSize);
      }
      auto glyphID = normalPositionedGlyphs.getGlyphID(j);
      normalRun->ids.emplace_back(glyphID);
      normalRun->positions.push_back(Point{normalX, y});
      normalX += normalRun->font.getAdvance(glyphID);
    }

    // Draw normal text for comparison
    Paint textStrokePaint;
    textStrokePaint.setColor(Color::Green());
    textStrokePaint.setStyle(PaintStyle::Stroke);
    textStrokePaint.setStrokeWidth(2.0f);
    textStrokePaint.setBlendMode(blendMode);

    Paint textFillPaint;
    textFillPaint.setColor(Color::FromRGBA(100, 150, 255, 200));
    textFillPaint.setBlendMode(blendMode);

    for (const auto& textRun : normalTextRuns) {
      canvas->drawGlyphs(textRun.ids.data(), textRun.positions.data(), textRun.ids.size(),
                         textRun.font, textStrokePaint);
      canvas->drawGlyphs(textRun.ids.data(), textRun.positions.data(), textRun.ids.size(),
                         textRun.font, textFillPaint);
    }
  }

  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/emojiTextStrokeBlending"));
}

TGFX_TEST(CanvasTest, textEmojiOverlayBlendModes) {
  auto serifTypeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  ASSERT_TRUE(serifTypeface != nullptr);
  auto emojiTypeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoColorEmoji.ttf"));
  ASSERT_TRUE(emojiTypeface != nullptr);

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  int surfaceWidth = 1200;
  int surfaceHeight = 900;
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  // Create striped background
  canvas->clear(Color::FromRGBA(230, 230, 250, 255));
  Paint stripePaint;
  stripePaint.setColor(Color::FromRGBA(200, 220, 240, 255));
  for (int i = 0; i < surfaceHeight; i += 20) {
    if ((i / 20) % 2 == 0) {
      canvas->drawRect(Rect::MakeXYWH(0, i, surfaceWidth, 20), stripePaint);
    }
  }

  float fontSize = 36.f;
  float lineHeight = 80.f;
  float startY = 60.f;

  // Test different blend modes for emoji overlays on text
  std::vector<BlendMode> blendModes = {
      BlendMode::SrcOver,   BlendMode::SrcIn,     BlendMode::SrcOut,     BlendMode::SrcATop,
      BlendMode::DstOver,   BlendMode::DstIn,     BlendMode::DstOut,     BlendMode::DstATop,
      BlendMode::Xor,       BlendMode::Multiply,  BlendMode::Screen,     BlendMode::Overlay,
      BlendMode::Darken,    BlendMode::Lighten,   BlendMode::ColorDodge, BlendMode::ColorBurn,
      BlendMode::HardLight, BlendMode::SoftLight, BlendMode::Difference, BlendMode::Exclusion};

  std::vector<std::string> blendModeNames = {
      "SrcOver", "SrcIn",      "SrcOut",    "SrcATop",   "DstOver",   "DstIn",      "DstOut",
      "DstATop", "Xor",        "Plus",      "Multiply",  "Screen",    "Overlay",    "Darken",
      "Lighten", "ColorDodge", "ColorBurn", "HardLight", "SoftLight", "Difference", "Exclusion"};

  std::string baseText = "Hello 世界";
  std::string emojiText = "🎨🎉🌟";

  for (size_t modeIndex = 0; modeIndex < blendModes.size(); ++modeIndex) {
    auto blendMode = blendModes[modeIndex];
    auto modeName = blendModeNames[modeIndex];

    float y = startY + (modeIndex / 3) * lineHeight;
    float x = 50.f + (modeIndex % 3) * 380.f;

    // Draw blend mode label
    Paint labelPaint;
    labelPaint.setColor(Color::Black());
    auto labelFont = Font(serifTypeface, 14.f);
    canvas->drawSimpleText(modeName, x, y - 20, labelFont, labelPaint);

    // First draw base text layer
    auto basePositionedGlyphs = TextShaper::Shape(baseText, serifTypeface);

    struct TextRun {
      std::vector<GlyphID> ids;
      std::vector<Point> positions;
      Font font;
    };
    std::vector<TextRun> baseTextRuns;
    TextRun* baseRun = nullptr;
    auto baseCount = basePositionedGlyphs.glyphCount();
    float baseX = x;

    for (size_t i = 0; i < baseCount; ++i) {
      auto typeface = basePositionedGlyphs.getTypeface(i);
      if (baseRun == nullptr || baseRun->font.getTypeface() != typeface) {
        baseTextRuns.emplace_back();
        baseRun = &baseTextRuns.back();
        baseRun->font = Font(typeface, fontSize);
      }
      auto glyphID = basePositionedGlyphs.getGlyphID(i);
      baseRun->ids.emplace_back(glyphID);
      baseRun->positions.push_back(Point{baseX, y});
      baseX += baseRun->font.getAdvance(glyphID);
    }

    // Draw base text with semi-transparent blue
    Paint baseTextPaint;
    baseTextPaint.setColor(Color::FromRGBA(50, 100, 200, 180));
    baseTextPaint.setBlendMode(BlendMode::SrcOver);

    for (const auto& textRun : baseTextRuns) {
      canvas->drawGlyphs(textRun.ids.data(), textRun.positions.data(), textRun.ids.size(),
                         textRun.font, baseTextPaint);
    }

    // Then overlay emoji with different blend modes
    auto emojiPositionedGlyphs = TextShaper::Shape(emojiText, emojiTypeface);
    std::vector<TextRun> emojiTextRuns;
    TextRun* emojiRun = nullptr;
    auto emojiCount = emojiPositionedGlyphs.glyphCount();
    float emojiX = x + 20;

    for (size_t i = 0; i < emojiCount; ++i) {
      auto typeface = emojiPositionedGlyphs.getTypeface(i);
      if (emojiRun == nullptr || emojiRun->font.getTypeface() != typeface) {
        emojiTextRuns.emplace_back();
        emojiRun = &emojiTextRuns.back();
        emojiRun->font = Font(typeface, fontSize);
      }
      auto glyphID = emojiPositionedGlyphs.getGlyphID(i);
      emojiRun->ids.emplace_back(glyphID);
      emojiRun->positions.push_back(Point{emojiX, y + 5});
      emojiX += emojiRun->font.getAdvance(glyphID);
    }

    // Draw overlaid emoji with the current blend mode
    Paint emojiPaint;
    emojiPaint.setColor(Color::FromRGBA(255, 150, 50, 200));
    emojiPaint.setBlendMode(blendMode);

    for (const auto& textRun : emojiTextRuns) {
      canvas->drawGlyphs(textRun.ids.data(), textRun.positions.data(), textRun.ids.size(),
                         textRun.font, emojiPaint);
    }
  }

  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/textEmojiOverlayBlendModes"));
}

TGFX_TEST(CanvasTest, RotateImageRect) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  int surfaceWidth = 100;
  int surfaceHeight = 100;
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  image = image->makeOriented(Orientation::RightBottom);
  ASSERT_TRUE(image != nullptr);

  auto srcRect = Rect::MakeXYWH(20, 20, 40, 40);
  auto dstRect = Rect::MakeXYWH(0, 0, 100, 100);
  canvas->drawImageRect(image, srcRect, dstRect, {}, nullptr, SrcRectConstraint::Strict);
  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/RotateImageRect"));
}

TGFX_TEST(CanvasTest, ScaleImage) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  auto scaledImage = image->makeScaled(image->width(), image->height());
  EXPECT_TRUE(scaledImage == image);
  image = MakeImage("resources/apitest/rotation.jpg");
  scaledImage = ScaleImage(image, 0.15f);
  EXPECT_FALSE(scaledImage->hasMipmaps());
  EXPECT_FALSE(scaledImage == image);
  EXPECT_EQ(scaledImage->width(), 454);
  EXPECT_EQ(scaledImage->height(), 605);
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, 1100, 1400);
  auto canvas = surface->getCanvas();
  canvas->drawImage(scaledImage);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/scaled_image"));
  canvas->clear();
  image = image->makeMipmapped(true);
  EXPECT_TRUE(image->hasMipmaps());
  SamplingOptions sampling(FilterMode::Linear, MipmapMode::Linear);
  scaledImage = ScaleImage(image, 0.15f, sampling);
  EXPECT_TRUE(scaledImage->hasMipmaps());
  canvas->drawImage(scaledImage, 100, 100);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/scaled_mipmap"));
  canvas->clear();
  scaledImage = scaledImage->makeMipmapped(false);
  EXPECT_FALSE(scaledImage->hasMipmaps());
  scaledImage = ScaleImage(scaledImage, 2.0f, sampling);
  EXPECT_FALSE(scaledImage->hasMipmaps());
  scaledImage = scaledImage->makeMipmapped(true);
  EXPECT_TRUE(scaledImage->hasMipmaps());
  EXPECT_EQ(scaledImage->width(), 908);
  EXPECT_EQ(scaledImage->height(), 1210);
  canvas->drawImage(scaledImage);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/scaled_scale_up"));
  canvas->clear();
  canvas->clipRect(Rect::MakeXYWH(100, 100, 500, 500));
  canvas->drawImage(scaledImage);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/scaled_clip"));
  auto imagePath = "resources/apitest/rotation.jpg";
  image = MakeImage(imagePath);
  auto newWidth = image->width() / 8;
  auto newHeight = image->height() / 8;
  scaledImage = image->makeScaled(newWidth, newHeight);
  canvas->clear();
  canvas->drawImage(scaledImage);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/scaled_imageCodec_box_filter"));
  auto codec = MakeImageCodec(imagePath);
  ASSERT_TRUE(codec != nullptr);
  Bitmap bitmap(codec->width(), codec->height(), false, true, codec->colorSpace());
  ASSERT_FALSE(bitmap.isEmpty());
  Pixmap pixmap(bitmap);
  auto result = codec->readPixels(pixmap.info(), pixmap.writablePixels());
  pixmap.reset();
  EXPECT_TRUE(result);
  image = Image::MakeFrom(bitmap);
  newWidth = image->width() / 8;
  newHeight = image->height() / 8;
  scaledImage = image->makeScaled(newWidth, newHeight);
  canvas->clear();
  canvas->drawImage(scaledImage);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/scaled_imageBuffer_box_filter"));
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

TGFX_TEST(CanvasTest, RasterizedMipmapImage) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  auto originKey = std::static_pointer_cast<RasterizedImage>(image)->getTextureKey();
  auto textureProxy = context->proxyProvider()->findOrWrapTextureProxy(originKey);
  EXPECT_TRUE(textureProxy == nullptr);
  auto surface = Surface::Make(context, 300, 300);
  auto canvas = surface->getCanvas();
  canvas->drawImage(image);
  context->flushAndSubmit();
  textureProxy = context->proxyProvider()->findOrWrapTextureProxy(originKey);
  EXPECT_TRUE(textureProxy != nullptr);

  image = image->makeMipmapped(true);
  EXPECT_TRUE(image->hasMipmaps());
  auto mipmapKey = std::static_pointer_cast<RasterizedImage>(image)->getTextureKey();
  EXPECT_TRUE(mipmapKey != originKey);
  auto mipmapTexture = context->proxyProvider()->findOrWrapTextureProxy(mipmapKey);
  EXPECT_TRUE(mipmapTexture == nullptr);
  canvas->drawImage(image);
  context->flushAndSubmit();
  mipmapTexture = context->proxyProvider()->findOrWrapTextureProxy(mipmapKey);
  EXPECT_TRUE(mipmapTexture != nullptr);

  image = image->makeMipmapped(false);
  EXPECT_FALSE(image->hasMipmaps());
  EXPECT_TRUE(originKey == std::static_pointer_cast<RasterizedImage>(image)->getTextureKey());

  textureProxy = context->proxyProvider()->findOrWrapTextureProxy(originKey);
  EXPECT_TRUE(textureProxy != nullptr);
  image = image->makeMipmapped(true);
  EXPECT_TRUE(image->hasMipmaps());
  EXPECT_TRUE(mipmapKey == std::static_pointer_cast<RasterizedImage>(image)->getTextureKey());
  mipmapTexture = context->proxyProvider()->findOrWrapTextureProxy(mipmapKey);
  EXPECT_TRUE(mipmapTexture != nullptr);
}

TGFX_TEST(CanvasTest, RoundRectRadii) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/roundRectRadii"));

  radii[1] = {60, 20};
  Path path2 = {};
  path2.addRoundRect(rect, radii);
  paint.setColor(Color::Red());
  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(10.f);
  canvas->clear();
  canvas->drawPath(path2, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/roundRectRadiiStroke"));
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

TGFX_TEST(CanvasTest, drawScaleImage) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto imagePath = "resources/apitest/rotation.jpg";
  auto codec = MakeImageCodec(imagePath);
  ASSERT_TRUE(codec != nullptr);
  auto image = Image::MakeFrom(codec);
  ASSERT_TRUE(image != nullptr);
  PictureRecorder recorder = {};
  auto canvas = recorder.beginRecording();
  auto paint = Paint();
  paint.setColor(Color::Red());
  auto rect1 = Rect::MakeWH(1000, 1000);
  auto rect2 = Rect::MakeXYWH(1000, 2000, 1000, 1000);
  canvas->drawImage(image);
  canvas->drawRect(rect1, paint);
  canvas->drawRect(rect2, paint);
  auto singleImageRecord = recorder.finishRecordingAsPicture();
  auto pictureImage = Image::MakeFrom(singleImageRecord, image->width(), image->height());
  pictureImage = pictureImage->makeRasterized();
  auto scale = 0.5f;
  auto width = static_cast<int>(image->width() * scale);
  auto height = static_cast<int>(image->height() * scale);
  auto matrix = Matrix::MakeScale(scale);
  auto surface = Surface::Make(context, width, height);
  canvas = surface->getCanvas();
  canvas->setMatrix(matrix);
  canvas->drawImage(pictureImage);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/drawScalePictureImage"));
  auto scaleImage = image->makeScaled(width, height);
  canvas->clear();
  canvas->setMatrix(matrix);
  canvas->drawImage(scaleImage);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/drawScaleCodecImage"));
  auto rect = Rect::MakeXYWH(500, 1000, 2000, 1000);
  auto subImage = image->makeSubset(rect)->makeRasterized();
  canvas->clear();
  canvas->setMatrix(matrix);
  canvas->drawImage(subImage);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/drawScaleSubImage"));
  Bitmap bitmap(codec->width(), codec->height(), false, true, codec->colorSpace());
  ASSERT_FALSE(bitmap.isEmpty());
  Pixmap pixmap(bitmap);
  auto result = codec->readPixels(pixmap.info(), pixmap.writablePixels());
  pixmap.reset();
  EXPECT_TRUE(result);
  auto bufferImage = Image::MakeFrom(bitmap);
  width = static_cast<int>(bufferImage->width() * scale);
  height = static_cast<int>(bufferImage->height() * scale);
  scaleImage = bufferImage->makeScaled(width, height);
  canvas->clear();
  canvas->setMatrix(matrix);
  canvas->drawImage(scaleImage);
  EXPECT_TRUE(Baseline::Compare(surface, "CanvasTest/drawScaleBufferImage"));
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
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  ASSERT_TRUE(surface != nullptr);
  auto* canvas = surface->getCanvas();

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
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 100, 100);
  ASSERT_TRUE(surface != nullptr);
  auto* canvas = surface->getCanvas();
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
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 300);
  ASSERT_TRUE(surface != nullptr);
  auto* canvas = surface->getCanvas();

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
  auto transform3DFilter = ImageFilter::Transform3D(transform);
  paint1.setImageFilter(transform3DFilter);
  canvas->drawShape(rawShape, paint1);

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
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 3024, 4032);
  ASSERT_TRUE(surface != nullptr);
  auto* canvas = surface->getCanvas();
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

}  // namespace tgfx

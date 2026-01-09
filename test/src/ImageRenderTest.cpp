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

#include "core/images/CodecImage.h"
#include "core/images/RasterizedImage.h"
#include "core/images/SubsetImage.h"
#include "core/images/TransformImage.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/ProxyProvider.h"
#include "gpu/opengl/GLFunctions.h"
#include "gpu/opengl/GLGPU.h"
#include "gpu/resources/TextureView.h"
#include "gtest/gtest.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(ImageRenderTest, TileMode) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/tile_mode_normal"));
  canvas->clear();
  image = image->makeSubset(Rect::MakeXYWH(300, 1000, 2400, 2000));
  shader = Shader::MakeImageShader(image, TileMode::Mirror, TileMode::Repeat)
               ->makeWithMatrix(Matrix::MakeScale(0.125f));
  paint.setShader(shader);
  canvas->drawRect(drawRect, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/tile_mode_subset"));
  canvas->clear();
  image = MakeImage("resources/apitest/rgbaaa.png");
  ASSERT_TRUE(image != nullptr);
  image = image->makeRGBAAA(512, 512, 512, 0);
  ASSERT_TRUE(image != nullptr);
  shader = Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Mirror);
  paint.setShader(shader);
  canvas->drawRect(drawRect, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/tile_mode_rgbaaa"));
}

TGFX_TEST(ImageRenderTest, filterMode) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/filter_mode_nearest"));
  canvas->clear();
  canvas->drawImage(image, SamplingOptions(FilterMode::Linear));
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/filter_mode_linear"));
}

TGFX_TEST(ImageRenderTest, rasterizedImage) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/rasterized"));
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
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/rasterized_mipmap"));
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
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/rasterized_scale_up"));
  context->setResourceExpirationFrames(defaultExpirationFrames);
}

TGFX_TEST(ImageRenderTest, mipmap) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/mipmap_none"));
  canvas->clear();
  canvas->drawImage(imageMipmapped, SamplingOptions(FilterMode::Linear, MipmapMode::Nearest));
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/mipmap_nearest"));
  canvas->clear();
  canvas->drawImage(imageMipmapped, SamplingOptions(FilterMode::Linear, MipmapMode::Linear));
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/mipmap_linear"));
  surface = Surface::Make(context, static_cast<int>(imageWidth * 4.f),
                          static_cast<int>(imageHeight * 4.f));
  canvas = surface->getCanvas();
  Paint paint;
  paint.setShader(Shader::MakeImageShader(imageMipmapped, TileMode::Mirror, TileMode::Repeat,
                                          SamplingOptions(FilterMode::Linear, MipmapMode::Linear))
                      ->makeWithMatrix(imageMatrix));
  canvas->drawRect(Rect::MakeWH(surface->width(), surface->height()), paint);
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/mipmap_linear_texture_effect"));
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

TGFX_TEST(ImageRenderTest, TileModeFallback) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/TileModeFallback"));
  gl->deleteTextures(1, &glInfo.id);
}

TGFX_TEST(ImageRenderTest, hardwareMipmap) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/mipmap_linear_hardware"));
}

TGFX_TEST(ImageRenderTest, image) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/drawImage"));
}

TGFX_TEST(ImageRenderTest, drawImageRect) {
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

  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/drawImageRect"));
}

TGFX_TEST(ImageRenderTest, atlas) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/atlas"));
}

TGFX_TEST(ImageRenderTest, rectangleTextureAsBlendDst) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/hardware_render_target_blend"));
  auto gl = static_cast<GLGPU*>(context->gpu())->functions();
  gl->deleteTextures(1, &(glInfo.id));
}

TGFX_TEST(ImageRenderTest, YUVImage) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/YUVImage"));
  canvas->clear();
  auto rgbaa = image->makeRGBAAA(width / 2, static_cast<int>(height), width / 2, 0);
  ASSERT_TRUE(rgbaa != nullptr);
  canvas->setMatrix(Matrix::MakeTrans(static_cast<float>(width / 4), 0.f));
  canvas->drawImage(rgbaa);
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/YUVImage_RGBAA"));
}

TGFX_TEST(ImageRenderTest, RotateImageRect) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/RotateImageRect"));
}

TGFX_TEST(ImageRenderTest, ScaleImage) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/scaled_image"));
  canvas->clear();
  image = image->makeMipmapped(true);
  EXPECT_TRUE(image->hasMipmaps());
  SamplingOptions sampling(FilterMode::Linear, MipmapMode::Linear);
  scaledImage = ScaleImage(image, 0.15f, sampling);
  EXPECT_TRUE(scaledImage->hasMipmaps());
  canvas->drawImage(scaledImage, 100, 100);
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/scaled_mipmap"));
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
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/scaled_scale_up"));
  canvas->clear();
  canvas->clipRect(Rect::MakeXYWH(100, 100, 500, 500));
  canvas->drawImage(scaledImage);
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/scaled_clip"));
  auto imagePath = "resources/apitest/rotation.jpg";
  image = MakeImage(imagePath);
  auto newWidth = image->width() / 8;
  auto newHeight = image->height() / 8;
  scaledImage = image->makeScaled(newWidth, newHeight);
  canvas->clear();
  canvas->drawImage(scaledImage);
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/scaled_imageCodec_box_filter"));
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
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/scaled_imageBuffer_box_filter"));
}

TGFX_TEST(ImageRenderTest, RasterizedMipmapImage) {
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

TGFX_TEST(ImageRenderTest, drawScaleImage) {
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
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/drawScalePictureImage"));
  auto scaleImage = image->makeScaled(width, height);
  canvas->clear();
  canvas->setMatrix(matrix);
  canvas->drawImage(scaleImage);
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/drawScaleCodecImage"));
  auto rect = Rect::MakeXYWH(500, 1000, 2000, 1000);
  auto subImage = image->makeSubset(rect)->makeRasterized();
  canvas->clear();
  canvas->setMatrix(matrix);
  canvas->drawImage(subImage);
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/drawScaleSubImage"));
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
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/drawScaleBufferImage"));
}

}  // namespace tgfx

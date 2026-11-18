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
#include "gpu/ResourceCache.h"
#include "gpu/TPArgs.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/TextureView.h"
#include "gtest/gtest.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageBuffer.h"
#include "tgfx/core/ImageProvider.h"
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
  TGFX_PRIVATE_ACCESS(auto rasterImageUniqueKey =
                          std::static_pointer_cast<RasterizedImage>(rasterImage)->getTextureKey();
                      auto textureView = Resource::Find<TextureView>(context, rasterImageUniqueKey);
                      ASSERT_TRUE(textureView != nullptr); EXPECT_TRUE(textureView != nullptr);
                      EXPECT_EQ(textureView->width(), 454); EXPECT_EQ(textureView->height(), 605);
                      auto source = std::static_pointer_cast<TransformImage>(image)->source;
                      auto imageUniqueKey =
                          std::static_pointer_cast<RasterizedImage>(source)->getTextureKey();
                      textureView = Resource::Find<TextureView>(context, imageUniqueKey);
                      EXPECT_TRUE(textureView == nullptr));
  canvas->clear();
  image = image->makeMipmapped(true);
  EXPECT_TRUE(image->hasMipmaps());
  SamplingOptions sampling(FilterMode::Linear, MipmapMode::Linear);
  auto scaledImage = ScaleImage(image, 0.15f, sampling);
  rasterImage = scaledImage->makeRasterized();
  EXPECT_TRUE(rasterImage->hasMipmaps());
  canvas->drawImage(rasterImage, 100, 100);
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/rasterized_mipmap"));
  TGFX_PRIVATE_ACCESS(textureView = Resource::Find<TextureView>(context, rasterImageUniqueKey);
                      EXPECT_TRUE(textureView == nullptr);
                      rasterImageUniqueKey =
                          std::static_pointer_cast<RasterizedImage>(rasterImage)->getTextureKey();
                      textureView = Resource::Find<TextureView>(context, rasterImageUniqueKey);
                      EXPECT_TRUE(textureView != nullptr));
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

TGFX_TEST(ImageRenderTest, YUVImageWithRowBytesTooSmall) {
  constexpr int width = 64;
  constexpr int height = 64;
  std::vector<uint8_t> yData(static_cast<size_t>(width) * height);
  std::vector<uint8_t> uvData(static_cast<size_t>(width) * height / 4);
  const void* addresses[3] = {yData.data(), uvData.data(), uvData.data()};
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  // A row of one byte cannot hold a line of any plane, which is what a decoder returns when it
  // produces a frame that is smaller than the size its caller declared. Such data must not reach the
  // upload, which would read past the buffers the caller described.
  const size_t narrowRowBytes[3] = {1, 1, 1};
  auto narrowI420 =
      YUVData::MakeFrom(width, height, addresses, narrowRowBytes, YUVData::I420_PLANE_COUNT);
  ASSERT_TRUE(narrowI420 != nullptr);
  EXPECT_EQ(TextureView::MakeI420(context, narrowI420.get(), YUVColorSpace::BT601_LIMITED),
            nullptr);
  auto narrowNV12 =
      YUVData::MakeFrom(width, height, addresses, narrowRowBytes, YUVData::NV12_PLANE_COUNT);
  ASSERT_TRUE(narrowNV12 != nullptr);
  EXPECT_EQ(TextureView::MakeNV12(context, narrowNV12.get(), YUVColorSpace::BT601_LIMITED),
            nullptr);

  // The same buffers with rows that can hold a full line are accepted.
  const size_t rowBytes[3] = {width, width / 2, width / 2};
  auto validData = YUVData::MakeFrom(width, height, addresses, rowBytes, YUVData::I420_PLANE_COUNT);
  ASSERT_TRUE(validData != nullptr);
  EXPECT_NE(TextureView::MakeI420(context, validData.get(), YUVColorSpace::BT601_LIMITED), nullptr);
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
  TGFX_PRIVATE_ACCESS(
      auto originKey = std::static_pointer_cast<RasterizedImage>(image)->getTextureKey();
      auto textureProxy = context->proxyProvider()->findOrWrapTextureProxy(originKey);
      EXPECT_TRUE(textureProxy == nullptr));
  auto surface = Surface::Make(context, 300, 300);
  auto canvas = surface->getCanvas();
  canvas->drawImage(image);
  context->flushAndSubmit();
  TGFX_PRIVATE_ACCESS(textureProxy = context->proxyProvider()->findOrWrapTextureProxy(originKey);
                      EXPECT_TRUE(textureProxy != nullptr));

  image = image->makeMipmapped(true);
  EXPECT_TRUE(image->hasMipmaps());
  TGFX_PRIVATE_ACCESS(
      auto mipmapKey = std::static_pointer_cast<RasterizedImage>(image)->getTextureKey();
      EXPECT_TRUE(mipmapKey != originKey);
      auto mipmapTexture = context->proxyProvider()->findOrWrapTextureProxy(mipmapKey);
      EXPECT_TRUE(mipmapTexture == nullptr));
  canvas->drawImage(image);
  context->flushAndSubmit();
  TGFX_PRIVATE_ACCESS(mipmapTexture = context->proxyProvider()->findOrWrapTextureProxy(mipmapKey);
                      EXPECT_TRUE(mipmapTexture != nullptr));

  image = image->makeMipmapped(false);
  EXPECT_FALSE(image->hasMipmaps());
  TGFX_PRIVATE_ACCESS(
      EXPECT_TRUE(originKey == std::static_pointer_cast<RasterizedImage>(image)->getTextureKey());
      textureProxy = context->proxyProvider()->findOrWrapTextureProxy(originKey);
      EXPECT_TRUE(textureProxy != nullptr));
  image = image->makeMipmapped(true);
  EXPECT_TRUE(image->hasMipmaps());
  TGFX_PRIVATE_ACCESS(
      EXPECT_TRUE(mipmapKey == std::static_pointer_cast<RasterizedImage>(image)->getTextureKey());
      mipmapTexture = context->proxyProvider()->findOrWrapTextureProxy(mipmapKey);
      EXPECT_TRUE(mipmapTexture != nullptr));
}

TGFX_TEST(ImageRenderTest, RasterizedScaledProxySize) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  // GenMesh.png is 1563x1563, an odd size so that width * 0.5 is not an integer. The decoder rounds
  // the downscaled cache texture to round(1563 * 0.5) = 782. getRasterizedSize() must report that
  // same 782, and the wrapped proxy served from the resource cache on a later draw must keep the
  // real texture size instead of a recomputed one, otherwise a cached draw would sample a different
  // number of texels than the first draw.
  auto image = MakeImage("resources/assets/GenMesh.png");
  ASSERT_TRUE(image != nullptr);
  image = image->makeRasterized();
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, 800, 800);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->setMatrix(Matrix::MakeScale(0.4f));
  canvas->drawImage(image);
  context->flushAndSubmit();
  TGFX_PRIVATE_ACCESS(auto raster = std::static_pointer_cast<RasterizedImage>(image);
                      auto scaledKey = raster->getTextureKey(0.5f);
                      EXPECT_TRUE(context->resourceCache()->hasUniqueResource(scaledKey));
                      auto contentSize = raster->source->getRasterizedSize(0.5f);
                      EXPECT_EQ(contentSize.width, 782); EXPECT_EQ(contentSize.height, 782);
                      TPArgs tpArgs(context, 0, false, 0.5f, BackingFit::Exact);
                      auto proxy = raster->lockTextureProxy(tpArgs); ASSERT_TRUE(proxy != nullptr);
                      EXPECT_EQ(proxy->width(), 782); EXPECT_EQ(proxy->height(), 782));
}

namespace {
class VariantOnlyProvider : public ImageProvider {
 public:
  VariantOnlyProvider(int width, int height, std::shared_ptr<Image> variant)
      : _width(width), _height(height), variant(std::move(variant)) {
  }

  int width() const override {
    return _width;
  }

  int height() const override {
    return _height;
  }

  std::shared_ptr<Image> getImage(float) const override {
    return variant;
  }

 private:
  int _width = 0;
  int _height = 0;
  std::shared_ptr<Image> variant = nullptr;
};
}  // namespace

TGFX_TEST(ImageRenderTest, RasterizedProviderVariant) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  // A provider reports a 10x10 logical image but only serves a lower-density 6x6 variant with four
  // distinct quadrants. RasterizedImage has no content size of its own for this source, so it must
  // fall back to the variant texture size (6) instead of the logical size (10); otherwise fpMatrix
  // becomes 1 and only the top-left quadrant is sampled and stretched over the whole image.
  constexpr int vw = 6;
  constexpr int vh = 6;
  constexpr uint32_t red = 0xFF0000FFu;
  constexpr uint32_t green = 0xFF00FF00u;
  constexpr uint32_t blue = 0xFFFF0000u;
  constexpr uint32_t yellow = 0xFF00FFFFu;
  std::vector<uint32_t> pixels(static_cast<size_t>(vw) * static_cast<size_t>(vh));
  for (int y = 0; y < vh; y++) {
    for (int x = 0; x < vw; x++) {
      auto right = x >= vw / 2;
      auto bottom = y >= vh / 2;
      pixels[static_cast<size_t>(y) * static_cast<size_t>(vw) + static_cast<size_t>(x)] =
          bottom ? (right ? yellow : blue) : (right ? green : red);
    }
  }
  auto variantInfo = ImageInfo::Make(vw, vh, ColorType::RGBA_8888, AlphaType::Premultiplied);
  auto variantData = Data::MakeWithCopy(pixels.data(), pixels.size() * sizeof(uint32_t));
  auto variant = Image::MakeFrom(variantInfo, std::move(variantData));
  ASSERT_TRUE(variant != nullptr);
  auto provider = std::make_shared<VariantOnlyProvider>(10, 10, std::move(variant));
  auto image = Image::MakeFrom(provider);
  ASSERT_TRUE(image != nullptr);
  auto rasterized = image->makeRasterized();
  ASSERT_TRUE(rasterized != nullptr);
  auto surface = Surface::Make(context, 10, 10);
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->drawImage(rasterized);
  context->flushAndSubmit();
  auto info = ImageInfo::Make(10, 10, ColorType::RGBA_8888, AlphaType::Premultiplied);
  Buffer buffer(info.byteSize());
  ASSERT_TRUE(surface->readPixels(info, buffer.data()));
  auto result = reinterpret_cast<const uint32_t*>(buffer.data());
  // Each quadrant center maps inside a single variant texel region, so the sampled color is exact.
  EXPECT_EQ(result[2 * 10 + 2], red);
  EXPECT_EQ(result[2 * 10 + 7], green);
  EXPECT_EQ(result[7 * 10 + 2], blue);
  EXPECT_EQ(result[7 * 10 + 7], yellow);
}

namespace {
// An ImageBuffer whose GPU texture is taller than its logical content: the extra rows are red
// padding below the green content, mimicking a hardware-decoded buffer aligned by the decoder.
class PaddedImageBuffer : public ImageBuffer {
 public:
  PaddedImageBuffer(int width, int height) : _width(width), _height(height) {
  }

  int width() const override {
    return _width;
  }

  int height() const override {
    return _height;
  }

  bool isAlphaOnly() const override {
    return false;
  }

  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return imageColorSpace;
  }

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) const override {
    auto bufferHeight = _height + PADDING;
    std::vector<uint32_t> pixels(static_cast<size_t>(_width) * static_cast<size_t>(bufferHeight));
    for (int y = 0; y < bufferHeight; y++) {
      auto color = y < _height ? 0xFF00FF00u : 0xFFFF0000u;
      for (int x = 0; x < _width; x++) {
        pixels[static_cast<size_t>(y) * static_cast<size_t>(_width) + static_cast<size_t>(x)] =
            color;
      }
    }
    return TextureView::MakeRGBA(context, _width, bufferHeight, pixels.data(),
                                 static_cast<size_t>(_width) * sizeof(uint32_t), mipmapped);
  }

 private:
  static constexpr int PADDING = 8;
  int _width = 0;
  int _height = 0;
  std::shared_ptr<ColorSpace> imageColorSpace = ColorSpace::SRGB();
};
}  // namespace

TGFX_TEST(ImageRenderTest, RasterizedPaddedBuffer) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  // The buffer reports 16x16 content but its texture is 16x24, the extra 8 rows being red padding.
  // RasterizedImage must keep the content size (16) after the proxy is released and rewrapped from
  // the cache at the full 24-row texture size, otherwise the draw stretches the content and samples
  // the red padding.
  constexpr int w = 16;
  constexpr int h = 16;
  auto buffer = std::make_shared<PaddedImageBuffer>(w, h);
  auto image = Image::MakeFrom(buffer);
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, w, h);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->drawImage(image);
  context->flushAndSubmit();
  canvas->clear();
  canvas->drawImage(image);
  context->flushAndSubmit();
  auto info = ImageInfo::Make(w, h, ColorType::RGBA_8888, AlphaType::Premultiplied);
  Buffer readback(info.byteSize());
  ASSERT_TRUE(surface->readPixels(info, readback.data()));
  auto pixels = reinterpret_cast<const uint32_t*>(readback.data());
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      ASSERT_EQ(pixels[y * w + x], 0xFF00FF00u) << "mismatch at " << x << "," << y;
    }
  }
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
  auto width = static_cast<int>(static_cast<float>(image->width()) * scale);
  auto height = static_cast<int>(static_cast<float>(image->height()) * scale);
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
  width = static_cast<int>(static_cast<float>(bufferImage->width()) * scale);
  height = static_cast<int>(static_cast<float>(bufferImage->height()) * scale);
  scaleImage = bufferImage->makeScaled(width, height);
  canvas->clear();
  canvas->setMatrix(matrix);
  canvas->drawImage(scaleImage);
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/drawScaleBufferImage"));
}

/**
 * Draws the visible part of a subset image with a src rect that starts inside the subset bounds
 * and overflows them, at half scale. The crop is rendered into a texture at the draw scale and
 * sampled through the crop origin, so this covers the crop uv mapping end to end.
 */
TGFX_TEST(ImageRenderTest, SubsetImageCropDownscale) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto source = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_TRUE(source != nullptr);
  auto subset = source->makeSubset(Rect::MakeXYWH(4, 4, 120, 120));
  ASSERT_TRUE(subset != nullptr);
  auto surface = Surface::Make(context, 96, 96);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());
  auto srcRect = Rect::MakeXYWH(12, 8, 116, 116);
  canvas->drawImageRect(subset, srcRect, Rect::MakeXYWH(10, 10, 54, 56));
  EXPECT_TRUE(Baseline::Compare(surface, "ImageRenderTest/subset_crop_downscale"));
}

}  // namespace tgfx

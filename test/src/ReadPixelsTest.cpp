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

#include <vector>
#include "gpu/opengl/GLUtil.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/opengl/GLDevice.h"
#include "utils/TestUtils.h"

namespace tgfx {

#define CHECK_PIXELS(info, pixels, key)                                       \
  {                                                                           \
    Pixmap bm(info, pixels);                                                  \
    EXPECT_TRUE(Baseline::Compare(bm, "ReadPixelsTest/" + std::string(key))); \
  }

TGFX_TEST(ReadPixelsTest, PixelMap) {
  auto codec = MakeImageCodec("resources/apitest/test_timestretch.png");
  auto colorSpace = codec->colorSpace();
  EXPECT_TRUE(codec != nullptr);
  auto width = codec->width();
  auto height = codec->height();
  auto RGBAInfo = ImageInfo::Make(width, height, ColorType::RGBA_8888, AlphaType::Unpremultiplied,
                                  0, colorSpace);
  auto byteSize = RGBAInfo.byteSize();
  Buffer pixelsA(byteSize);
  Buffer pixelsB(byteSize * 2);
  auto result = codec->readPixels(RGBAInfo, pixelsA.data());
  EXPECT_TRUE(result);

  Pixmap RGBAMap(RGBAInfo, pixelsA.data());
  CHECK_PIXELS(RGBAInfo, pixelsA.data(), "PixelMap_RGBA_Original");

  result = RGBAMap.readPixels(RGBAInfo, pixelsB.data());
  EXPECT_TRUE(result);
  CHECK_PIXELS(RGBAInfo, pixelsB.data(), "PixelMap_RGBA_to_RGBA");

  pixelsB.clear();
  auto RGB565Info = RGBAInfo.makeColorType(ColorType::RGB_565);
  result = RGBAMap.readPixels(RGB565Info, pixelsB.data());
  EXPECT_TRUE(result);
  CHECK_PIXELS(RGB565Info, pixelsB.data(), "PixelMap_RGBA_to_RGB565");

  pixelsB.clear();
  auto Gray8Info = RGBAInfo.makeColorType(ColorType::Gray_8);
  result = RGBAMap.readPixels(Gray8Info, pixelsB.data());
  EXPECT_TRUE(result);
  CHECK_PIXELS(Gray8Info, pixelsB.data(), "PixelMap_RGBA_to_Gray8");

  pixelsB.clear();
  auto RGBAF16Info = RGBAInfo.makeColorType(ColorType::RGBA_F16);
  result = RGBAMap.readPixels(RGBAF16Info, pixelsB.data());
  EXPECT_TRUE(result);
  CHECK_PIXELS(RGBAF16Info, pixelsB.data(), "PixelMap_RGBA_to_RGBA_F16");

  pixelsB.clear();
  auto RGBA1010102Info = RGBAInfo.makeColorType(ColorType::RGBA_1010102);
  result = RGBAMap.readPixels(RGBA1010102Info, pixelsB.data());
  EXPECT_TRUE(result);
  CHECK_PIXELS(RGBA1010102Info, pixelsB.data(), "PixelMap_RGBA_to_RGBA_1010102");

  pixelsB.clear();
  result = RGBAMap.readPixels(RGBAInfo, pixelsB.data(), 100, 100);
  EXPECT_TRUE(result);
  CHECK_PIXELS(RGBAInfo, pixelsB.data(), "PixelMap_RGBA_to_RGBA_100_100");

  auto RGBARectInfo =
      ImageInfo::Make(500, 500, ColorType::RGBA_8888, AlphaType::Premultiplied, 0, colorSpace);
  pixelsB.clear();
  result = RGBAMap.readPixels(RGBARectInfo, pixelsB.data(), -100, -100);
  EXPECT_TRUE(result);
  CHECK_PIXELS(RGBARectInfo, pixelsB.data(), "PixelMap_RGBA_to_RGBA_-100_-100");

  pixelsB.clear();
  result = RGBAMap.readPixels(RGBARectInfo, pixelsB.data(), 100, -100);
  EXPECT_TRUE(result);
  CHECK_PIXELS(RGBARectInfo, pixelsB.data(), "PixelMap_RGBA_to_RGBA_100_-100");

  auto rgb_AInfo = RGBAInfo.makeAlphaType(AlphaType::Premultiplied);
  result = RGBAMap.readPixels(rgb_AInfo, pixelsB.data());
  EXPECT_TRUE(result);
  CHECK_PIXELS(rgb_AInfo, pixelsB.data(), "PixelMap_RGBA_to_rgb_A");

  auto bgr_AInfo = rgb_AInfo.makeColorType(ColorType::BGRA_8888);
  result = RGBAMap.readPixels(bgr_AInfo, pixelsB.data());
  EXPECT_TRUE(result);
  CHECK_PIXELS(bgr_AInfo, pixelsB.data(), "PixelMap_RGBA_to_bgr_A");

  auto BGRAInfo = bgr_AInfo.makeAlphaType(AlphaType::Unpremultiplied);
  result = RGBAMap.readPixels(BGRAInfo, pixelsB.data());
  EXPECT_TRUE(result);
  CHECK_PIXELS(BGRAInfo, pixelsB.data(), "PixelMap_RGBA_to_BGRA");

  Pixmap BGRAMap(BGRAInfo, pixelsB.data());

  result = BGRAMap.readPixels(BGRAInfo, pixelsA.data());
  EXPECT_TRUE(result);
  CHECK_PIXELS(BGRAInfo, pixelsA.data(), "PixelMap_BGRA_to_BGRA");

  result = BGRAMap.readPixels(RGBAInfo, pixelsA.data());
  EXPECT_TRUE(result);
  CHECK_PIXELS(RGBAInfo, pixelsA.data(), "PixelMap_BGRA_to_RGBA");

  result = BGRAMap.readPixels(rgb_AInfo, pixelsA.data());
  EXPECT_TRUE(result);
  CHECK_PIXELS(rgb_AInfo, pixelsA.data(), "PixelMap_BGRA_to_rgb_A");

  Pixmap rgb_AMap(rgb_AInfo, pixelsA.data());

  result = rgb_AMap.readPixels(RGBAInfo, pixelsB.data());
  EXPECT_TRUE(result);
  CHECK_PIXELS(RGBAInfo, pixelsB.data(), "PixelMap_rgb_A_to_RGBA");

  result = rgb_AMap.readPixels(BGRAInfo, pixelsB.data());
  EXPECT_TRUE(result);
  CHECK_PIXELS(BGRAInfo, pixelsB.data(), "PixelMap_rgb_A_to_BGRA");

  result = rgb_AMap.readPixels(bgr_AInfo, pixelsB.data());
  EXPECT_TRUE(result);
  CHECK_PIXELS(bgr_AInfo, pixelsB.data(), "PixelMap_rgb_A_to_bgr_A");

  auto A8Info =
      ImageInfo::Make(width, height, ColorType::ALPHA_8, AlphaType::Unpremultiplied, 0, colorSpace);
  EXPECT_EQ(A8Info.alphaType(), AlphaType::Premultiplied);
  auto alphaByteSize = A8Info.byteSize();
  Buffer pixelsC(alphaByteSize);

  result = rgb_AMap.readPixels(A8Info, pixelsC.data());
  EXPECT_TRUE(result);
  CHECK_PIXELS(A8Info, pixelsC.data(), "PixelMap_rgb_A_to_alpha");

  Pixmap A8Map(A8Info, pixelsC.data());

  result = A8Map.readPixels(rgb_AInfo, pixelsB.data());
  EXPECT_TRUE(result);
  CHECK_PIXELS(rgb_AInfo, pixelsB.data(), "PixelMap_alpha_to_rgb_A");

  result = A8Map.readPixels(BGRAInfo, pixelsB.data());
  EXPECT_TRUE(result);
  CHECK_PIXELS(BGRAInfo, pixelsB.data(), "PixelMap_alpha_to_BGRA");
}

TGFX_TEST(ReadPixelsTest, Surface) {
  auto codec = MakeImageCodec("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(codec != nullptr);
  Bitmap bitmap(codec->width(), codec->height(), false, false, codec->colorSpace());
  ASSERT_FALSE(bitmap.isEmpty());
  auto pixels = bitmap.lockPixels();
  auto result = codec->readPixels(bitmap.info(), pixels);
  bitmap.unlockPixels();
  EXPECT_TRUE(result);

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = Image::MakeFrom(bitmap);
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, image->width(), image->height());
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->drawImage(image);

  auto width = bitmap.width();
  auto height = bitmap.height();
  pixels = bitmap.lockPixels();

  auto colorSpace = surface->colorSpace();
  auto RGBAInfo =
      ImageInfo::Make(width, height, ColorType::RGBA_8888, AlphaType::Premultiplied, 0, colorSpace);
  result = surface->readPixels(RGBAInfo, pixels);
  EXPECT_TRUE(result);
  CHECK_PIXELS(RGBAInfo, pixels, "Surface_rgb_A_to_rgb_A");

  auto BGRAInfo =
      ImageInfo::Make(width, height, ColorType::BGRA_8888, AlphaType::Premultiplied, 0, colorSpace);
  result = surface->readPixels(BGRAInfo, pixels);
  EXPECT_TRUE(result);
  CHECK_PIXELS(BGRAInfo, pixels, "Surface_rgb_A_to_bgr_A");

  memset(pixels, 0, RGBAInfo.byteSize());
  result = surface->readPixels(RGBAInfo, pixels, 100, 100);
  EXPECT_TRUE(result);
  CHECK_PIXELS(RGBAInfo, pixels, "Surface_rgb_A_to_rgb_A_100_100");

  auto RGBARectInfo =
      ImageInfo::Make(500, 500, ColorType::RGBA_8888, AlphaType::Premultiplied, 0, colorSpace);
  memset(pixels, 0, RGBARectInfo.byteSize());
  result = surface->readPixels(RGBARectInfo, pixels, -100, -100);
  EXPECT_TRUE(result);
  CHECK_PIXELS(RGBARectInfo, pixels, "Surface_rgb_A_to_rgb_A_-100_-100");

  memset(pixels, 0, RGBARectInfo.byteSize());
  result = surface->readPixels(RGBARectInfo, pixels, 100, -100);
  EXPECT_TRUE(result);
  CHECK_PIXELS(RGBARectInfo, pixels, "Surface_rgb_A_to_rgb_A_100_-100");

  surface = Surface::Make(context, width, height, true);
  colorSpace = surface->colorSpace();
  ASSERT_TRUE(surface != nullptr);
  canvas = surface->getCanvas();
  canvas->drawImage(image);

  auto A8Info =
      ImageInfo::Make(width, height, ColorType::ALPHA_8, AlphaType::Premultiplied, 0, colorSpace);
  result = surface->readPixels(A8Info, pixels);
  EXPECT_TRUE(result);
  CHECK_PIXELS(A8Info, pixels, "Surface_alpha_to_alpha");

  result = surface->readPixels(RGBAInfo, pixels);
  EXPECT_TRUE(result);
  CHECK_PIXELS(RGBAInfo, pixels, "Surface_alpha_to_rgba");

  auto AlphaRectInfo =
      ImageInfo::Make(500, 500, ColorType::ALPHA_8, AlphaType::Premultiplied, 0, colorSpace);
  memset(pixels, 0, AlphaRectInfo.byteSize());
  result = surface->readPixels(AlphaRectInfo, pixels, 100, -100);
  EXPECT_TRUE(result);
  CHECK_PIXELS(AlphaRectInfo, pixels, "Surface_alpha_to_alpha_100_-100");

  auto texture = context->gpu()->createTexture({width, height, PixelFormat::RGBA_8888});
  ASSERT_TRUE(texture != nullptr);
  surface = Surface::MakeFrom(context, texture->getBackendTexture(), ImageOrigin::BottomLeft);
  colorSpace = surface->colorSpace();
  ASSERT_TRUE(surface != nullptr);
  canvas = surface->getCanvas();
  canvas->clear();
  canvas->drawImage(image);

  result = surface->readPixels(RGBAInfo, pixels);
  EXPECT_TRUE(result);
  CHECK_PIXELS(RGBAInfo, pixels, "Surface_BL_rgb_A_to_rgb_A");

  memset(pixels, 0, RGBAInfo.byteSize());
  result = surface->readPixels(RGBAInfo, pixels, 100, 100);
  EXPECT_TRUE(result);
  CHECK_PIXELS(RGBAInfo, pixels, "Surface_BL_rgb_A_to_rgb_A_100_100");

  memset(pixels, 0, RGBARectInfo.byteSize());
  result = surface->readPixels(RGBARectInfo, pixels, -100, -100);
  EXPECT_TRUE(result);
  CHECK_PIXELS(RGBARectInfo, pixels, "Surface_BL_rgb_A_to_rgb_A_-100_-100");

  memset(pixels, 0, RGBARectInfo.byteSize());
  result = surface->readPixels(RGBARectInfo, pixels, 100, -100);
  EXPECT_TRUE(result);
  CHECK_PIXELS(RGBARectInfo, pixels, "Surface_BL_rgb_A_to_rgb_A_100_-100");
  bitmap.unlockPixels();
}

TGFX_TEST(ReadPixelsTest, PngCodec) {
  auto rgbaCodec = MakeImageCodec("resources/apitest/imageReplacement.png");
  auto colorSpace = rgbaCodec->colorSpace();
  ASSERT_TRUE(rgbaCodec != nullptr);
  EXPECT_EQ(rgbaCodec->width(), 110);
  EXPECT_EQ(rgbaCodec->height(), 110);
  EXPECT_EQ(rgbaCodec->orientation(), Orientation::TopLeft);
  auto rowBytes = static_cast<size_t>(rgbaCodec->width()) * 4;
  Buffer buffer(rowBytes * static_cast<size_t>(rgbaCodec->height()));
  auto pixels = buffer.data();
  ASSERT_TRUE(pixels);
  auto RGBAInfo = ImageInfo::Make(rgbaCodec->width(), rgbaCodec->height(), ColorType::RGBA_8888,
                                  AlphaType::Premultiplied, 0, colorSpace);
  EXPECT_TRUE(rgbaCodec->readPixels(RGBAInfo, pixels));
  // set each pixel's alpha to 255
  for (size_t i = 3; i < rowBytes * static_cast<size_t>(rgbaCodec->height()); i += 4) {
    static_cast<uint8_t*>(pixels)[i] = 255;
  }
  CHECK_PIXELS(RGBAInfo, pixels, "PngCodec_Decode_RGBA");
  auto bytes = ImageCodec::Encode(Pixmap(RGBAInfo, pixels), EncodedFormat::PNG, 100);
  auto codec = ImageCodec::MakeFrom(bytes);
  ASSERT_TRUE(codec != nullptr);
  EXPECT_EQ(codec->width(), 110);
  EXPECT_EQ(codec->height(), 110);
  EXPECT_EQ(codec->orientation(), Orientation::TopLeft);
  buffer.clear();
  EXPECT_TRUE(codec->readPixels(RGBAInfo, pixels));
  CHECK_PIXELS(RGBAInfo, pixels, "PngCodec_Encode_RGBA");

  auto A8Info = ImageInfo::Make(rgbaCodec->width(), rgbaCodec->height(), ColorType::ALPHA_8,
                                AlphaType::Premultiplied, 0, colorSpace);
  buffer.clear();
  EXPECT_TRUE(rgbaCodec->readPixels(A8Info, pixels));
  CHECK_PIXELS(A8Info, pixels, "PngCodec_Decode_Alpha8");
  bytes = ImageCodec::Encode(Pixmap(A8Info, pixels), EncodedFormat::PNG, 100);
  codec = ImageCodec::MakeFrom(bytes);
  ASSERT_TRUE(codec != nullptr);
  buffer.clear();
  EXPECT_TRUE(codec->readPixels(A8Info, pixels));
  CHECK_PIXELS(A8Info, pixels, "PngCodec_Encode_Alpha8");

  auto Gray8Info = ImageInfo::Make(rgbaCodec->width(), rgbaCodec->height(), ColorType::Gray_8,
                                   AlphaType::Opaque, 0, colorSpace);
  buffer.clear();
  EXPECT_TRUE(rgbaCodec->readPixels(Gray8Info, pixels));
  CHECK_PIXELS(Gray8Info, pixels, "PngCodec_Decode_Gray8");
  bytes = ImageCodec::Encode(Pixmap(Gray8Info, pixels), EncodedFormat::PNG, 100);
  codec = ImageCodec::MakeFrom(bytes);
  ASSERT_TRUE(codec != nullptr);
  buffer.clear();
  EXPECT_TRUE(codec->readPixels(Gray8Info, pixels));
  CHECK_PIXELS(Gray8Info, pixels, "PngCodec_Encode_Gray8");

  auto RGB565Info = ImageInfo::Make(rgbaCodec->width(), rgbaCodec->height(), ColorType::RGB_565,
                                    AlphaType::Opaque, 0, colorSpace);
  buffer.clear();
  EXPECT_TRUE(rgbaCodec->readPixels(RGB565Info, pixels));
  CHECK_PIXELS(RGB565Info, pixels, "PngCodec_Decode_RGB565");
  bytes = ImageCodec::Encode(Pixmap(RGB565Info, pixels), EncodedFormat::PNG, 100);
  codec = ImageCodec::MakeFrom(bytes);
  ASSERT_TRUE(codec != nullptr);
  buffer.clear();
  EXPECT_TRUE(codec->readPixels(RGB565Info, pixels));
  CHECK_PIXELS(RGB565Info, pixels, "PngCodec_Encode_RGB565");
}

TGFX_TEST(ReadPixelsTest, WebpCodec) {
  auto rgbaCodec = MakeImageCodec("resources/apitest/imageReplacement.webp");
  auto colorSpace = rgbaCodec->colorSpace();
  ASSERT_TRUE(rgbaCodec != nullptr);
  EXPECT_EQ(rgbaCodec->width(), 110);
  EXPECT_EQ(rgbaCodec->height(), 110);
  EXPECT_EQ(rgbaCodec->orientation(), Orientation::TopLeft);
  auto RGBAInfo = ImageInfo::Make(rgbaCodec->width(), rgbaCodec->height(), ColorType::RGBA_8888,
                                  AlphaType::Premultiplied, 0, colorSpace);

  Buffer buffer(RGBAInfo.byteSize());
  auto pixels = buffer.data();
  ASSERT_TRUE(pixels);
  EXPECT_TRUE(rgbaCodec->readPixels(RGBAInfo, pixels));
  CHECK_PIXELS(RGBAInfo, pixels, "WebpCodec_Decode_RGBA");
  Pixmap pixmap(RGBAInfo, pixels);
  auto bytes = ImageCodec::Encode(pixmap, EncodedFormat::WEBP, 100);
  auto codec = ImageCodec::MakeFrom(bytes);
  ASSERT_TRUE(codec != nullptr);
  EXPECT_EQ(codec->width(), 110);
  EXPECT_EQ(codec->height(), 110);
  EXPECT_EQ(codec->orientation(), Orientation::TopLeft);
  buffer.clear();
  EXPECT_TRUE(codec->readPixels(RGBAInfo, pixels));
  CHECK_PIXELS(RGBAInfo, pixels, "WebpCodec_Encode_RGBA");

  auto A8Info = ImageInfo::Make(rgbaCodec->width(), rgbaCodec->height(), ColorType::ALPHA_8,
                                AlphaType::Premultiplied, 0, colorSpace);
  buffer.clear();
  EXPECT_TRUE(rgbaCodec->readPixels(A8Info, pixels));
  CHECK_PIXELS(A8Info, pixels, "WebpCodec_Decode_Alpha8");
  bytes = ImageCodec::Encode(Pixmap(A8Info, pixels), EncodedFormat::WEBP, 100);
  codec = ImageCodec::MakeFrom(bytes);
  buffer.clear();
  EXPECT_TRUE(codec->readPixels(A8Info, pixels));
  CHECK_PIXELS(A8Info, pixels, "WebpCodec_Encode_Alpha8");

  auto Gray8Info = ImageInfo::Make(rgbaCodec->width(), rgbaCodec->height(), ColorType::Gray_8,
                                   AlphaType::Opaque, 0, colorSpace);
  buffer.clear();
  EXPECT_TRUE(rgbaCodec->readPixels(Gray8Info, pixels));
  CHECK_PIXELS(Gray8Info, pixels, "WebpCodec_Decode_Gray8");
  bytes = ImageCodec::Encode(Pixmap(Gray8Info, pixels), EncodedFormat::WEBP, 100);
  codec = ImageCodec::MakeFrom(bytes);
  buffer.clear();
  EXPECT_TRUE(codec->readPixels(Gray8Info, pixels));
  CHECK_PIXELS(Gray8Info, pixels, "WebpCodec_Encode_Gray8");

  auto RGB565Info = ImageInfo::Make(rgbaCodec->width(), rgbaCodec->height(), ColorType::RGB_565,
                                    AlphaType::Opaque, 0, colorSpace);
  buffer.clear();
  EXPECT_TRUE(rgbaCodec->readPixels(RGB565Info, pixels));
  CHECK_PIXELS(RGB565Info, pixels, "WebpCodec_Decode_RGB565");
  bytes = ImageCodec::Encode(Pixmap(RGB565Info, pixels), EncodedFormat::WEBP, 100);
  codec = ImageCodec::MakeFrom(bytes);
  buffer.clear();
  EXPECT_TRUE(codec->readPixels(RGB565Info, pixels));
  CHECK_PIXELS(RGB565Info, pixels, "WebpCodec_Encode_RGB565");
}

TGFX_TEST(ReadPixelsTest, JpegCodec) {
  auto rgbaCodec = MakeImageCodec("resources/apitest/imageReplacement.jpg");
  auto colorSpace = rgbaCodec->colorSpace();
  ASSERT_TRUE(rgbaCodec != nullptr);
  EXPECT_EQ(rgbaCodec->width(), 110);
  EXPECT_EQ(rgbaCodec->height(), 110);
  EXPECT_EQ(rgbaCodec->orientation(), Orientation::RightTop);
  auto RGBAInfo = ImageInfo::Make(rgbaCodec->width(), rgbaCodec->height(), ColorType::RGBA_8888,
                                  AlphaType::Premultiplied, 0, colorSpace);
  Buffer buffer(RGBAInfo.byteSize());
  auto pixels = buffer.data();
  ASSERT_TRUE(pixels);
  EXPECT_TRUE(rgbaCodec->readPixels(RGBAInfo, pixels));
  CHECK_PIXELS(RGBAInfo, pixels, "JpegCodec_Decode_RGBA");
  auto bytes = ImageCodec::Encode(Pixmap(RGBAInfo, pixels), EncodedFormat::JPEG, 20);
  auto codec = ImageCodec::MakeFrom(bytes);
  ASSERT_TRUE(codec != nullptr);
  EXPECT_EQ(codec->width(), 110);
  EXPECT_EQ(codec->height(), 110);
  EXPECT_EQ(codec->orientation(), Orientation::TopLeft);
  buffer.clear();
  EXPECT_TRUE(codec->readPixels(RGBAInfo, pixels));
  CHECK_PIXELS(RGBAInfo, pixels, "JpegCodec_Encode_RGBA");

  auto A8Info = ImageInfo::Make(rgbaCodec->width(), rgbaCodec->height(), ColorType::ALPHA_8,
                                AlphaType::Premultiplied, 0, colorSpace);
  buffer.clear();
  EXPECT_TRUE(rgbaCodec->readPixels(A8Info, pixels));
  CHECK_PIXELS(A8Info, pixels, "JpegCodec_Decode_Alpha8");
  bytes = ImageCodec::Encode(Pixmap(A8Info, pixels), EncodedFormat::JPEG, 100);
  codec = ImageCodec::MakeFrom(bytes);
  buffer.clear();
  EXPECT_TRUE(codec->readPixels(A8Info, pixels));
  CHECK_PIXELS(A8Info, pixels, "JpegCodec_Encode_Alpha8");

  auto Gray8Info = ImageInfo::Make(rgbaCodec->width(), rgbaCodec->height(), ColorType::Gray_8,
                                   AlphaType::Opaque, 0, colorSpace);
  buffer.clear();
  EXPECT_TRUE(rgbaCodec->readPixels(Gray8Info, pixels));
  CHECK_PIXELS(Gray8Info, pixels, "JpegCodec_Decode_Gray8");
  bytes = ImageCodec::Encode(Pixmap(Gray8Info, pixels), EncodedFormat::JPEG, 70);
  codec = ImageCodec::MakeFrom(bytes);
  buffer.clear();
  EXPECT_TRUE(codec->readPixels(Gray8Info, pixels));
  CHECK_PIXELS(Gray8Info, pixels, "JpegCodec_Encode_Gray8");

  auto RGB565Info = ImageInfo::Make(rgbaCodec->width(), rgbaCodec->height(), ColorType::RGB_565,
                                    AlphaType::Opaque, 0, colorSpace);
  buffer.clear();
  EXPECT_TRUE(rgbaCodec->readPixels(RGB565Info, pixels));
  CHECK_PIXELS(RGB565Info, pixels, "JpegCodec_Decode_RGB565");
  bytes = ImageCodec::Encode(Pixmap(RGB565Info, pixels), EncodedFormat::JPEG, 80);
  codec = ImageCodec::MakeFrom(bytes);
  buffer.clear();
  EXPECT_TRUE(codec->readPixels(RGB565Info, pixels));
  CHECK_PIXELS(RGB565Info, pixels, "JpegCodec_Encode_RGB565");
}

TGFX_TEST(ReadPixelsTest, NativeCodec) {
  auto rgbaCodec = MakeNativeCodec("resources/apitest/imageReplacement.png");
  auto colorSpace = rgbaCodec->colorSpace();
  ASSERT_TRUE(rgbaCodec != nullptr);
  EXPECT_EQ(rgbaCodec->width(), 110);
  EXPECT_EQ(rgbaCodec->height(), 110);
  EXPECT_EQ(rgbaCodec->orientation(), Orientation::TopLeft);
  auto RGBAInfo = ImageInfo::Make(rgbaCodec->width(), rgbaCodec->height(), ColorType::RGBA_8888,
                                  AlphaType::Premultiplied, 0, colorSpace);
  Buffer buffer(RGBAInfo.byteSize());
  auto pixels = buffer.data();
  ASSERT_TRUE(pixels);
  EXPECT_TRUE(rgbaCodec->readPixels(RGBAInfo, pixels));
  CHECK_PIXELS(RGBAInfo, pixels, "NativeCodec_Decode_RGBA");
  auto bytes = ImageCodec::Encode(Pixmap(RGBAInfo, pixels), EncodedFormat::PNG, 100);
  auto codec = ImageCodec::MakeNativeCodec(bytes);
  ASSERT_TRUE(codec != nullptr);
  ASSERT_EQ(codec->width(), 110);
  ASSERT_EQ(codec->height(), 110);
  ASSERT_EQ(codec->orientation(), Orientation::TopLeft);
  buffer.clear();
  EXPECT_TRUE(codec->readPixels(RGBAInfo, pixels));
  CHECK_PIXELS(RGBAInfo, pixels, "NativeCodec_Encode_RGBA");

  auto A8Info = ImageInfo::Make(rgbaCodec->width(), rgbaCodec->height(), ColorType::ALPHA_8,
                                AlphaType::Premultiplied, 0, colorSpace);
  buffer.clear();
  EXPECT_TRUE(rgbaCodec->readPixels(A8Info, pixels));
  CHECK_PIXELS(A8Info, pixels, "NativeCodec_Decode_Alpha8");
  bytes = ImageCodec::Encode(Pixmap(A8Info, pixels), EncodedFormat::PNG, 100);
  codec = ImageCodec::MakeNativeCodec(bytes);
  ASSERT_TRUE(codec != nullptr);
  buffer.clear();
  EXPECT_TRUE(codec->readPixels(A8Info, pixels));
  CHECK_PIXELS(A8Info, pixels, "NativeCodec_Encode_Alpha8");
}

TGFX_TEST(ReadPixelsTest, ReadScaleCodec) {
  auto codec = MakeImageCodec("resources/apitest/rotation.jpg");
  auto colorSpace = codec->colorSpace();
  EXPECT_TRUE(codec != nullptr);
  auto width = codec->width() / 10;
  auto height = codec->height() / 10;
  auto RGBA_1010102Info = ImageInfo::Make(width, height, ColorType::RGBA_1010102,
                                          AlphaType::Unpremultiplied, 0, colorSpace);
  auto byteSize = RGBA_1010102Info.byteSize();
  Buffer pixelsA(byteSize);
  auto result = codec->readPixels(RGBA_1010102Info, pixelsA.data());
  EXPECT_TRUE(result);
  EXPECT_TRUE(Baseline::Compare(Pixmap(RGBA_1010102Info, pixelsA.data()),
                                "ReadPixelsTest/read_RGBA_1010102_scaled_codec"));
  auto RGBInfo =
      ImageInfo::Make(width, height, ColorType::RGB_565, AlphaType::Unpremultiplied, 0, colorSpace);
  byteSize = RGBInfo.byteSize();
  Buffer pixelsB(byteSize);
  result = codec->readPixels(RGBInfo, pixelsB.data());
  EXPECT_TRUE(result);
  EXPECT_TRUE(Baseline::Compare(Pixmap(RGBInfo, pixelsB.data()),
                                "ReadPixelsTest/read_RGB_565_scaled_codec"));
  auto RGBA_F16Info = ImageInfo::Make(width, height, ColorType::RGBA_F16,
                                      AlphaType::Unpremultiplied, 0, colorSpace);
  byteSize = RGBA_F16Info.byteSize();
  Buffer pixelsC(byteSize);
  result = codec->readPixels(RGBA_F16Info, pixelsC.data());
  EXPECT_TRUE(result);
  EXPECT_TRUE(Baseline::Compare(Pixmap(RGBA_F16Info, pixelsC.data()),
                                "ReadPixelsTest/read_RGBA_F16_scaled_codec"));
}
}  // namespace tgfx
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

#include <array>
#include <vector>
#include "gtest/gtest.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/ImageEncoder.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/WriteStream.h"

namespace tgfx {

namespace {
class RejectingStream final : public WriteStream {
 public:
  bool write(const void*, size_t) override {
    return false;
  }

  size_t bytesWritten() const override {
    return 0;
  }

  void flush() override {
  }
};
}  // namespace

TEST(ImageEncoderTest, StreamingAndWholeImageAgree) {
  for (auto format : {EncodedFormat::PNG, EncodedFormat::JPEG}) {
    auto alpha = format == EncodedFormat::PNG ? AlphaType::Premultiplied : AlphaType::Opaque;
    constexpr int width = 17;
    constexpr int height = 19;
    constexpr size_t rowBytes = 80;
    auto info =
        ImageInfo::Make(width, height, ColorType::BGRA_8888, alpha, rowBytes, ColorSpace::SRGB());
    std::array<uint8_t, rowBytes* height> pixels = {};
    for (int row = 0; row < height; ++row) {
      for (int column = 0; column < width; ++column) {
        auto* pixel = pixels.data() + static_cast<size_t>(row) * rowBytes + column * 4;
        pixel[0] = static_cast<uint8_t>(row);
        pixel[1] = static_cast<uint8_t>(column * 3);
        pixel[2] = static_cast<uint8_t>((row + column) * 2);
        pixel[3] = static_cast<uint8_t>(format == EncodedFormat::PNG ? 96 + row : 255);
      }
    }

    auto stream = MemoryWriteStream::Make();
    auto encoder = ImageEncoder::Make(stream, info, format, 90);
    ASSERT_NE(encoder, nullptr);
    ASSERT_TRUE(encoder->appendRows(Pixmap(info.makeWH(width, 7), pixels.data())));
    ASSERT_TRUE(
        encoder->appendRows(Pixmap(info.makeWH(width, height - 7), pixels.data() + 7 * rowBytes)));
    ASSERT_TRUE(encoder->finish());
    EXPECT_FALSE(encoder->finish());
    EXPECT_FALSE(encoder->appendRows(Pixmap(info, pixels.data())));

    auto streamed = ImageCodec::MakeFrom(stream->readData());
    auto whole = ImageCodec::MakeFrom(ImageCodec::Encode(Pixmap(info, pixels.data()), format, 90));
    ASSERT_NE(streamed, nullptr);
    ASSERT_NE(whole, nullptr);
    EXPECT_EQ(streamed->width(), width);
    EXPECT_EQ(streamed->height(), height);
    EXPECT_NE(streamed->colorSpace(), nullptr);

    auto decodeInfo = ImageInfo::Make(width, height, ColorType::RGBA_8888,
                                      AlphaType::Unpremultiplied, 0, ColorSpace::SRGB());
    std::vector<uint8_t> streamedPixels(decodeInfo.byteSize());
    std::vector<uint8_t> wholePixels(decodeInfo.byteSize());
    ASSERT_TRUE(streamed->readPixels(decodeInfo, streamedPixels.data()));
    ASSERT_TRUE(whole->readPixels(decodeInfo, wholePixels.data()));
    EXPECT_EQ(streamedPixels, wholePixels);
  }
}

TEST(ImageEncoderTest, Accepts32768OnEitherAxis) {
  for (auto format : {EncodedFormat::PNG, EncodedFormat::JPEG}) {
    for (int width : {16, 32768}) {
      int height = width == 16 ? 32768 : 16;
      auto info = ImageInfo::Make(width, height, ColorType::RGBA_8888, AlphaType::Opaque);
      auto bandInfo = ImageInfo::Make(width, 16, ColorType::RGBA_8888, AlphaType::Opaque);
      std::vector<uint8_t> pixels(bandInfo.byteSize(), 255);
      auto stream = MemoryWriteStream::Make();
      auto encoder = ImageEncoder::Make(stream, info, format);
      ASSERT_NE(encoder, nullptr);
      for (int row = 0; row < height; row += bandInfo.height()) {
        ASSERT_TRUE(encoder->appendRows(Pixmap(bandInfo, pixels.data())));
      }
      ASSERT_TRUE(encoder->finish());
      auto codec = ImageCodec::MakeFrom(stream->readData());
      ASSERT_NE(codec, nullptr);
      EXPECT_EQ(codec->width(), width);
      EXPECT_EQ(codec->height(), height);
    }
  }
}

TEST(ImageEncoderTest, RejectsInvalidInput) {
  constexpr int width = 4;
  constexpr int height = 4;
  auto info = ImageInfo::Make(width, height, ColorType::RGBA_8888, AlphaType::Opaque);
  std::array<uint8_t, width* height* 4> pixels = {};

  auto stream = MemoryWriteStream::Make();
  EXPECT_EQ(ImageEncoder::Make(stream, info, EncodedFormat::WEBP), nullptr);
  auto premultiplied =
      ImageInfo::Make(width, height, ColorType::RGBA_8888, AlphaType::Premultiplied);
  EXPECT_EQ(ImageEncoder::Make(stream, premultiplied, EncodedFormat::JPEG), nullptr);

  auto encoder = ImageEncoder::Make(stream, info, EncodedFormat::PNG);
  ASSERT_NE(encoder, nullptr);
  auto narrow = ImageInfo::Make(width - 1, 1, ColorType::RGBA_8888, AlphaType::Opaque);
  EXPECT_FALSE(encoder->appendRows(Pixmap(narrow, pixels.data())));
  EXPECT_FALSE(encoder->finish());

  encoder = ImageEncoder::Make(stream, info, EncodedFormat::PNG);
  ASSERT_NE(encoder, nullptr);
  auto bandInfo = ImageInfo::Make(width, 3, ColorType::RGBA_8888, AlphaType::Opaque);
  ASSERT_TRUE(encoder->appendRows(Pixmap(bandInfo, pixels.data())));
  auto overflowInfo = ImageInfo::Make(width, 2, ColorType::RGBA_8888, AlphaType::Opaque);
  EXPECT_FALSE(encoder->appendRows(Pixmap(overflowInfo, pixels.data())));
  EXPECT_FALSE(encoder->finish());

  encoder = ImageEncoder::Make(stream, info, EncodedFormat::PNG);
  ASSERT_NE(encoder, nullptr);
  auto alphaMismatch = ImageInfo::Make(width, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
  EXPECT_FALSE(encoder->appendRows(Pixmap(alphaMismatch, pixels.data())));
  EXPECT_FALSE(encoder->finish());
}

TEST(ImageEncoderTest, FailsOnIncompleteInputOrWriteError) {
  auto info = ImageInfo::Make(2, 2, ColorType::RGBA_8888, AlphaType::Opaque);
  std::array<uint8_t, 16> pixels = {};
  auto stream = MemoryWriteStream::Make();
  auto encoder = ImageEncoder::Make(stream, info, EncodedFormat::PNG);
  ASSERT_NE(encoder, nullptr);
  EXPECT_FALSE(encoder->finish());
  EXPECT_FALSE(encoder->appendRows(Pixmap(info, pixels.data())));

  auto rejectingStream = std::make_shared<RejectingStream>();
  EXPECT_EQ(ImageEncoder::Make(rejectingStream, info, EncodedFormat::PNG), nullptr);
  encoder = ImageEncoder::Make(rejectingStream, info, EncodedFormat::JPEG);
  ASSERT_NE(encoder, nullptr);
  ASSERT_TRUE(encoder->appendRows(Pixmap(info, pixels.data())));
  EXPECT_FALSE(encoder->finish());
}

TEST(ImageEncoderTest, WholeImageKeepsAlphaAndGrayFormats) {
  constexpr int width = 17;
  constexpr int height = 9;
  std::array<uint8_t, width* height> source = {};
  for (size_t index = 0; index < source.size(); ++index) {
    source[index] = static_cast<uint8_t>(index);
  }

  auto alphaInfo = ImageInfo::Make(width, height, ColorType::ALPHA_8, AlphaType::Premultiplied);
  auto png = ImageCodec::MakeFrom(
      ImageCodec::Encode(Pixmap(alphaInfo, source.data()), EncodedFormat::PNG, 100));
  ASSERT_NE(png, nullptr);
  EXPECT_TRUE(png->isAlphaOnly());
  std::array<uint8_t, width* height> output = {};
  ASSERT_TRUE(png->readPixels(alphaInfo, output.data()));
  EXPECT_EQ(output, source);

  auto grayInfo = ImageInfo::Make(width, height, ColorType::Gray_8, AlphaType::Opaque);
  auto jpeg = ImageCodec::MakeFrom(
      ImageCodec::Encode(Pixmap(grayInfo, source.data()), EncodedFormat::JPEG, 100));
  ASSERT_NE(jpeg, nullptr);
  ASSERT_TRUE(jpeg->readPixels(grayInfo, output.data()));
  for (size_t index = 0; index < source.size(); ++index) {
    EXPECT_NEAR(output[index], source[index], 1);
  }
}

}  // namespace tgfx

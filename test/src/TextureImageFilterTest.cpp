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
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "core/filters/TextureImageFilter.h"
#include "core/utils/TextureGrainNoise.h"
#include "gtest/gtest.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/ImageInfo.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/YUVColorSpace.h"
#include "tgfx/core/YUVData.h"
#include "utils/TestUtils.h"

namespace tgfx {

namespace {

struct TestImage {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels = {};
};

struct DiffMetrics {
  int maxDifference = 0;
  double meanDifference = 0.0;
  double fractionOverFour = 0.0;
};

void SetPremultipliedPixel(TestImage* image, int x, int y, int red, int green, int blue,
                           int alpha) {
  alpha = std::clamp(alpha, 0, 255);
  red = std::clamp(red, 0, 255);
  green = std::clamp(green, 0, 255);
  blue = std::clamp(blue, 0, 255);
  auto offset =
      (static_cast<size_t>(y) * static_cast<size_t>(image->width) + static_cast<size_t>(x)) * 4;
  image->pixels[offset] = static_cast<uint8_t>((red * alpha + 127) / 255);
  image->pixels[offset + 1] = static_cast<uint8_t>((green * alpha + 127) / 255);
  image->pixels[offset + 2] = static_cast<uint8_t>((blue * alpha + 127) / 255);
  image->pixels[offset + 3] = static_cast<uint8_t>(alpha);
}

TestImage MakeOpaqueMotif() {
  TestImage image = {320, 240, {}};
  image.pixels.resize(static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4);
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      auto inRectangle = x >= 0 && x < 118 && y >= 34 && y < 206;
      auto rectangleHoleX = x - 62;
      auto rectangleHoleY = y - 120;
      if (rectangleHoleX * rectangleHoleX + rectangleHoleY * rectangleHoleY < 22 * 22) {
        inRectangle = false;
      }

      auto circleX = x - 205;
      auto circleY = y - 120;
      auto inCircle = circleX * circleX + circleY * circleY <= 68 * 68;
      if (circleX * circleX + circleY * circleY < 22 * 22) {
        inCircle = false;
      }

      if (!inRectangle && !inCircle) {
        continue;
      }
      auto stripe = ((x + y) / 12) % 2 == 0;
      if (stripe) {
        SetPremultipliedPixel(&image, x, y, 243, 113, 89, 255);
      } else {
        SetPremultipliedPixel(&image, x, y, 45, 169, 160, 255);
      }
    }
  }
  return image;
}

TestImage MakeTranslucentMotif() {
  TestImage image = {280, 200, {}};
  image.pixels.resize(static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4);
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      auto normalizedX = (static_cast<float>(x) - 140.0f) / 110.0f;
      auto normalizedY = (static_cast<float>(y) - 100.0f) / 70.0f;
      auto radius = std::sqrt(normalizedX * normalizedX + normalizedY * normalizedY);
      if (radius > 1.0f) {
        continue;
      }
      auto alpha = static_cast<int>(std::lround(48.0f + 176.0f * (1.0f - radius)));
      auto stripe = ((x + y) / 9) % 3;
      std::array<int, 3> color = {255, 255, 255};
      if (stripe == 0) {
        color = {248, 189, 74};
      } else if (stripe == 1) {
        color = {69, 153, 216};
      } else {
        color = {226, 96, 132};
      }
      SetPremultipliedPixel(&image, x, y, color[0], color[1], color[2], alpha);
    }
  }
  return image;
}

TestImage MakeOrangeCircle() {
  TestImage image = {500, 500, {}};
  image.pixels.resize(static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4);
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      auto deltaX = static_cast<float>(x) + 0.5f - 250.0f;
      auto deltaY = static_cast<float>(y) + 0.5f - 250.0f;
      auto distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
      if (distance >= 250.0f) {
        continue;
      }
      auto alpha =
          distance <= 248.5f ? 255 : static_cast<int>(std::lround((250.0f - distance) * 170.0f));
      SetPremultipliedPixel(&image, x, y, 249, 190, 113, alpha);
    }
  }
  return image;
}

TestImage MakeOpaqueRectangle() {
  TestImage image = {240, 160, {}};
  image.pixels.resize(static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4);
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      SetPremultipliedPixel(&image, x, y, 249, 190, 113, 255);
    }
  }
  return image;
}

std::shared_ptr<Image> MakeImage(const TestImage& image) {
  auto info =
      ImageInfo::Make(image.width, image.height, ColorType::RGBA_8888, AlphaType::Premultiplied);
  auto data = Data::MakeWithCopy(image.pixels.data(), image.pixels.size());
  return Image::MakeFrom(info, data);
}

TestImage LoadScaledImage(const std::string& path, int maxHeight) {
  auto codec = MakeImageCodec(path);
  if (codec == nullptr) {
    return {};
  }
  auto scale = std::min(1.0f, static_cast<float>(maxHeight) / static_cast<float>(codec->height()));
  auto width =
      std::max(1, static_cast<int>(std::lround(static_cast<float>(codec->width()) * scale)));
  auto height =
      std::max(1, static_cast<int>(std::lround(static_cast<float>(codec->height()) * scale)));
  TestImage image = {width, height, {}};
  image.pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
  auto info = ImageInfo::Make(width, height, ColorType::RGBA_8888, AlphaType::Premultiplied);
  if (!codec->readPixels(info, image.pixels.data())) {
    return {};
  }
  return image;
}

std::array<uint8_t, 4> PixelAt(const TestImage& image, int x, int y) {
  if (x < 0 || y < 0 || x >= image.width || y >= image.height) {
    return {0, 0, 0, 0};
  }
  auto offset =
      (static_cast<size_t>(y) * static_cast<size_t>(image.width) + static_cast<size_t>(x)) * 4;
  return {image.pixels[offset], image.pixels[offset + 1], image.pixels[offset + 2],
          image.pixels[offset + 3]};
}

std::array<float, 4> SamplePremultiplied(const TestImage& image, float x, float y) {
  auto sourceX = x - 0.5f;
  auto sourceY = y - 0.5f;
  auto x0 = static_cast<int>(std::floor(sourceX));
  auto y0 = static_cast<int>(std::floor(sourceY));
  auto fractionX = sourceX - static_cast<float>(x0);
  auto fractionY = sourceY - static_cast<float>(y0);
  std::array<float, 4> result = {0.0f, 0.0f, 0.0f, 0.0f};
  for (int dy = 0; dy < 2; ++dy) {
    auto weightY = dy == 0 ? 1.0f - fractionY : fractionY;
    for (int dx = 0; dx < 2; ++dx) {
      auto weightX = dx == 0 ? 1.0f - fractionX : fractionX;
      auto pixel = PixelAt(image, x0 + dx, y0 + dy);
      auto weight = weightX * weightY;
      for (size_t channel = 0; channel < 4; ++channel) {
        result[channel] += static_cast<float>(pixel[channel]) * weight;
      }
    }
  }
  return result;
}

std::array<uint8_t, 4> QuantizePremultiplied(const std::array<float, 4>& pixel) {
  return {static_cast<uint8_t>(std::lround(std::clamp(pixel[0], 0.0f, 255.0f))),
          static_cast<uint8_t>(std::lround(std::clamp(pixel[1], 0.0f, 255.0f))),
          static_cast<uint8_t>(std::lround(std::clamp(pixel[2], 0.0f, 255.0f))),
          static_cast<uint8_t>(std::lround(std::clamp(pixel[3], 0.0f, 255.0f)))};
}

std::array<uint8_t, 4> EvaluateReference(const TestImage& source, const TextureGrainParams& params,
                                         const uint8_t* gradientTable, float sourceX,
                                         float sourceY) {
  auto localX = params.sourceOrigin.x + sourceX / params.pixelsPerLocalUnit;
  auto localY = params.sourceOrigin.y + sourceY / params.pixelsPerLocalUnit;
  auto grainX = (localX - params.nodeCenter.x) * params.frequencyX;
  auto grainY = (localY - params.nodeCenter.y) * params.frequencyY;
  auto noise = TextureGrainSignedFBM(gradientTable, grainX, grainY, params.seed);
  auto radiusSource = params.radius * params.pixelsPerLocalUnit;
  auto displacementX = radiusSource * noise.x;
  auto displacementY = radiusSource * noise.y;
  if (!params.clipToShape) {
    return QuantizePremultiplied(
        SamplePremultiplied(source, sourceX + displacementX, sourceY + displacementY));
  }

  auto original = SamplePremultiplied(source, sourceX, sourceY);
  auto displaced = SamplePremultiplied(source, sourceX + displacementX, sourceY + displacementY);
  auto result = original;
  if (original[3] > 0 && displaced[3] >= 1.0f) {
    for (size_t channel = 0; channel < 3; ++channel) {
      result[channel] = displaced[channel] * original[3] / displaced[3];
    }
  }
  result[3] = original[3];
  for (size_t channel = 0; channel < 3; ++channel) {
    result[channel] = std::min(result[channel], result[3]);
  }
  return QuantizePremultiplied(result);
}

TestImage EvaluateReference(const TestImage& source, const TextureGrainParams& params,
                            const Rect& outputBounds, bool applyDisplacement) {
  TestImage output = {static_cast<int>(outputBounds.width()),
                      static_cast<int>(outputBounds.height()),
                      {}};
  output.pixels.resize(static_cast<size_t>(output.width) * static_cast<size_t>(output.height) * 4);
  const auto& table = TextureGrainGradientTable();
  for (int y = 0; y < output.height; ++y) {
    for (int x = 0; x < output.width; ++x) {
      auto sourceX = outputBounds.left + static_cast<float>(x) + 0.5f;
      auto sourceY = outputBounds.top + static_cast<float>(y) + 0.5f;
      auto pixel = applyDisplacement
                       ? EvaluateReference(source, params, table.data(), sourceX, sourceY)
                       : QuantizePremultiplied(SamplePremultiplied(source, sourceX, sourceY));
      auto offset =
          (static_cast<size_t>(y) * static_cast<size_t>(output.width) + static_cast<size_t>(x)) * 4;
      output.pixels[offset] = pixel[0];
      output.pixels[offset + 1] = pixel[1];
      output.pixels[offset + 2] = pixel[2];
      output.pixels[offset + 3] = pixel[3];
    }
  }
  return output;
}

TestImage RenderImageOnGPU(Context* context, const std::shared_ptr<Image>& image, int width,
                           int height, const Paint* paint = nullptr, float left = 0.0f,
                           float top = 0.0f) {
  TestImage result = {width, height, {}};
  result.pixels.resize(static_cast<size_t>(result.width) * static_cast<size_t>(result.height) * 4);
  auto surface = Surface::Make(context, width, height, false, 1, false, 0, image->colorSpace());
  if (surface == nullptr) {
    return result;
  }
  surface->getCanvas()->drawImage(image, left, top, paint);
  auto info = ImageInfo::Make(width, height, ColorType::RGBA_8888, AlphaType::Premultiplied);
  if (!surface->readPixels(info, result.pixels.data())) {
    result.pixels.assign(result.pixels.size(), 0);
  }
  return result;
}

TestImage RenderOnGPU(Context* context, const std::shared_ptr<Image>& sourceImage,
                      const TestImage& expected, const std::shared_ptr<ImageFilter>& filter,
                      const Rect& expectedBounds, const Paint* paint = nullptr) {
  TestImage result = {expected.width, expected.height, {}};
  result.pixels.resize(static_cast<size_t>(result.width) * static_cast<size_t>(result.height) * 4);
  if (sourceImage == nullptr) {
    return result;
  }
  Point offset = {};
  auto filteredImage = sourceImage->makeWithFilter(filter, &offset);
  if (filteredImage == nullptr || filteredImage->width() != result.width ||
      filteredImage->height() != result.height) {
    return result;
  }
  EXPECT_EQ(offset.x, expectedBounds.left);
  EXPECT_EQ(offset.y, expectedBounds.top);
  return RenderImageOnGPU(context, filteredImage, result.width, result.height, paint);
}

TestImage RenderOnGPU(Context* context, const TestImage& source, const TestImage& expected,
                      const std::shared_ptr<ImageFilter>& filter, const Rect& expectedBounds,
                      const Paint* paint = nullptr) {
  return RenderOnGPU(context, MakeImage(source), expected, filter, expectedBounds, paint);
}

DiffMetrics Compare(const TestImage& first, const TestImage& second) {
  DiffMetrics metrics = {};
  if (first.width != second.width || first.height != second.height ||
      first.pixels.size() != second.pixels.size() || first.pixels.empty()) {
    metrics.maxDifference = 255;
    metrics.meanDifference = 255.0;
    metrics.fractionOverFour = 1.0;
    return metrics;
  }
  uint64_t totalDifference = 0;
  uint64_t pixelsOverFour = 0;
  auto pixelCount = static_cast<uint64_t>(first.width) * static_cast<uint64_t>(first.height);
  for (uint64_t pixel = 0; pixel < pixelCount; ++pixel) {
    int maxDifference = 0;
    for (size_t channel = 0; channel < 4; ++channel) {
      auto index = pixel * 4 + channel;
      auto difference =
          std::abs(static_cast<int>(first.pixels[index]) - static_cast<int>(second.pixels[index]));
      maxDifference = std::max(maxDifference, difference);
      totalDifference += static_cast<uint64_t>(difference);
    }
    metrics.maxDifference = std::max(metrics.maxDifference, maxDifference);
    if (maxDifference > 4) {
      pixelsOverFour++;
    }
  }
  metrics.meanDifference =
      static_cast<double>(totalDifference) / static_cast<double>(pixelCount * 4);
  metrics.fractionOverFour = static_cast<double>(pixelsOverFour) / static_cast<double>(pixelCount);
  return metrics;
}

int MaxAlphaDifference(const TestImage& first, const TestImage& second) {
  if (first.width != second.width || first.height != second.height ||
      first.pixels.size() != second.pixels.size()) {
    return 255;
  }
  int result = 0;
  for (size_t offset = 3; offset < first.pixels.size(); offset += 4) {
    result = std::max(result, std::abs(static_cast<int>(first.pixels[offset]) -
                                       static_cast<int>(second.pixels[offset])));
  }
  return result;
}

int MaxSolidColorError(const TestImage& image, int red, int green, int blue) {
  int result = 0;
  for (size_t offset = 0; offset < image.pixels.size(); offset += 4) {
    auto alpha = static_cast<int>(image.pixels[offset + 3]);
    if (alpha < 32) {
      continue;
    }
    auto expectedRed = (red * alpha + 127) / 255;
    auto expectedGreen = (green * alpha + 127) / 255;
    auto expectedBlue = (blue * alpha + 127) / 255;
    result = std::max(result, std::abs(static_cast<int>(image.pixels[offset]) - expectedRed));
    result = std::max(result, std::abs(static_cast<int>(image.pixels[offset + 1]) - expectedGreen));
    result = std::max(result, std::abs(static_cast<int>(image.pixels[offset + 2]) - expectedBlue));
  }
  return result;
}

int CountDarkCoveragePixels(const TestImage& image, int minimumAlpha) {
  int result = 0;
  for (size_t offset = 0; offset < image.pixels.size(); offset += 4) {
    auto alpha = static_cast<int>(image.pixels[offset + 3]);
    if (alpha < minimumAlpha) {
      continue;
    }
    auto maxColor = std::max({static_cast<int>(image.pixels[offset]),
                              static_cast<int>(image.pixels[offset + 1]),
                              static_cast<int>(image.pixels[offset + 2])});
    if (maxColor * 4 < alpha) {
      result++;
    }
  }
  return result;
}

void ExpectPremultiplied(const TestImage& image) {
  for (size_t offset = 0; offset < image.pixels.size(); offset += 4) {
    auto alpha = image.pixels[offset + 3];
    EXPECT_LE(image.pixels[offset], alpha);
    EXPECT_LE(image.pixels[offset + 1], alpha);
    EXPECT_LE(image.pixels[offset + 2], alpha);
  }
}

int CountAlphaOutsideSourceBounds(const TestImage& image, const Rect& outputBounds, int sourceWidth,
                                  int sourceHeight) {
  int count = 0;
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      auto sourceX = outputBounds.left + static_cast<float>(x) + 0.5f;
      auto sourceY = outputBounds.top + static_cast<float>(y) + 0.5f;
      auto outside = sourceX < 0.0f || sourceY < 0.0f ||
                     sourceX >= static_cast<float>(sourceWidth) ||
                     sourceY >= static_cast<float>(sourceHeight);
      if (outside && PixelAt(image, x, y)[3] > 0) {
        count++;
      }
    }
  }
  return count;
}

int CountAlphaOutsideCircle(const TestImage& image, const Rect& outputBounds, const Point& center,
                            float radius) {
  int count = 0;
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      auto deltaX = outputBounds.left + static_cast<float>(x) + 0.5f - center.x;
      auto deltaY = outputBounds.top + static_cast<float>(y) + 0.5f - center.y;
      if (deltaX * deltaX + deltaY * deltaY > radius * radius && PixelAt(image, x, y)[3] > 0) {
        count++;
      }
    }
  }
  return count;
}

std::shared_ptr<Image> MakeAlphaOnlyImage(int width, int height) {
  std::vector<uint8_t> alpha(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
  for (int y = 12; y < height - 12; ++y) {
    for (int x = 12; x < width - 12; ++x) {
      auto holeX = x - width / 2;
      auto holeY = y - height / 2;
      if (holeX * holeX + holeY * holeY < 6 * 6) {
        continue;
      }
      alpha[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = 255;
    }
  }
  auto info = ImageInfo::Make(width, height, ColorType::ALPHA_8, AlphaType::Premultiplied);
  auto data = Data::MakeWithCopy(alpha.data(), alpha.size());
  return Image::MakeFrom(info, data);
}

struct TestYUVImage {
  std::vector<uint8_t> y = {};
  std::vector<uint8_t> u = {};
  std::vector<uint8_t> v = {};
  std::shared_ptr<Image> image = nullptr;
};

TestYUVImage MakeTestYUVImage(bool nv12, YUVColorSpace colorSpace) {
  constexpr int width = 64;
  constexpr int height = 64;
  TestYUVImage result = {};
  result.y.resize(static_cast<size_t>(width) * height);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      auto high = (x / 8 + y / 8) % 2 == 0;
      result.y[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] =
          high ? 200 : 60;
    }
  }

  const void* addresses[3] = {};
  size_t rowBytes[3] = {};
  size_t planeCount = 0;
  if (nv12) {
    result.u.resize(static_cast<size_t>(width) * height / 2);
    for (size_t index = 0; index < result.u.size(); index += 2) {
      result.u[index] = 85;
      result.u[index + 1] = 255;
    }
    addresses[0] = result.y.data();
    addresses[1] = result.u.data();
    rowBytes[0] = width;
    rowBytes[1] = width;
    planeCount = YUVData::NV12_PLANE_COUNT;
  } else {
    auto chromaSize = static_cast<size_t>(width / 2) * static_cast<size_t>(height / 2);
    result.u.assign(chromaSize, 85);
    result.v.assign(chromaSize, 255);
    addresses[0] = result.y.data();
    addresses[1] = result.u.data();
    addresses[2] = result.v.data();
    rowBytes[0] = width;
    rowBytes[1] = width / 2;
    rowBytes[2] = width / 2;
    planeCount = YUVData::I420_PLANE_COUNT;
  }
  auto yuvData = YUVData::MakeFrom(width, height, addresses, rowBytes, planeCount);
  if (yuvData != nullptr) {
    result.image =
        nv12 ? Image::MakeNV12(yuvData, colorSpace) : Image::MakeI420(yuvData, colorSpace);
  }
  return result;
}

struct PrototypeResult {
  TestImage original = {};
  TestImage gpu = {};
  TestImage reference = {};
  DiffMetrics metrics = {};
};

PrototypeResult RunPrototype(Context* context, const TestImage& source,
                             const TextureGrainParams& params) {
  PrototypeResult result = {};
  auto filter = std::make_shared<TextureImageFilter>(params);
  auto outputBounds = filter->filterBounds(
      Rect::MakeWH(static_cast<float>(source.width), static_cast<float>(source.height)));
  result.original = EvaluateReference(source, params, outputBounds, false);
  result.reference = EvaluateReference(source, params, outputBounds, true);
  result.gpu = RenderOnGPU(context, source, result.reference, filter, outputBounds);
  result.metrics = Compare(result.gpu, result.reference);
  return result;
}

void ExpectReferenceClose(const PrototypeResult& result, const std::string& prefix,
                          int maxDifference) {
  EXPECT_LE(result.metrics.meanDifference, 0.1) << prefix;
  EXPECT_LE(result.metrics.fractionOverFour, 0.0001) << prefix;
  EXPECT_LE(result.metrics.maxDifference, maxDifference) << prefix;
  ExpectPremultiplied(result.gpu);
}

}  // namespace

TGFX_TEST(TextureImageFilterTest, OpaqueAndTranslucentReference) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  TextureGrainParams params = {};
  params.frequencyX = 0.035f;
  params.frequencyY = 0.052f;
  params.radius = 10.0f;
  params.seed = 3491;
  params.nodeCenter = Point::Make(160.0f, 120.0f);
  params.sourceOrigin = Point::Zero();
  params.pixelsPerLocalUnit = 1.0f;

  auto opaque = MakeOpaqueMotif();
  auto translucent = MakeTranslucentMotif();
  auto opaqueResult = RunPrototype(context, opaque, params);
  auto translucentResult = RunPrototype(context, translucent, params);
  ExpectReferenceClose(opaqueResult, "opaque", 4);
  ExpectReferenceClose(translucentResult, "translucent", 4);

  auto opaqueBounds = std::make_shared<TextureImageFilter>(params)->filterBounds(
      Rect::MakeWH(static_cast<float>(opaque.width), static_cast<float>(opaque.height)));
  EXPECT_GT(
      CountAlphaOutsideSourceBounds(opaqueResult.gpu, opaqueBounds, opaque.width, opaque.height),
      0);
}

TGFX_TEST(TextureImageFilterTest, RadiusZeroIsIdentity) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  TextureGrainParams params = {};
  params.radius = 0.0f;
  params.nodeCenter = Point::Make(160.0f, 120.0f);
  auto source = MakeOpaqueMotif();
  auto filter = std::make_shared<TextureImageFilter>(params);
  auto bounds = filter->filterBounds(
      Rect::MakeWH(static_cast<float>(source.width), static_cast<float>(source.height)));
  EXPECT_EQ(bounds,
            Rect::MakeWH(static_cast<float>(source.width), static_cast<float>(source.height)));

  auto reference = EvaluateReference(source, params, bounds, false);
  auto gpu = RenderOnGPU(context, source, reference, filter, bounds);
  auto metrics = Compare(gpu, reference);
  EXPECT_EQ(metrics.maxDifference, 0);
}

TGFX_TEST(TextureImageFilterTest, NoiseFixedVectors) {
  const auto& table = TextureGrainGradientTable();
  auto value = TextureGrainSignedFBM(table.data(), 12.25f, -3.5f, 3491);
  auto offset0 = TextureGrainSeedOffset(3491, 0);
  auto offset2 = TextureGrainSeedOffset(3491, 2);
  EXPECT_NEAR(value.x, -0.066452228f, 1e-6f);
  EXPECT_NEAR(value.y, -0.070894606f, 1e-6f);
  EXPECT_EQ(offset0, Point::Make(228.0f, 93.0f));
  EXPECT_EQ(offset2, Point::Make(141.0f, 56.0f));
  EXPECT_NE(TextureGrainSeedOffset(0xF1234567U, 0), TextureGrainSeedOffset(0x71234567U, 0));
}

TGFX_TEST(TextureImageFilterTest, VisualCalibrationAndClip) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto params = MakeTextureGrainParams(25.0f, 25.0f, 50.0f, Point::Make(250.0f, 250.0f), 3491);
  EXPECT_FLOAT_EQ(params.frequencyX, 0.04f);
  EXPECT_FLOAT_EQ(params.frequencyY, 0.04f);
  EXPECT_FALSE(params.clipToShape);

  auto source = MakeOrangeCircle();
  auto sourceBounds =
      Rect::MakeWH(static_cast<float>(source.width), static_cast<float>(source.height));
  auto unclippedBounds = std::make_shared<TextureImageFilter>(params)->filterBounds(sourceBounds);
  auto unclipped = RunPrototype(context, source, params);
  ExpectReferenceClose(unclipped, "visual-calibration-size25-radius50-unclipped", 4);

  params.clipToShape = true;
  auto clipped = RunPrototype(context, source, params);
  ExpectReferenceClose(clipped, "visual-calibration-size25-radius50-clipped", 12);
  auto clipBounds = std::make_shared<TextureImageFilter>(params)->filterBounds(sourceBounds);
  EXPECT_EQ(clipBounds, unclippedBounds);

  auto original = EvaluateReference(source, params, clipBounds, false);
  EXPECT_EQ(CountAlphaOutsideCircle(clipped.gpu, clipBounds, Point::Make(250.0f, 250.0f), 250.5f),
            0);
  EXPECT_LE(MaxAlphaDifference(clipped.gpu, original), 1);
  EXPECT_EQ(CountDarkCoveragePixels(clipped.gpu, 16), 0);
}

TGFX_TEST(TextureImageFilterTest, ClipKeepsRectangleSilhouette) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto source = MakeOpaqueRectangle();
  auto params = MakeTextureGrainParams(25.0f, 25.0f, 20.0f,
                                       Point::Make(static_cast<float>(source.width) * 0.5f,
                                                   static_cast<float>(source.height) * 0.5f),
                                       3491);
  auto sourceBounds =
      Rect::MakeWH(static_cast<float>(source.width), static_cast<float>(source.height));
  auto unclippedBounds = std::make_shared<TextureImageFilter>(params)->filterBounds(sourceBounds);
  params.clipToShape = true;
  auto clippedBounds = std::make_shared<TextureImageFilter>(params)->filterBounds(sourceBounds);
  EXPECT_EQ(clippedBounds, unclippedBounds);

  auto clipped = RunPrototype(context, source, params);
  ExpectReferenceClose(clipped, "rectangle-clip-on", 4);
  auto original = EvaluateReference(source, params, clippedBounds, false);
  EXPECT_EQ(MaxAlphaDifference(clipped.gpu, original), 0);
  EXPECT_LE(MaxSolidColorError(clipped.gpu, 249, 190, 113), 2);
}

TGFX_TEST(TextureImageFilterTest, AlphaOnlyPaintModulation) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto source = MakeAlphaOnlyImage(64, 64);
  ASSERT_TRUE(source != nullptr);
  EXPECT_TRUE(source->isAlphaOnly());
  auto params = MakeTextureGrainParams(12.0f, 12.0f, 8.0f, Point::Make(32.0f, 32.0f), 3491);
  auto filter = std::make_shared<TextureImageFilter>(params);
  auto bounds = filter->filterBounds(
      Rect::MakeWH(static_cast<float>(source->width()), static_cast<float>(source->height())));
  TestImage expected = {static_cast<int>(bounds.width()), static_cast<int>(bounds.height()), {}};
  Paint paint;
  paint.setColor(Color::FromRGBA(200, 80, 40, 128));
  auto result = RenderOnGPU(context, source, expected, filter, bounds, &paint);

  int maximumAlpha = 0;
  int maximumPremultipliedError = 0;
  std::vector<int> alphaValues = {};
  for (size_t offset = 0; offset < result.pixels.size(); offset += 4) {
    auto alpha = static_cast<int>(result.pixels[offset + 3]);
    maximumAlpha = std::max(maximumAlpha, alpha);
    constexpr int expectedColor[3] = {200, 80, 40};
    for (size_t channel = 0; channel < 3; ++channel) {
      auto expected = (expectedColor[channel] * alpha + 127) / 255;
      maximumPremultipliedError =
          std::max(maximumPremultipliedError,
                   std::abs(static_cast<int>(result.pixels[offset + channel]) - expected));
    }
    if (std::find(alphaValues.begin(), alphaValues.end(), alpha) == alphaValues.end()) {
      alphaValues.push_back(alpha);
    }
  }
  EXPECT_NEAR(maximumAlpha, 128, 1);
  EXPECT_LE(maximumPremultipliedError, 3);
  EXPECT_GT(alphaValues.size(), 8U);
  ExpectPremultiplied(result);
}

TGFX_TEST(TextureImageFilterTest, ClipPreservesTransparentHoles) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto source = MakeOpaqueMotif();
  auto params = MakeTextureGrainParams(18.0f, 18.0f, 14.0f, Point::Make(160.0f, 120.0f), 3491);
  auto sourceBounds =
      Rect::MakeWH(static_cast<float>(source.width), static_cast<float>(source.height));
  auto unclippedBounds = std::make_shared<TextureImageFilter>(params)->filterBounds(sourceBounds);
  params.clipToShape = true;
  auto clippedBounds = std::make_shared<TextureImageFilter>(params)->filterBounds(sourceBounds);
  EXPECT_EQ(clippedBounds, unclippedBounds);

  auto clipped = RunPrototype(context, source, params);
  auto original = EvaluateReference(source, params, clippedBounds, false);
  auto holeX = static_cast<int>(std::lround(62.5f - clippedBounds.left));
  auto holeY = static_cast<int>(std::lround(120.5f - clippedBounds.top));
  EXPECT_EQ(PixelAt(original, holeX, holeY)[3], 0);
  EXPECT_EQ(PixelAt(clipped.gpu, holeX, holeY)[3], 0);
  EXPECT_LE(MaxAlphaDifference(clipped.gpu, original), 1);
  EXPECT_GT(Compare(clipped.gpu, original).fractionOverFour, 0.01);
  ExpectPremultiplied(clipped.gpu);
}

TGFX_TEST(TextureImageFilterTest, YUVFormatsRenderThroughTextureGrain) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  for (auto nv12 : {false, true}) {
    auto source = MakeTestYUVImage(nv12, YUVColorSpace::BT601_LIMITED);
    ASSERT_TRUE(source.image != nullptr);
    auto params = MakeTextureGrainParams(16.0f, 16.0f, 8.0f, Point::Make(32.0f, 32.0f), 3491);
    auto filter = std::make_shared<TextureImageFilter>(params);
    auto bounds = filter->filterBounds(Rect::MakeWH(static_cast<float>(source.image->width()),
                                                    static_cast<float>(source.image->height())));
    TestImage expected = {static_cast<int>(bounds.width()), static_cast<int>(bounds.height()), {}};
    auto filtered = RenderOnGPU(context, source.image, expected, filter, bounds);
    auto unfiltered = RenderImageOnGPU(context, source.image, expected.width, expected.height,
                                       nullptr, -bounds.left, -bounds.top);
    EXPECT_GT(Compare(filtered, unfiltered).meanDifference, 0.1);
    EXPECT_EQ(PixelAt(filtered, expected.width / 2, expected.height / 2)[3], 255);
    ExpectPremultiplied(filtered);
  }
}

TGFX_TEST(TextureImageFilterTest, YUVColorSpaceChangesConversion) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto bt601 = MakeTestYUVImage(false, YUVColorSpace::BT601_LIMITED);
  auto bt709 = MakeTestYUVImage(false, YUVColorSpace::BT709_LIMITED);
  ASSERT_TRUE(bt601.image != nullptr);
  ASSERT_TRUE(bt709.image != nullptr);
  auto params = MakeTextureGrainParams(16.0f, 16.0f, 8.0f, Point::Make(32.0f, 32.0f), 3491);
  auto filter = std::make_shared<TextureImageFilter>(params);
  auto bounds = filter->filterBounds(Rect::MakeWH(static_cast<float>(bt601.image->width()),
                                                  static_cast<float>(bt601.image->height())));
  TestImage expected = {static_cast<int>(bounds.width()), static_cast<int>(bounds.height()), {}};
  auto bt601Result = RenderOnGPU(context, bt601.image, expected, filter, bounds);
  auto bt709Result = RenderOnGPU(context, bt709.image, expected, filter, bounds);
  auto bt601Center = PixelAt(bt601Result, expected.width / 2, expected.height / 2);
  auto bt709Center = PixelAt(bt709Result, expected.width / 2, expected.height / 2);
  EXPECT_EQ(bt601Center[3], 255);
  EXPECT_EQ(bt709Center[3], 255);
  EXPECT_GT(bt601Center[0], bt601Center[1] + 80);
  EXPECT_GT(bt601Center[0], bt601Center[2] + 80);
  EXPECT_GT(std::abs(static_cast<int>(bt601Center[0]) - static_cast<int>(bt709Center[0])) +
                std::abs(static_cast<int>(bt601Center[1]) - static_cast<int>(bt709Center[1])) +
                std::abs(static_cast<int>(bt601Center[2]) - static_cast<int>(bt709Center[2])),
            2);
}

TGFX_TEST(TextureImageFilterTest, MaxParametersComplexImageClipPair) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto source = LoadScaledImage("resources/assets/bridge.jpg", 500);
  ASSERT_FALSE(source.pixels.empty());
  auto center = Point::Make(static_cast<float>(source.width) * 0.5f,
                            static_cast<float>(source.height) * 0.5f);
  auto sourceBounds =
      Rect::MakeWH(static_cast<float>(source.width), static_cast<float>(source.height));
  auto render = [&](bool clipToShape) {
    auto params = MakeTextureGrainParams(100.0f, 100.0f, 100.0f, center, 3491, Point::Zero(), 1.0f,
                                         clipToShape);
    auto filter = std::make_shared<TextureImageFilter>(params);
    auto bounds = filter->filterBounds(sourceBounds);
    TestImage blank = {static_cast<int>(bounds.width()), static_cast<int>(bounds.height()), {}};
    blank.pixels.resize(static_cast<size_t>(blank.width) * static_cast<size_t>(blank.height) * 4);
    auto result = RenderOnGPU(context, source, blank, filter, bounds);
    return std::make_pair(std::move(result), bounds);
  };

  auto clipped = render(true);
  auto unclipped = render(false);
  EXPECT_EQ(clipped.second, unclipped.second);
  EXPECT_EQ(
      CountAlphaOutsideSourceBounds(clipped.first, clipped.second, source.width, source.height), 0);
  EXPECT_GT(
      CountAlphaOutsideSourceBounds(unclipped.first, unclipped.second, source.width, source.height),
      0);
  EXPECT_GT(Compare(clipped.first, unclipped.first).meanDifference, 0.1);
}

}  // namespace tgfx

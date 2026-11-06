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
#include <iostream>
#include <limits>
#include <vector>
#include "core/utils/MathExtra.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageInfo.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Surface.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/VectorLayer.h"
#include "tgfx/layers/layerstyles/GlassStyle.h"
#include "tgfx/layers/vectors/Ellipse.h"
#include "tgfx/layers/vectors/FillStyle.h"
#include "tgfx/layers/vectors/ShapePath.h"
#include "tgfx/layers/vectors/SolidColor.h"
#include "tgfx/layers/vectors/VectorGroup.h"
#include "utils/ContextScope.h"
#include "utils/TestUtils.h"

namespace tgfx {
namespace {

constexpr int GlassDepthSurfaceSize = 400;
constexpr float GlassDepthCenter = 200.0f;
constexpr float GlassDepthHalfExtent = 90.0f;
constexpr float GlassDepthSDFSampleX = GlassDepthCenter + GlassDepthHalfExtent - 5.0f;
constexpr float GlassDepthUDFSampleX = GlassDepthCenter + GlassDepthHalfExtent - 3.0f;
constexpr float GlassDepthCoordinatePeriod = 128.0f;

enum class GlassDepthShape { Circle, Star };

// The red channel is a linear sawtooth in layer-space x. Differencing it against an undisplaced
// read recovers the final background sample offset without relying on an opaque image diff.
static std::shared_ptr<Image> MakeCoordinateImage(int size) {
  auto info = ImageInfo::Make(size, size, ColorType::RGBA_8888, AlphaType::Premultiplied);
  std::vector<uint8_t> pixels(static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      auto offset =
          (static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)) * 4;
      auto phase = std::fmod(static_cast<float>(x) + 0.5f, GlassDepthCoordinatePeriod) /
                   GlassDepthCoordinatePeriod;
      pixels[offset] = static_cast<uint8_t>(std::round(phase * 255.0f));
      pixels[offset + 1] = static_cast<uint8_t>(
          std::round((static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 255.0f));
      pixels[offset + 2] = 0;
      pixels[offset + 3] = 255;
    }
  }
  auto data = Data::MakeWithCopy(pixels.data(), pixels.size());
  return Image::MakeFrom(info, std::move(data));
}

static std::shared_ptr<Image> MakeLinearCoordinateImage(int size) {
  auto info = ImageInfo::Make(size, size, ColorType::RGBA_8888, AlphaType::Premultiplied);
  std::vector<uint8_t> pixels(static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      auto offset =
          (static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)) * 4;
      auto red = static_cast<uint8_t>(
          std::round(static_cast<float>(x) / static_cast<float>(size - 1) * 255.0f));
      pixels[offset] = red;
      pixels[offset + 1] = static_cast<uint8_t>(
          std::round((static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 255.0f));
      pixels[offset + 2] = 0;
      pixels[offset + 3] = 255;
    }
  }
  auto data = Data::MakeWithCopy(pixels.data(), pixels.size());
  return Image::MakeFrom(info, std::move(data));
}

static Path MakeStarPath(float scale) {
  constexpr int pointCount = 5;
  constexpr float pi = 3.14159265358979323846f;
  auto centerX = GlassDepthCenter * scale;
  auto centerY = GlassDepthCenter * scale;
  auto outerRadius = GlassDepthHalfExtent * scale;
  auto innerRadius = outerRadius * 0.45f;
  Path path = {};
  for (int i = 0; i < pointCount * 2; ++i) {
    auto angle = static_cast<float>(i) * pi / static_cast<float>(pointCount);
    auto radius = i % 2 == 0 ? outerRadius : innerRadius;
    auto x = centerX + std::cos(angle) * radius;
    auto y = centerY + std::sin(angle) * radius;
    if (i == 0) {
      path.moveTo(x, y);
    } else {
      path.lineTo(x, y);
    }
  }
  path.close();
  return path;
}

static float ProbeSampleX(GlassDepthShape shape, float scale) {
  auto baseX = shape == GlassDepthShape::Star ? GlassDepthUDFSampleX : GlassDepthSDFSampleX;
  return baseX * scale;
}

static std::shared_ptr<Layer> MakeGlassProbeLayer(GlassDepthShape shape, float scale, float depth) {
  auto layer = VectorLayer::Make();
  std::shared_ptr<VectorElement> geometry = nullptr;
  if (shape == GlassDepthShape::Star) {
    auto shapePath = ShapePath::Make();
    shapePath->setPath(MakeStarPath(scale));
    geometry = std::move(shapePath);
  } else {
    auto ellipse = Ellipse::Make();
    ellipse->setPosition({GlassDepthCenter * scale, GlassDepthCenter * scale});
    ellipse->setSize({GlassDepthHalfExtent * scale * 2.0f, GlassDepthHalfExtent * scale * 2.0f});
    geometry = std::move(ellipse);
  }
  // Keep a real color painter at zero style alpha so the geometry remains available to Glass while
  // contributing no color of its own to the probe read.
  auto fill = FillStyle::Make(SolidColor::Make(Color::White()));
  fill->setAlpha(0.0f);
  auto group = VectorGroup::Make();
  group->setElements({std::move(geometry), std::move(fill)});
  layer->setContents({std::move(group)});
  layer->setLayerStyles({GlassStyle::Make(100.0f, depth, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f)});
  return layer;
}

// Returns the background red channel at the probe point. With applyGlass=false this is the
// undisplaced source coordinate; with a Glass style it is the final coordinate after the complete
// SDF/UDF pipeline, including texture generation and background sampling.
static std::shared_ptr<Surface> RenderProbeSurface(Context* context, GlassDepthShape shape,
                                                   int scale, float depth, bool applyGlass,
                                                   bool linearCoordinate, bool clippedViewport) {
  auto surfaceSize = GlassDepthSurfaceSize * scale;
  auto surface = Surface::Make(context, surfaceSize, surfaceSize, ColorType::RGBA_F16);
  if (surface == nullptr) {
    surface = Surface::Make(context, surfaceSize, surfaceSize);
  }
  if (surface == nullptr) {
    return nullptr;
  }

  DisplayList displayList;
  auto background = ImageLayer::Make();
  background->setImage(linearCoordinate ? MakeLinearCoordinateImage(surfaceSize)
                                        : MakeCoordinateImage(surfaceSize));
  background->setSampling(SamplingOptions(FilterMode::Linear));
  displayList.root()->addChild(background);

  if (applyGlass) {
    auto glassLayer = MakeGlassProbeLayer(shape, static_cast<float>(scale), depth);
    if (clippedViewport) {
      auto viewport = Layer::Make();
      viewport->setScrollRect(
          Rect::MakeWH(320.0f * static_cast<float>(scale), 320.0f * static_cast<float>(scale)));
      viewport->addChild(std::move(glassLayer));
      displayList.root()->addChild(std::move(viewport));
    } else {
      displayList.root()->addChild(std::move(glassLayer));
    }
  }

  displayList.render(surface.get());
  return surface;
}

static float RenderProbeRedAt(Context* context, GlassDepthShape shape, int scale, float depth,
                              bool applyGlass, float sampleX, bool clippedViewport = false) {
  auto surface =
      RenderProbeSurface(context, shape, scale, depth, applyGlass, false, clippedViewport);
  if (surface == nullptr) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  auto color = surface->getColor(static_cast<int>(sampleX),
                                 static_cast<int>(GlassDepthCenter * static_cast<float>(scale)));
  return color.red;
}

static std::vector<float> RenderLinearProbeRedRow(Context* context, GlassDepthShape shape,
                                                  int scale, float depth, bool applyGlass) {
  auto surface = RenderProbeSurface(context, shape, scale, depth, applyGlass, true, false);
  auto surfaceSize = GlassDepthSurfaceSize * scale;
  std::vector<float> reds = {};
  if (surface == nullptr) {
    return reds;
  }
  auto info = ImageInfo::Make(surfaceSize, 1, ColorType::RGBA_F16, AlphaType::Premultiplied);
  std::vector<uint16_t> pixels(static_cast<size_t>(surfaceSize) * 4);
  auto sampleY = static_cast<int>(GlassDepthCenter * static_cast<float>(scale));
  if (!surface->readPixels(info, pixels.data(), 0, sampleY)) {
    return reds;
  }
  reds.resize(static_cast<size_t>(surfaceSize));
  for (int x = 0; x < surfaceSize; ++x) {
    reds[static_cast<size_t>(x)] = HalfToFloat(pixels[static_cast<size_t>(x) * 4]);
  }
  return reds;
}

static float RenderProbeRed(Context* context, GlassDepthShape shape, int scale, float depth,
                            bool applyGlass) {
  return RenderProbeRedAt(context, shape, scale, depth, applyGlass,
                          ProbeSampleX(shape, static_cast<float>(scale)));
}

static float MeasureDisplacementPixels(Context* context, GlassDepthShape shape, int scale,
                                       float depth) {
  auto sourceRed = RenderProbeRed(context, shape, scale, 0.0f, false);
  auto refractedRed = RenderProbeRed(context, shape, scale, depth, true);
  if (!std::isfinite(sourceRed) || !std::isfinite(refractedRed)) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  return std::abs(sourceRed - refractedRed) * GlassDepthCoordinatePeriod;
}

static std::array<float, 3> MeasureScaleSeries(Context* context, GlassDepthShape shape) {
  std::array<float, 3> displacements = {};
  constexpr std::array<int, 3> scales = {1, 2, 4};
  for (size_t i = 0; i < scales.size(); ++i) {
    auto scale = scales[i];
    displacements[i] =
        MeasureDisplacementPixels(context, shape, scale, 20.0f * static_cast<float>(scale));
    std::cout << "[glass-depth] " << (shape == GlassDepthShape::Star ? "UDF" : "SDF")
              << " K=" << scale << " displacement=" << displacements[i] << " px\n";
  }
  return displacements;
}

static void ExpectLinearScaleSeries(const std::array<float, 3>& displacements) {
  ASSERT_TRUE(std::isfinite(displacements[0]));
  ASSERT_TRUE(std::isfinite(displacements[1]));
  ASSERT_TRUE(std::isfinite(displacements[2]));
  ASSERT_GT(displacements[0], 0.5f);
  auto ratio2 = displacements[1] / displacements[0];
  auto ratio4 = displacements[2] / displacements[0];
  EXPECT_NEAR(ratio2, 2.0f, 0.25f) << "K=2 must scale the final sampled displacement, not K^2";
  EXPECT_LT(ratio2, 3.0f) << "K=2 is close to the old K^2 behavior";
  EXPECT_NEAR(ratio4, 4.0f, 0.6f) << "K=4 must scale the final sampled displacement linearly";
  EXPECT_LT(ratio4, 6.0f) << "K=4 is close to the old quadratic behavior";
}

}  // namespace

TGFX_TEST_PRIVATE(GlassDepthTest, PixelThicknessAndRawDepthContract) {
  auto style = GlassStyle::Make(100.0f, 60.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
  TGFX_PRIVATE_ACCESS({
    EXPECT_FLOAT_EQ(style->getGlassThickness(90.0f), 60.0f);
    style->setDepth(150.0f);
    EXPECT_FLOAT_EQ(style->depth(), 150.0f);
    EXPECT_FLOAT_EQ(style->getGlassThickness(200.0f), 150.0f);
    style->setDepth(250.0f);
    EXPECT_FLOAT_EQ(style->depth(), 250.0f);
    EXPECT_FLOAT_EQ(style->getGlassThickness(200.0f), 200.0f);
    style->setDepth(0.0f);
    EXPECT_FLOAT_EQ(style->depth(), 0.0f);
    EXPECT_FLOAT_EQ(style->getGlassThickness(90.0f), 0.0f);
    style->setDepth(-30.0f);
    EXPECT_FLOAT_EQ(style->depth(), -30.0f);
    EXPECT_FLOAT_EQ(style->getGlassThickness(90.0f), 0.0f);
    EXPECT_TRUE(std::isfinite(style->getGlassThickness(90.0f)));
  })

  auto negative = GlassStyle::Make(100.0f, -30.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
  TGFX_PRIVATE_ACCESS({
    EXPECT_FLOAT_EQ(negative->depth(), -30.0f);
    EXPECT_FLOAT_EQ(negative->getGlassThickness(90.0f), 0.0f);
  })
}

TGFX_TEST(GlassDepthTest, SDFDisplacementScalesLinearly) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  ExpectLinearScaleSeries(MeasureScaleSeries(context, GlassDepthShape::Circle));
}

TGFX_TEST(GlassDepthTest, UDFDisplacementScalesLinearly) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  ExpectLinearScaleSeries(MeasureScaleSeries(context, GlassDepthShape::Star));
}

TGFX_TEST(GlassDepthTest, UDFDoesNotTruncateDepthAboveOneHundred) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  constexpr int scale = 2;
  auto source = RenderLinearProbeRedRow(context, GlassDepthShape::Star, scale, 0.0f, false);
  ASSERT_EQ(source.size(), static_cast<size_t>(GlassDepthSurfaceSize * scale));

  auto measureMax = [&](float depth) {
    auto refracted = RenderLinearProbeRedRow(context, GlassDepthShape::Star, scale, depth, true);
    EXPECT_EQ(refracted.size(), source.size());
    if (refracted.size() != source.size()) {
      return std::numeric_limits<float>::quiet_NaN();
    }
    auto maxDisplacement = 0.0f;
    auto startX = static_cast<int>(GlassDepthCenter * static_cast<float>(scale));
    auto endX =
        static_cast<int>((GlassDepthCenter + GlassDepthHalfExtent) * static_cast<float>(scale));
    for (int x = startX; x <= endX; ++x) {
      auto index = static_cast<size_t>(x);
      auto displacement =
          (source[index] - refracted[index]) * static_cast<float>(GlassDepthSurfaceSize * scale);
      maxDisplacement = std::max(maxDisplacement, displacement);
    }
    return maxDisplacement;
  };

  auto at100 = measureMax(100.0f);
  auto at150 = measureMax(150.0f);
  std::cout << "[glass-depth] UDF max row displacement depth=100/150=" << at100 << "/" << at150
            << " px\n";
  ASSERT_TRUE(std::isfinite(at100));
  ASSERT_TRUE(std::isfinite(at150));
  ASSERT_GT(at100, 0.5f);
  EXPECT_NEAR(at150 / at100, 1.5f, 0.25f)
      << "depth 150 below minHalf 180 must not collapse to the depth=100 result";
}

TGFX_TEST(GlassDepthTest, ClippedEvaluationKeepsFinalDisplacement) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto sampleX = ProbeSampleX(GlassDepthShape::Star, 1.0f);
  auto fullRed = RenderProbeRedAt(context, GlassDepthShape::Star, 1, 20.0f, true, sampleX, false);
  auto clippedRed = RenderProbeRedAt(context, GlassDepthShape::Star, 1, 20.0f, true, sampleX, true);
  ASSERT_TRUE(std::isfinite(fullRed));
  ASSERT_TRUE(std::isfinite(clippedRed));
  EXPECT_NEAR(clippedRed, fullRed, 0.001f)
      << "local evaluation must preserve the same final background sample";
}

TGFX_TEST(GlassDepthTest, ZeroAndNegativeDepthDoNotDisplace) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  for (auto shape : {GlassDepthShape::Circle, GlassDepthShape::Star}) {
    auto sourceRed = RenderProbeRed(context, shape, 1, 0.0f, false);
    auto zeroRed = RenderProbeRed(context, shape, 1, 0.0f, true);
    auto negativeRed = RenderProbeRed(context, shape, 1, -30.0f, true);
    ASSERT_TRUE(std::isfinite(sourceRed));
    ASSERT_TRUE(std::isfinite(zeroRed));
    ASSERT_TRUE(std::isfinite(negativeRed));
    EXPECT_NEAR(zeroRed, sourceRed, 0.001f);
    EXPECT_NEAR(negativeRed, sourceRed, 0.001f);
  }
}

}  // namespace tgfx

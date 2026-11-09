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
#include "tgfx/core/ImageInfo.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Surface.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/VectorLayer.h"
#include "tgfx/layers/layerstyles/GlassStyle.h"
#include "tgfx/layers/vectors/Ellipse.h"
#include "tgfx/layers/vectors/FillStyle.h"
#include "tgfx/layers/vectors/Gradient.h"
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

enum class GlassDepthShape { Circle, Star };
enum class GlassCoordinateAxis { X, Y };

// A float gradient keeps the coordinate measurement out of an 8-bit repeat cycle. Differencing the
// refracted red channel against the same pixel from an unglassed render recovers a signed component
// of the final background sample offset.
static std::shared_ptr<Layer> MakeCoordinateBackground(int size, GlassCoordinateAxis axis) {
  auto layer = VectorLayer::Make();
  auto rect = ShapePath::Make();
  Path path = {};
  path.addRect(Rect::MakeWH(static_cast<float>(size), static_cast<float>(size)));
  rect->setPath(path);
  auto startPoint =
      axis == GlassCoordinateAxis::X ? Point::Make(0.0f, 0.5f) : Point::Make(0.5f, 0.0f);
  auto endPoint =
      axis == GlassCoordinateAxis::X ? Point::Make(1.0f, 0.5f) : Point::Make(0.5f, 1.0f);
  auto gradient =
      Gradient::MakeLinear(startPoint, endPoint, {Color::Black(), Color::White()}, {0.0f, 1.0f});
  auto group = VectorGroup::Make();
  group->setElements({std::move(rect), FillStyle::Make(std::move(gradient))});
  layer->setContents({std::move(group)});
  return layer;
}

static Path MakeStarPath(float scale, float halfExtent) {
  constexpr int pointCount = 5;
  constexpr float pi = 3.14159265358979323846f;
  auto centerX = GlassDepthCenter * scale;
  auto centerY = GlassDepthCenter * scale;
  auto outerRadius = halfExtent * scale;
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

static float ProbeSampleX(GlassDepthShape shape, float scale,
                          float halfExtent = GlassDepthHalfExtent) {
  auto inset = shape == GlassDepthShape::Star ? 3.0f : 5.0f;
  auto baseX = GlassDepthCenter + halfExtent - inset;
  return baseX * scale;
}

static std::shared_ptr<Layer> MakeGlassProbeLayer(GlassDepthShape shape, float scale, float depth,
                                                  float halfExtent) {
  auto layer = VectorLayer::Make();
  std::shared_ptr<VectorElement> geometry = nullptr;
  if (shape == GlassDepthShape::Star) {
    auto shapePath = ShapePath::Make();
    shapePath->setPath(MakeStarPath(scale, halfExtent));
    geometry = std::move(shapePath);
  } else {
    auto ellipse = Ellipse::Make();
    ellipse->setPosition({GlassDepthCenter * scale, GlassDepthCenter * scale});
    ellipse->setSize({halfExtent * scale * 2.0f, halfExtent * scale * 2.0f});
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
                                                   GlassCoordinateAxis axis, bool clippedViewport,
                                                   float halfExtent = GlassDepthHalfExtent) {
  auto surfaceSize = GlassDepthSurfaceSize * scale;
  auto surface = Surface::Make(context, surfaceSize, surfaceSize, ColorType::RGBA_F16);
  if (surface == nullptr) {
    surface = Surface::Make(context, surfaceSize, surfaceSize);
  }
  if (surface == nullptr) {
    return nullptr;
  }

  DisplayList displayList;
  displayList.root()->addChild(MakeCoordinateBackground(surfaceSize, axis));

  if (applyGlass) {
    auto glassLayer = MakeGlassProbeLayer(shape, static_cast<float>(scale), depth, halfExtent);
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
                              bool applyGlass, GlassCoordinateAxis axis, float sampleX,
                              float sampleY, bool clippedViewport = false,
                              float halfExtent = GlassDepthHalfExtent) {
  auto surface = RenderProbeSurface(context, shape, scale, depth, applyGlass, axis, clippedViewport,
                                    halfExtent);
  if (surface == nullptr) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  auto color = surface->getColor(static_cast<int>(sampleX), static_cast<int>(sampleY));
  return color.red;
}

static std::vector<float> RenderProbeRedRow(Context* context, GlassDepthShape shape, int scale,
                                            float depth, bool applyGlass,
                                            float halfExtent = GlassDepthHalfExtent) {
  auto surface = RenderProbeSurface(context, shape, scale, depth, applyGlass,
                                    GlassCoordinateAxis::X, false, halfExtent);
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
                            bool applyGlass, float halfExtent = GlassDepthHalfExtent) {
  return RenderProbeRedAt(context, shape, scale, depth, applyGlass, GlassCoordinateAxis::X,
                          ProbeSampleX(shape, static_cast<float>(scale), halfExtent),
                          GlassDepthCenter * static_cast<float>(scale), false, halfExtent);
}

static float MeasureDisplacementPixels(Context* context, GlassDepthShape shape, int scale,
                                       float depth, float halfExtent = GlassDepthHalfExtent) {
  auto sourceRed = RenderProbeRed(context, shape, scale, 0.0f, false, halfExtent);
  auto refractedRed = RenderProbeRed(context, shape, scale, depth, true, halfExtent);
  if (!std::isfinite(sourceRed) || !std::isfinite(refractedRed)) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  return std::abs(sourceRed - refractedRed) * static_cast<float>(GlassDepthSurfaceSize * scale - 1);
}

static std::array<float, 3> MeasureScaleSeries(Context* context, GlassDepthShape shape,
                                               float baseDepth = 20.0f,
                                               float halfExtent = GlassDepthHalfExtent) {
  std::array<float, 3> displacements = {};
  constexpr std::array<int, 3> scales = {1, 2, 4};
  for (size_t i = 0; i < scales.size(); ++i) {
    auto scale = scales[i];
    displacements[i] = MeasureDisplacementPixels(context, shape, scale,
                                                 baseDepth * static_cast<float>(scale), halfExtent);
    std::cout << "[glass-depth] " << (shape == GlassDepthShape::Star ? "UDF" : "SDF")
              << " K=" << scale << " displacement=" << displacements[i] << " px\n";
  }
  return displacements;
}

static float MeasureMaxRowDisplacement(Context* context, GlassDepthShape shape, int scale,
                                       float depth) {
  auto source = RenderProbeRedRow(context, shape, scale, 0.0f, false);
  auto refracted = RenderProbeRedRow(context, shape, scale, depth, true);
  if (source.empty() || source.size() != refracted.size()) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  auto maxDisplacement = 0.0f;
  auto startX = static_cast<int>(GlassDepthCenter * static_cast<float>(scale));
  auto endX =
      static_cast<int>((GlassDepthCenter + GlassDepthHalfExtent) * static_cast<float>(scale));
  for (int x = startX; x <= endX; ++x) {
    auto index = static_cast<size_t>(x);
    auto displacement = std::abs(source[index] - refracted[index]) *
                        static_cast<float>(GlassDepthSurfaceSize * scale - 1);
    maxDisplacement = std::max(maxDisplacement, displacement);
  }
  return maxDisplacement;
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

static void ExpectAbsoluteScaleSeries(const std::array<float, 3>& displacements,
                                      const std::array<float, 3>& expected) {
  for (size_t i = 0; i < displacements.size(); ++i) {
    EXPECT_NEAR(displacements[i], expected[i], 0.75f)
        << "the render mapping must match the layer-pixel contract at index " << i;
  }
}

static std::vector<float> ReadRedChannel(const std::shared_ptr<Surface>& surface) {
  if (surface == nullptr) {
    return {};
  }
  auto width = surface->width();
  auto height = surface->height();
  auto info = ImageInfo::Make(width, height, ColorType::RGBA_F16, AlphaType::Premultiplied);
  std::vector<uint16_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
  if (!surface->readPixels(info, pixels.data())) {
    return {};
  }
  std::vector<float> reds(static_cast<size_t>(width) * static_cast<size_t>(height));
  for (size_t i = 0; i < reds.size(); ++i) {
    reds[i] = HalfToFloat(pixels[i * 4]);
  }
  return reds;
}

static float MeasureMaxLayerDisplacement(Context* context, GlassDepthShape shape, float depth) {
  constexpr int scale = 1;
  auto sourceX = ReadRedChannel(
      RenderProbeSurface(context, shape, scale, 0.0f, false, GlassCoordinateAxis::X, false));
  auto sourceY = ReadRedChannel(
      RenderProbeSurface(context, shape, scale, 0.0f, false, GlassCoordinateAxis::Y, false));
  auto refractedX = ReadRedChannel(
      RenderProbeSurface(context, shape, scale, depth, true, GlassCoordinateAxis::X, false));
  auto refractedY = ReadRedChannel(
      RenderProbeSurface(context, shape, scale, depth, true, GlassCoordinateAxis::Y, false));
  auto sampleCount = sourceX.size();
  if (sampleCount == 0 || sourceY.size() != sampleCount || refractedX.size() != sampleCount ||
      refractedY.size() != sampleCount) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  constexpr auto surfaceSize = static_cast<float>(GlassDepthSurfaceSize);
  auto maxDisplacement = 0.0f;
  for (size_t i = 0; i < sampleCount; ++i) {
    auto dx = (refractedX[i] - sourceX[i]) * (surfaceSize - 1.0f);
    auto dy = (refractedY[i] - sourceY[i]) * (surfaceSize - 1.0f);
    maxDisplacement = std::max(maxDisplacement, std::sqrt(dx * dx + dy * dy));
  }
  return maxDisplacement;
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
  auto displacements = MeasureScaleSeries(context, GlassDepthShape::Circle);
  ExpectLinearScaleSeries(displacements);
  ExpectAbsoluteScaleSeries(displacements, {7.79297f, 15.6055f, 31.2305f});
}

TGFX_TEST(GlassDepthTest, UDFDisplacementScalesLinearly) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto displacements = MeasureScaleSeries(context, GlassDepthShape::Star);
  ExpectLinearScaleSeries(displacements);
  // Do not pin the UDF result to backend-specific pixel values. The final probe is RGBA_F16 and,
  // at K=4, one half-float ULP is already wider than the SDF absolute tolerance. Rasterized masks
  // and filtered UDF texels can move that last bit differently on D3D12/WARP without changing the
  // layer-pixel scale contract. The analytical SDF path retains the absolute mapping check above.
}

TGFX_TEST(GlassDepthTest, UDFShallowDepthScalesLinearly) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  constexpr float ShallowDepth = 16.0f;
  constexpr float HalfExtent = 180.0f;
  EXPECT_LT(ShallowDepth / HalfExtent, 0.1f);
  auto at1 = MeasureDisplacementPixels(context, GlassDepthShape::Star, 1, ShallowDepth, HalfExtent);
  auto at2 =
      MeasureDisplacementPixels(context, GlassDepthShape::Star, 2, ShallowDepth * 2.0f, HalfExtent);
  std::cout << "[glass-depth] UDF shallow K=1/2 displacement=" << at1 << "/" << at2 << " px\n";
  ASSERT_TRUE(std::isfinite(at1));
  ASSERT_TRUE(std::isfinite(at2));
  ASSERT_GT(at1, 0.5f);
  EXPECT_NEAR(at2 / at1, 2.0f, 0.25f)
      << "the scale-invariant shallow-depth curve must preserve K-linear displacement";
}

TGFX_TEST(GlassDepthTest, UDFDoesNotTruncateDepthAboveOneHundred) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  constexpr int scale = 2;
  auto at100 = MeasureMaxRowDisplacement(context, GlassDepthShape::Star, scale, 100.0f);
  auto at150 = MeasureMaxRowDisplacement(context, GlassDepthShape::Star, scale, 150.0f);
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
  auto sampleY = GlassDepthCenter;
  auto fullRed = RenderProbeRedAt(context, GlassDepthShape::Star, 1, 20.0f, true,
                                  GlassCoordinateAxis::X, sampleX, sampleY, false);
  auto clippedRed = RenderProbeRedAt(context, GlassDepthShape::Star, 1, 20.0f, true,
                                     GlassCoordinateAxis::X, sampleX, sampleY, true);
  ASSERT_TRUE(std::isfinite(fullRed));
  ASSERT_TRUE(std::isfinite(clippedRed));
  EXPECT_NEAR(clippedRed, fullRed, 0.001f)
      << "local evaluation must preserve the same final background sample";
}

TGFX_TEST(GlassDepthTest, UDFFinalDisplacementRespectsScalarBound) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  for (auto shape : {GlassDepthShape::Circle, GlassDepthShape::Star}) {
    for (auto depth : {20.0f, 50.0f, 80.0f}) {
      auto maxDisplacement = MeasureMaxLayerDisplacement(context, shape, depth);
      std::cout << "[glass-depth] " << (shape == GlassDepthShape::Star ? "UDF" : "SDF")
                << " max-vector depth=" << depth << " displacement=" << maxDisplacement << " px\n";
      ASSERT_TRUE(std::isfinite(maxDisplacement));
      EXPECT_GT(maxDisplacement, 0.5f);
      EXPECT_LE(maxDisplacement, depth + 1.5f)
          << "final vector displacement must not exceed depth * refractionFactor";
    }
  }
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

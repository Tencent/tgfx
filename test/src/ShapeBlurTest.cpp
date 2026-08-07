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

#include <cmath>
#include <vector>
#include "core/shaders/ShapeBlurShader.h"
#include "gpu/DrawingManager.h"
#include "gpu/glsl/processors/GLSLShapeBlurFunctions.h"
#include "gpu/processors/RRectBlurFragmentProcessor.h"
#include "gpu/processors/RRectInnerShadowFragmentProcessor.h"
#include "gpu/processors/RectBlurFragmentProcessor.h"
#include "gpu/processors/RectInnerShadowFragmentProcessor.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gtest/gtest.h"
#include "layers/SpreadUtils.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Surface.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/StrokeAlign.h"
#include "tgfx/layers/layerstyles/DropShadowStyle.h"
#include "tgfx/layers/layerstyles/InnerShadowStyle.h"
#include "utils/TestUtils.h"

namespace tgfx {

// CPU reference implementations of the closed form the shaders evaluate. Both sides share the same
// formula, so a mismatch points at the GLSL translation or the uniform plumbing rather than at the
// mathematics.

static constexpr float KernelTrunc = 2.0f;

static float RefCDF(float u) {
  if (u <= -KernelTrunc) {
    return 0.0f;
  }
  if (u >= KernelTrunc) {
    return 1.0f;
  }
  const auto low = 0.02275013f;
  const auto mass = 0.95449974f;
  auto cur = 0.5f * (1.0f + std::erf(u * 0.70710678f));
  return (cur - low) / mass;
}

static float RefRectCoverage(float cx, float cy, float hx, float hy) {
  return (RefCDF(cx + hx) - RefCDF(cx - hx)) * (RefCDF(cy + hy) - RefCDF(cy - hy));
}

static float RefRowSpan(float x, float y, float cornerAlong, float cornerCross, float halfCross,
                        float halfAlong) {
  auto delta = std::min(halfAlong - cornerAlong - std::abs(y), 0.0f);
  auto ratio = cornerCross / std::max(cornerAlong, 1e-6f);
  auto curved = halfCross - cornerCross +
                ratio * std::sqrt(std::max(0.0f, cornerAlong * cornerAlong - delta * delta));
  return RefCDF(x + curved) - RefCDF(x - curved);
}

static float RefRoundRectCoverage(float cx, float cy, float hx, float hy, float rx, float ry) {
  const int n = ShapeBlurQuadratureCount;
  float alongCoord = 0.0f;
  float alongHalf = 0.0f;
  bool swapAxes = false;
  if (std::abs(cx) > std::abs(cy)) {
    alongCoord = cy;
    alongHalf = hy;
    swapAxes = false;
  } else {
    alongCoord = cx;
    alongHalf = hx;
    swapAxes = true;
  }
  auto lo = alongCoord - alongHalf;
  auto hi = alongCoord + alongHalf;
  auto start = std::min(std::max(-KernelTrunc, lo), hi);
  auto end = std::min(std::max(KernelTrunc, lo), hi);
  if (start == end) {
    return 0.0f;
  }
  auto step = (end - start) / static_cast<float>(n);
  auto s = start + step * 0.5f;
  auto accum = 0.0f;
  auto weightSum = 0.0f;
  for (int i = 0; i < n; i++) {
    auto weight = std::exp(-0.5f * s * s);
    auto span =
        swapAxes ? RefRowSpan(cy, cx - s, rx, ry, hy, hx) : RefRowSpan(cx, cy - s, ry, rx, hx, hy);
    accum += span * weight;
    weightSum += weight;
    s += step;
  }
  if (weightSum <= 0.0f) {
    return 0.0f;
  }
  return accum * (RefCDF(end) - RefCDF(start)) / weightSum;
}

static float RefRectSDF(float px, float py, float hx, float hy) {
  auto dx = std::abs(px) - hx;
  auto dy = std::abs(py) - hy;
  auto outsideX = std::max(dx, 0.0f);
  auto outsideY = std::max(dy, 0.0f);
  return std::sqrt(outsideX * outsideX + outsideY * outsideY) + std::min(std::max(dx, dy), 0.0f);
}

static float RefRoundRectSDF(float px, float py, float hx, float hy, float corner) {
  auto r = std::min(corner, std::min(hx, hy));
  return RefRectSDF(px, py, hx - r, hy - r) - r;
}

static float RefMaskCoverage(float sdf) {
  return std::min(std::max(0.5f - sdf, 0.0f), 1.0f);
}

// Renders one FP into a render target and returns the raw pixels.
static std::vector<uint8_t> RenderFP(Context* context, int width, int height,
                                     PlacementPtr<FragmentProcessor> fp) {
  auto renderTarget = RenderTargetProxy::Make(context, width, height, false);
  if (renderTarget == nullptr || fp == nullptr) {
    return {};
  }
  context->drawingManager()->fillRTWithFP(renderTarget, std::move(fp), 0);
  auto surface = Surface::MakeFrom(renderTarget);
  if (surface == nullptr) {
    return {};
  }
  context->flushAndSubmit();
  auto info = ImageInfo::Make(width, height, ColorType::RGBA_8888, AlphaType::Premultiplied);
  std::vector<uint8_t> pixels(static_cast<size_t>(info.byteSize()));
  if (!surface->readPixels(info, pixels.data())) {
    return {};
  }
  return pixels;
}

TGFX_TEST(ShapeBlurTest, RectAndRRectAgainstReference) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  const int width = 64;
  const int height = 48;
  const auto centerX = static_cast<float>(width) * 0.5f;
  const auto centerY = static_cast<float>(height) * 0.5f;

  // Case 1: rectangle, anisotropic sigma. Geometry is expressed directly in sigma units; uvMatrix
  // maps pixel coordinates to that space (translate to center, then divide by sigma).
  {
    const float sigmaX = 6.0f;
    const float sigmaY = 3.0f;
    const Point halfOverSigma = {14.0f / sigmaX, 9.0f / sigmaY};
    auto uvMatrix = Matrix::MakeTrans(-centerX, -centerY);
    uvMatrix.postScale(1.0f / sigmaX, 1.0f / sigmaY);
    auto fp = RectBlurFragmentProcessor::Make(context->drawingAllocator(), halfOverSigma,
                                              Color::White().premultiply(), &uvMatrix);
    auto pixels = RenderFP(context, width, height, std::move(fp));
    ASSERT_FALSE(pixels.empty());

    float worst = 0.0f;
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        auto cx = (static_cast<float>(x) + 0.5f - centerX) / sigmaX;
        auto cy = (static_cast<float>(y) + 0.5f - centerY) / sigmaY;
        auto expected = RefRectCoverage(cx, cy, halfOverSigma.x, halfOverSigma.y);
        auto got = static_cast<float>(pixels[(static_cast<size_t>(y) * static_cast<size_t>(width) +
                                              static_cast<size_t>(x)) *
                                             4]) /
                   255.0f;
        worst = std::max(worst, std::abs(got - expected));
      }
    }
    EXPECT_LT(worst * 255.0f, 2.0f);
  }

  // Case 2: rounded rect with a circular corner under anisotropic sigma, so the normalized corner
  // becomes elliptical. This exercises the two-semi-axis rowSpan path.
  {
    const float sigmaX = 5.0f;
    const float sigmaY = 2.5f;
    const float radius = 6.0f;
    const Point halfOverSigma = {16.0f / sigmaX, 10.0f / sigmaY};
    const Point cornerOverSigma = {radius / sigmaX, radius / sigmaY};
    auto uvMatrix = Matrix::MakeTrans(-centerX, -centerY);
    uvMatrix.postScale(1.0f / sigmaX, 1.0f / sigmaY);
    auto fp =
        RRectBlurFragmentProcessor::Make(context->drawingAllocator(), halfOverSigma,
                                         cornerOverSigma, Color::White().premultiply(), &uvMatrix);
    auto pixels = RenderFP(context, width, height, std::move(fp));
    ASSERT_FALSE(pixels.empty());

    float worst = 0.0f;
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        auto cx = (static_cast<float>(x) + 0.5f - centerX) / sigmaX;
        auto cy = (static_cast<float>(y) + 0.5f - centerY) / sigmaY;
        float expected = 0.0f;
        if (std::abs(cx) < halfOverSigma.x - (cornerOverSigma.x + 2.0f) &&
            std::abs(cy) < halfOverSigma.y - (cornerOverSigma.y + 2.0f)) {
          expected = 1.0f;
        } else {
          expected = RefRoundRectCoverage(cx, cy, halfOverSigma.x, halfOverSigma.y,
                                          cornerOverSigma.x, cornerOverSigma.y);
        }
        auto got = static_cast<float>(pixels[(static_cast<size_t>(y) * static_cast<size_t>(width) +
                                              static_cast<size_t>(x)) *
                                             4]) /
                   255.0f;
        worst = std::max(worst, std::abs(got - expected));
      }
    }
    EXPECT_LT(worst * 255.0f, 2.0f);
  }
}

TGFX_TEST(ShapeBlurTest, InnerShadowAgainstReference) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  const int width = 64;
  const int height = 48;
  const auto centerX = static_cast<float>(width) * 0.5f;
  const auto centerY = static_cast<float>(height) * 0.5f;
  // The mask coordinates stay in unnormalized pixels, so the transform only centers them.
  const auto uvMatrix = Matrix::MakeTrans(-centerX, -centerY);

  // Case 1: rectangular inner shadow with an offset, so the shadow is displaced while the mask is
  // not. Anisotropic sigma exercises the per-axis normalization the shader applies itself.
  {
    const float sigmaX = 5.0f;
    const float sigmaY = 2.5f;
    const Point invSigma = {1.0f / sigmaX, 1.0f / sigmaY};
    const Point maskHalfSize = {20.0f, 14.0f};
    const Point shadowCenterOffset = {3.0f, -2.0f};
    const Point shadowHalfOverSigma = {16.0f / sigmaX, 10.0f / sigmaY};
    auto fp = RectInnerShadowFragmentProcessor::Make(
        context->drawingAllocator(), shadowHalfOverSigma, shadowCenterOffset, invSigma,
        maskHalfSize, Color::White().premultiply(), &uvMatrix);
    auto pixels = RenderFP(context, width, height, std::move(fp));
    ASSERT_FALSE(pixels.empty());

    float worst = 0.0f;
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        auto mx = static_cast<float>(x) + 0.5f - centerX;
        auto my = static_cast<float>(y) + 0.5f - centerY;
        auto sx = (mx - shadowCenterOffset.x) * invSigma.x;
        auto sy = (my - shadowCenterOffset.y) * invSigma.y;
        auto expected =
            1.0f - RefRectCoverage(sx, sy, shadowHalfOverSigma.x, shadowHalfOverSigma.y);
        expected *= RefMaskCoverage(RefRectSDF(mx, my, maskHalfSize.x, maskHalfSize.y));
        auto got = static_cast<float>(pixels[(static_cast<size_t>(y) * static_cast<size_t>(width) +
                                              static_cast<size_t>(x)) *
                                             4]) /
                   255.0f;
        worst = std::max(worst, std::abs(got - expected));
      }
    }
    EXPECT_LT(worst * 255.0f, 2.0f);
  }

  // Case 2: rounded inner shadow. The mask radius stays circular while sigma normalization turns the
  // shadow's corner elliptical, which is the asymmetry between the two coordinate spaces.
  {
    const float sigmaX = 4.0f;
    const float sigmaY = 2.0f;
    const Point invSigma = {1.0f / sigmaX, 1.0f / sigmaY};
    const Point maskHalfSize = {20.0f, 14.0f};
    const float maskCornerRadius = 6.0f;
    const Point shadowCenterOffset = {2.0f, 2.0f};
    const Point shadowHalfOverSigma = {16.0f / sigmaX, 10.0f / sigmaY};
    const Point shadowCornerOverSigma = {4.0f / sigmaX, 4.0f / sigmaY};
    auto fp = RRectInnerShadowFragmentProcessor::Make(
        context->drawingAllocator(), shadowHalfOverSigma, shadowCornerOverSigma, shadowCenterOffset,
        invSigma, maskHalfSize, maskCornerRadius, Color::White().premultiply(), &uvMatrix);
    auto pixels = RenderFP(context, width, height, std::move(fp));
    ASSERT_FALSE(pixels.empty());

    float worst = 0.0f;
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        auto mx = static_cast<float>(x) + 0.5f - centerX;
        auto my = static_cast<float>(y) + 0.5f - centerY;
        auto sx = (mx - shadowCenterOffset.x) * invSigma.x;
        auto sy = (my - shadowCenterOffset.y) * invSigma.y;
        auto expected =
            1.0f - RefRoundRectCoverage(sx, sy, shadowHalfOverSigma.x, shadowHalfOverSigma.y,
                                        shadowCornerOverSigma.x, shadowCornerOverSigma.y);
        expected *= RefMaskCoverage(
            RefRoundRectSDF(mx, my, maskHalfSize.x, maskHalfSize.y, maskCornerRadius));
        auto got = static_cast<float>(pixels[(static_cast<size_t>(y) * static_cast<size_t>(width) +
                                              static_cast<size_t>(x)) *
                                             4]) /
                   255.0f;
        worst = std::max(worst, std::abs(got - expected));
      }
    }
    EXPECT_LT(worst * 255.0f, 2.0f);
  }
}

TGFX_TEST(ShapeBlurTest, DegenerateInputs) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  const int width = 40;
  const int height = 40;
  const auto centerX = static_cast<float>(width) * 0.5f;
  const auto centerY = static_cast<float>(height) * 0.5f;

  struct Case {
    const char* name;
    Point halfOverSigma;
    Point cornerOverSigma;
  };
  // Degenerate geometry that the shader must survive: sharp corners (corner = 0, exercising the
  // max(cornerAlong, 1e-6) guard), a fully rounded shape (corner == half, the oval extreme), a
  // shape far smaller than the kernel (interior-skip test goes negative), and an extreme aspect
  // ratio.
  const Case cases[] = {
      {"sharp corner", {3.0f, 2.0f}, {0.0f, 0.0f}},
      {"full round", {2.0f, 2.0f}, {2.0f, 2.0f}},
      {"tiny vs kernel", {0.3f, 0.2f}, {0.1f, 0.1f}},
      {"extreme aspect", {8.0f, 0.4f}, {0.2f, 0.2f}},
  };

  for (const auto& c : cases) {
    auto uvMatrix = Matrix::MakeTrans(-centerX, -centerY);
    uvMatrix.postScale(1.0f / 4.0f, 1.0f / 4.0f);
    auto fp = RRectBlurFragmentProcessor::Make(context->drawingAllocator(), c.halfOverSigma,
                                               c.cornerOverSigma, Color::White().premultiply(),
                                               &uvMatrix);
    auto pixels = RenderFP(context, width, height, std::move(fp));
    ASSERT_FALSE(pixels.empty()) << c.name;
    // NaN or out-of-range coverage would surface as a stuck channel; check every pixel is a
    // plausible premultiplied white (r == g == b == a).
    bool allConsistent = true;
    int nonZero = 0;
    for (size_t i = 0; i < pixels.size(); i += 4) {
      auto r = pixels[i];
      auto a = pixels[i + 3];
      if (r != a) {
        allConsistent = false;
      }
      if (a != 0) {
        nonZero++;
      }
    }
    EXPECT_TRUE(allConsistent) << c.name;
    EXPECT_GT(nonZero, 0) << c.name;
  }
}

// Test code is compiled with private access, so isEqual can be reached through a subclass alias.
static bool TestShaderEqual(const Shader* a, const Shader* b) {
  struct Access : Shader {
    using Shader::isEqual;
  };
  return static_cast<const Access*>(a)->isEqual(b);
}

// Returns the alpha of one pixel in a tightly packed RGBA buffer.
static uint8_t AlphaAt(const std::vector<uint8_t>& pixels, int width, int x, int y) {
  return pixels[(static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4 +
                3];
}

// Returns the bounding box of pixels whose alpha exceeds the given threshold.
static Rect AlphaBounds(const std::vector<uint8_t>& pixels, int width, int height,
                        uint8_t threshold) {
  auto left = static_cast<float>(width);
  auto top = static_cast<float>(height);
  auto right = 0.0f;
  auto bottom = 0.0f;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      auto a =
          pixels[(static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) *
                     4 +
                 3];
      if (a <= threshold) {
        continue;
      }
      left = std::min(left, static_cast<float>(x));
      top = std::min(top, static_cast<float>(y));
      right = std::max(right, static_cast<float>(x) + 1.0f);
      bottom = std::max(bottom, static_cast<float>(y) + 1.0f);
    }
  }
  return Rect::MakeLTRB(left, top, right, bottom);
}

TGFX_TEST(ShapeBlurTest, ShaderThroughCanvas) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  const int width = 200;
  const int height = 200;
  const float sigma = 4.0f;
  // A 60x40 shape centered at (100, 100) in the space the geometry is expressed in.
  const auto shapeRect = Rect::MakeXYWH(70.0f, 80.0f, 60.0f, 40.0f);
  const auto rRect = RRect::MakeRectXY(shapeRect, 8.0f, 8.0f);

  // Case A: localScale = 1, no canvas matrix. The blurred shape must stay centered on shapeRect
  // and extend by the kernel support (2 sigma) on each side.
  {
    auto surface = Surface::Make(context, width, height);
    auto canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    auto shader = ShapeBlurShader::Make(rRect, sigma, sigma, Color::White(), 1.0f);
    ASSERT_TRUE(shader != nullptr);
    Paint paint;
    paint.setShader(shader);
    canvas->drawRect(shapeRect.makeOutset(2.0f * sigma, 2.0f * sigma), paint);
    context->flushAndSubmit();
    auto info = ImageInfo::Make(width, height, ColorType::RGBA_8888, AlphaType::Premultiplied);
    std::vector<uint8_t> pixels(static_cast<size_t>(info.byteSize()));
    ASSERT_TRUE(surface->readPixels(info, pixels.data()));
    auto bounds = AlphaBounds(pixels, width, height, 2);
    // Centered on the shape, and never reaching beyond the 2-sigma support.
    EXPECT_NEAR((bounds.left + bounds.right) * 0.5f, shapeRect.centerX(), 1.5f);
    EXPECT_NEAR((bounds.top + bounds.bottom) * 0.5f, shapeRect.centerY(), 1.5f);
    EXPECT_GE(bounds.left, shapeRect.left - 2.0f * sigma - 1.0f);
    EXPECT_LE(bounds.right, shapeRect.right + 2.0f * sigma + 1.0f);
  }

  // Case B: the canvas carries a 2x scale while the geometry and sigma are expressed in the
  // scaled (content-pixel-like) space, which is the layer-style situation. localScale must
  // compensate so the shadow still lands on the shape as drawn. The shape is placed near the
  // origin so that the scaled result stays inside the surface.
  {
    const float canvasScale = 2.0f;
    const auto localRect = Rect::MakeXYWH(20.0f, 15.0f, 40.0f, 30.0f);
    auto surface = Surface::Make(context, width, height);
    auto canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    // Geometry and sigma live in the scaled space: local coordinates times canvasScale.
    const auto scaledRect =
        Rect::MakeXYWH(localRect.left * canvasScale, localRect.top * canvasScale,
                       localRect.width() * canvasScale, localRect.height() * canvasScale);
    const auto scaledRRect = RRect::MakeRectXY(scaledRect, 8.0f * canvasScale, 8.0f * canvasScale);
    auto shader = ShapeBlurShader::Make(scaledRRect, sigma * canvasScale, sigma * canvasScale,
                                        Color::White(), canvasScale);
    ASSERT_TRUE(shader != nullptr);
    Paint paint;
    paint.setShader(shader);
    canvas->save();
    canvas->scale(canvasScale, canvasScale);
    // drawRect is in canvas local space; the shadow reaches 2 sigma beyond the shape there.
    canvas->drawRect(localRect.makeOutset(2.0f * sigma, 2.0f * sigma), paint);
    canvas->restore();
    context->flushAndSubmit();
    auto info = ImageInfo::Make(width, height, ColorType::RGBA_8888, AlphaType::Premultiplied);
    std::vector<uint8_t> pixels(static_cast<size_t>(info.byteSize()));
    ASSERT_TRUE(surface->readPixels(info, pixels.data()));
    auto bounds = AlphaBounds(pixels, width, height, 2);
    // The shadow must be centered on the shape in device space, i.e. the scaled rect.
    EXPECT_NEAR((bounds.left + bounds.right) * 0.5f, scaledRect.centerX(), 1.5f);
    EXPECT_NEAR((bounds.top + bounds.bottom) * 0.5f, scaledRect.centerY(), 1.5f);
    // And its extent must match 2 sigma in the scaled space, not the local one.
    EXPECT_NEAR(bounds.left, scaledRect.left - 2.0f * sigma * canvasScale, 2.0f);
    EXPECT_NEAR(bounds.right, scaledRect.right + 2.0f * sigma * canvasScale, 2.0f);
  }

  // Case C: isEqual must distinguish differing geometry, otherwise CompareBrush would merge
  // shadows that need different uniforms.
  {
    auto a = ShapeBlurShader::Make(rRect, sigma, sigma, Color::White(), 1.0f);
    auto same = ShapeBlurShader::Make(rRect, sigma, sigma, Color::White(), 1.0f);
    auto otherSigma = ShapeBlurShader::Make(rRect, sigma * 2.0f, sigma, Color::White(), 1.0f);
    auto otherRect = ShapeBlurShader::Make(RRect::MakeRectXY(shapeRect.makeOutset(1, 1), 8, 8),
                                           sigma, sigma, Color::White(), 1.0f);
    auto otherColor = ShapeBlurShader::Make(rRect, sigma, sigma, Color::Red(), 1.0f);
    ASSERT_TRUE(a && same && otherSigma && otherRect && otherColor);
    EXPECT_TRUE(TestShaderEqual(a.get(), same.get()));
    EXPECT_FALSE(TestShaderEqual(a.get(), otherSigma.get()));
    EXPECT_FALSE(TestShaderEqual(a.get(), otherRect.get()));
    EXPECT_FALSE(TestShaderEqual(a.get(), otherColor.get()));
  }
}

TGFX_TEST(ShapeBlurTest, PaintAlphaReachesShader) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  const int width = 120;
  const int height = 120;
  const float sigma = 4.0f;
  const auto shapeRect = Rect::MakeXYWH(30.0f, 40.0f, 60.0f, 40.0f);
  const auto rRect = RRect::MakeRectXY(shapeRect, 8.0f, 8.0f);
  const auto info = ImageInfo::Make(width, height, ColorType::RGBA_8888, AlphaType::Premultiplied);
  // Well inside the shape, where coverage is 1 and the alpha applied by the paint is the only factor.
  const int probeX = static_cast<int>(shapeRect.centerX());
  const int probeY = static_cast<int>(shapeRect.centerY());

  uint8_t alphaValues[2] = {};
  const float paintAlphas[2] = {1.0f, 0.5f};
  for (int i = 0; i < 2; i++) {
    auto surface = Surface::Make(context, width, height);
    auto canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    auto shader = ShapeBlurShader::Make(rRect, sigma, sigma, Color::White(), 1.0f);
    ASSERT_TRUE(shader != nullptr);
    Paint paint = {};
    paint.setShader(shader);
    paint.setAlpha(paintAlphas[i]);
    canvas->drawRect(shapeRect.makeOutset(2.0f * sigma, 2.0f * sigma), paint);
    context->flushAndSubmit();
    std::vector<uint8_t> pixels(static_cast<size_t>(info.byteSize()));
    ASSERT_TRUE(surface->readPixels(info, pixels.data()));
    alphaValues[i] = AlphaAt(pixels, width, probeX, probeY);
  }
  EXPECT_NEAR(alphaValues[0], 255, 1);
  EXPECT_NEAR(alphaValues[1], 128, 2);
}

TGFX_TEST(ShapeBlurTest, KnockoutMaskIgnoresShadowOffset) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  // A layer whose shadow is offset down-right and hidden behind the layer itself. The knockout must
  // follow the layer, not the shadow: if it were dragged along by the offset it would land on the
  // shadow and erase its interior, leaving only the falloff outside the shape.
  const int width = 200;
  const int height = 200;
  const float offset = 8.0f;
  auto surface = Surface::Make(context, width, height);
  auto displayList = std::make_unique<DisplayList>();
  auto layer = SolidLayer::Make();
  layer->setColor(Color::Blue());
  layer->setWidth(100);
  layer->setHeight(100);
  layer->setMatrix(Matrix::MakeTrans(50, 50));
  auto shadow = DropShadowStyle::Make(offset, offset, 5, 5, Color::Black(), false);
  shadow->setSpread(2);
  layer->setLayerStyles({shadow});
  displayList->root()->addChild(layer);
  displayList->render(surface.get());
  context->flushAndSubmit();

  auto info = ImageInfo::Make(width, height, ColorType::RGBA_8888, AlphaType::Premultiplied);
  std::vector<uint8_t> pixels(static_cast<size_t>(info.byteSize()));
  ASSERT_TRUE(surface->readPixels(info, pixels.data()));

  // The layer covers x 50..150. Just right of its edge the shadow is at its darkest, since the
  // shadow shape reaches x 160 while the knockout stops at 150.
  EXPECT_GT(AlphaAt(pixels, width, 153, 100), 200);
  // Deep inside the layer the shadow is knocked out, so only the layer itself contributes; sampling
  // alpha cannot tell them apart, so compare against the shadow-free interior instead.
  EXPECT_EQ(AlphaAt(pixels, width, 100, 100), 255);
  // Beyond the shadow's own extent everything is transparent again.
  EXPECT_LT(AlphaAt(pixels, width, 185, 100), 10);
}

// Builds a minimal LayerStyleInput carrying just the contour shape, which is all MakeAnalyticShape
// consumes. The content image is irrelevant to the analytic path.
static LayerStyleInput MakeShapeInput(const std::optional<StyledShape>& shape, float contentScale) {
  LayerStyleInput input = {};
  input.contentScale = contentScale;
  input.extraSources.push_back(std::make_shared<ContourInputSource>(nullptr, Point::Zero(), shape));
  return input;
}

static std::shared_ptr<Shape> ShapeFromRRect(const RRect& rRect) {
  Path path = {};
  path.addRRect(rRect);
  return Shape::MakeFrom(path);
}

TGFX_TEST(ShapeBlurTest, AnalyticShapeDispatch) {
  const auto rect = Rect::MakeXYWH(10.0f, 20.0f, 60.0f, 40.0f);

  // Accepted: plain rect, uniformly rounded rect and oval, all fill-only and exact.
  {
    auto plain = StyledShape::Make(ShapeFromRRect(RRect::MakeRect(rect)), StyledShapeType::Fill, 0,
                                   StrokeAlign::Center);
    auto result = SpreadUtils::MakeAnalyticShape(MakeShapeInput(plain, 1.0f), 0.0f);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->rRect.isRect());
    EXPECT_EQ(result->rRect.rect(), rect);
  }
  {
    auto rounded = StyledShape::Make(ShapeFromRRect(RRect::MakeRectXY(rect, 8.0f, 8.0f)),
                                     StyledShapeType::Fill, 0, StrokeAlign::Center);
    auto result = SpreadUtils::MakeAnalyticShape(MakeShapeInput(rounded, 1.0f), 0.0f);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->rRect.isComplex());
    EXPECT_NEAR(result->rRect.radii()[0].x, 8.0f, 0.01f);
  }
  {
    auto oval = StyledShape::Make(ShapeFromRRect(RRect::MakeOval(rect)), StyledShapeType::Fill, 0,
                                  StrokeAlign::Center);
    auto result = SpreadUtils::MakeAnalyticShape(MakeShapeInput(oval, 1.0f), 0.0f);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->rRect.isOval());
  }

  // contentScale folds into the geometry.
  {
    auto plain = StyledShape::Make(ShapeFromRRect(RRect::MakeRectXY(rect, 8.0f, 8.0f)),
                                   StyledShapeType::Fill, 0, StrokeAlign::Center);
    auto result = SpreadUtils::MakeAnalyticShape(MakeShapeInput(plain, 2.0f), 0.0f);
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->rRect.rect().width(), rect.width() * 2.0f, 0.01f);
    EXPECT_NEAR(result->rRect.radii()[0].x, 16.0f, 0.01f);
  }

  // Spread scales with contentScale too, matching how the raster path applies it.
  {
    auto plain = StyledShape::Make(ShapeFromRRect(RRect::MakeRect(rect)), StyledShapeType::Fill, 0,
                                   StrokeAlign::Center);
    auto result = SpreadUtils::MakeAnalyticShape(MakeShapeInput(plain, 2.0f), 5.0f);
    ASSERT_TRUE(result.has_value());
    // Outset by spread * scale on each side.
    EXPECT_NEAR(result->rRect.rect().width(), rect.width() * 2.0f + 2.0f * 10.0f, 0.01f);
  }

  // Rejected cases, each for a distinct documented reason.
  {
    // A bounding-box stand-in must not be substituted for the real outline.
    auto approx = StyledShape::MakeApproximate(ShapeFromRRect(RRect::MakeRect(rect)),
                                               StyledShapeType::Fill, 0, StrokeAlign::Center);
    EXPECT_FALSE(SpreadUtils::MakeAnalyticShape(MakeShapeInput(approx, 1.0f), 0.0f).has_value());
  }
  {
    // Stroked outlines are not analytic.
    auto stroked = StyledShape::Make(ShapeFromRRect(RRect::MakeRect(rect)), StyledShapeType::Stroke,
                                     4.0f, StrokeAlign::Center);
    EXPECT_FALSE(SpreadUtils::MakeAnalyticShape(MakeShapeInput(stroked, 1.0f), 0.0f).has_value());
    auto both = StyledShape::Make(ShapeFromRRect(RRect::MakeRect(rect)),
                                  StyledShapeType::FillStroke, 4.0f, StrokeAlign::Center);
    EXPECT_FALSE(SpreadUtils::MakeAnalyticShape(MakeShapeInput(both, 1.0f), 0.0f).has_value());
  }
  {
    // Per-corner radii need a segmented row-span formula.
    std::array<Point, 4> radii = {Point{4, 4}, Point{10, 10}, Point{4, 4}, Point{10, 10}};
    auto complexShape = StyledShape::Make(ShapeFromRRect(RRect::MakeRectRadii(rect, radii)),
                                          StyledShapeType::Fill, 0, StrokeAlign::Center);
    EXPECT_FALSE(
        SpreadUtils::MakeAnalyticShape(MakeShapeInput(complexShape, 1.0f), 0.0f).has_value());
  }
  {
    // Arbitrary paths are not analytic.
    Path star = {};
    star.moveTo(50, 0);
    star.lineTo(60, 35);
    star.lineTo(98, 35);
    star.lineTo(68, 57);
    star.close();
    auto pathShape =
        StyledShape::Make(Shape::MakeFrom(star), StyledShapeType::Fill, 0, StrokeAlign::Center);
    EXPECT_FALSE(SpreadUtils::MakeAnalyticShape(MakeShapeInput(pathShape, 1.0f), 0.0f).has_value());
  }
  {
    // Rotation breaks axis alignment.
    auto rotated =
        Shape::ApplyMatrix(ShapeFromRRect(RRect::MakeRect(rect)), Matrix::MakeRotate(30.0f));
    auto rotatedShape = StyledShape::Make(rotated, StyledShapeType::Fill, 0, StrokeAlign::Center);
    EXPECT_FALSE(
        SpreadUtils::MakeAnalyticShape(MakeShapeInput(rotatedShape, 1.0f), 0.0f).has_value());
  }
  {
    // Non-uniform scale makes spread anisotropic, which MakeSpreadRRect cannot express. Without
    // spread the same shape is fine.
    auto skewed = Shape::ApplyMatrix(ShapeFromRRect(RRect::MakeRectXY(rect, 8.0f, 8.0f)),
                                     Matrix::MakeScale(2.0f, 1.0f));
    auto skewedShape = StyledShape::Make(skewed, StyledShapeType::Fill, 0, StrokeAlign::Center);
    EXPECT_TRUE(
        SpreadUtils::MakeAnalyticShape(MakeShapeInput(skewedShape, 1.0f), 0.0f).has_value());
    EXPECT_FALSE(
        SpreadUtils::MakeAnalyticShape(MakeShapeInput(skewedShape, 1.0f), 5.0f).has_value());
  }
  {
    // Negative spread that consumes the shape.
    auto plain = StyledShape::Make(ShapeFromRRect(RRect::MakeRect(rect)), StyledShapeType::Fill, 0,
                                   StrokeAlign::Center);
    EXPECT_FALSE(SpreadUtils::MakeAnalyticShape(MakeShapeInput(plain, 1.0f), -30.0f).has_value());
  }
  {
    // No contour shape at all.
    EXPECT_FALSE(
        SpreadUtils::MakeAnalyticShape(MakeShapeInput(std::nullopt, 1.0f), 0.0f).has_value());
  }
}

// Adds one shape with a shadow to the display list, positioned so its cell starts at (x, y).
static void AddShadowCell(Layer* root, float x, float y, const RRect& rRect, float sigmaX,
                          float sigmaY, bool inner) {
  auto layer = ShapeLayer::Make();
  Path path = {};
  path.addRRect(rRect);
  layer->setPath(path);
  layer->setFillStyle(ShapeStyle::Make(Color::Blue()));
  layer->setMatrix(Matrix::MakeTrans(x, y));
  std::shared_ptr<LayerStyle> style = nullptr;
  if (inner) {
    auto shadow = InnerShadowStyle::Make(0, 0, sigmaX, sigmaY, Color::Black());
    shadow->setSpread(1);
    style = shadow;
  } else {
    auto shadow = DropShadowStyle::Make(0, 0, sigmaX, sigmaY, Color::Black(), true);
    shadow->setSpread(1);
    style = shadow;
  }
  layer->setLayerStyles({style});
  root->addChild(layer);
}

// Covers the analytic branches the LayerTest shadow cases do not reach: a circle (corner radius at
// its maximum), anisotropic sigma (elliptical normalized corners) and a sigma far above the filter
// path's MAX_BLUR_SIGMA. All carry a spread, without which the fast path is not taken.
TGFX_TEST(ShapeBlurTest, AnalyticShadowVariants) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto surface = Surface::Make(context, 674, 485);
  auto displayList = std::make_unique<DisplayList>();
  auto* root = displayList->root();

  // Row 1: a circle, drop and inner. sigma = 10 is the filter path's limit, so the two paths remain
  // comparable here.
  const auto circle = RRect::MakeOval(Rect::MakeWH(120, 120));
  AddShadowCell(root, 69, 69, circle, 10, 10, false);
  AddShadowCell(root, 409, 69, circle, 10, 10, true);

  // Row 2: anisotropic sigma on a uniformly rounded rect, and a sigma well past MAX_BLUR_SIGMA.
  const auto rounded = RRect::MakeRectXY(Rect::MakeWH(160, 100), 30, 30);
  AddShadowCell(root, 99, 279, rounded, 12, 4, false);
  AddShadowCell(root, 409, 279, rounded, 30, 30, false);

  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "ShapeBlurTest/AnalyticShadowVariants"));
}

// Draws a shape the fast path accepts next to one it rejects, so both code paths run in the same
// frame. Only placement and rough strength are checked; the shapes differ, so their shadows are not
// expected to match each other.
TGFX_TEST(ShapeBlurTest, FastAndFilterPathCoexist) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  const int width = 600;
  const int height = 260;
  auto surface = Surface::Make(context, width, height);
  auto displayList = std::make_unique<DisplayList>();

  // Left: a rounded solid layer with a spread, which the fast path accepts.
  auto fast = SolidLayer::Make();
  fast->setColor(Color::Red());
  fast->setWidth(160);
  fast->setHeight(100);
  fast->setRadiusX(24);
  fast->setRadiusY(24);
  fast->setMatrix(Matrix::MakeTrans(80, 80));
  auto fastShadow = DropShadowStyle::Make(20, 20, 8, 8, Color::Black(), true);
  fastShadow->setSpread(4);
  fast->setLayerStyles({fastShadow});
  displayList->root()->addChild(fast);

  // Right: a star path, which the fast path rejects, so its shadow comes from the filter path.
  auto slow = ShapeLayer::Make();
  Path star = {};
  star.moveTo(80, 0);
  star.lineTo(104, 60);
  star.lineTo(160, 60);
  star.lineTo(116, 96);
  star.lineTo(136, 100);
  star.lineTo(80, 76);
  star.lineTo(24, 100);
  star.lineTo(44, 96);
  star.lineTo(0, 60);
  star.lineTo(56, 60);
  star.close();
  slow->setPath(star);
  slow->setFillStyle(ShapeStyle::Make(Color::Red()));
  slow->setMatrix(Matrix::MakeTrans(340, 80));
  auto slowShadow = DropShadowStyle::Make(20, 20, 8, 8, Color::Black(), true);
  slowShadow->setSpread(4);
  slow->setLayerStyles({slowShadow});
  displayList->root()->addChild(slow);

  displayList->render(surface.get());
  context->flushAndSubmit();

  auto info = ImageInfo::Make(width, height, ColorType::RGBA_8888, AlphaType::Premultiplied);
  std::vector<uint8_t> pixels(static_cast<size_t>(info.byteSize()));
  ASSERT_TRUE(surface->readPixels(info, pixels.data()));

  // Shape occupies x 80..240 (left) and 340..500 (right), y 80..180; shadow is offset by +20.
  EXPECT_GT(AlphaAt(pixels, width, 160, 195), 20);
  EXPECT_GT(AlphaAt(pixels, width, 420, 185), 20);
  EXPECT_LT(AlphaAt(pixels, width, 160, 250), 10);
}

}  // namespace tgfx

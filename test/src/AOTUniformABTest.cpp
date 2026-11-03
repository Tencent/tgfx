/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// The S4 upload-unification gate (plan S1.2): every scene here renders on both upload routes —
// the runtime writers with the bundle unloaded, the AOT writers with it loaded — and each route's
// assembled uniform trace is pinned byte-for-byte against the golden under
// test/baseline/uniform/ (see UniformABCompare.h for why the routes are pinned separately rather
// than against each other). The scenes cover the eight feature modules S4.4 migrates one commit
// at a time — texture source, tiled source, gradient, pointwise op, device mask, analytic clip,
// blur kernel, Porter-Duff XP — plus the alpha-only source and target forms. Each S4.4 commit
// extends this file with a shape exercising the module it migrates; a drifted value, offset
// order or binding fails here before any pixel test can see it. Regenerate a golden by running
// once with TGFX_ACCEPT_BASELINE set.

#include <array>
#include <memory>
#include "gtest/gtest.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "utils/TestUtils.h"
#include "utils/UniformABCompare.h"

namespace tgfx {
namespace {

std::shared_ptr<Image> MakeCheckerImage() {
  Bitmap bitmap = {};
  if (!bitmap.allocPixels(32, 32)) {
    return nullptr;
  }
  auto* pixels = static_cast<uint8_t*>(bitmap.lockPixels());
  if (pixels == nullptr) {
    return nullptr;
  }
  auto rowBytes = bitmap.rowBytes();
  for (int y = 0; y < 32; ++y) {
    auto* row = pixels + static_cast<size_t>(y) * rowBytes;
    for (int x = 0; x < 32; ++x) {
      bool checker = ((x / 8) + (y / 8)) % 2 == 0;
      row[x * 4 + 0] = static_cast<uint8_t>(checker ? 230 : 40);
      row[x * 4 + 1] = static_cast<uint8_t>(x * 8);
      row[x * 4 + 2] = static_cast<uint8_t>(y * 8);
      row[x * 4 + 3] = 255;
    }
  }
  bitmap.unlockPixels();
  return Image::MakeFrom(bitmap);
}

std::shared_ptr<Image> MakeAlphaOnlyImage() {
  Bitmap bitmap = {};
  if (!bitmap.allocPixels(32, 32, true)) {
    return nullptr;
  }
  auto* pixels = static_cast<uint8_t*>(bitmap.lockPixels());
  if (pixels == nullptr) {
    return nullptr;
  }
  auto rowBytes = bitmap.rowBytes();
  for (int y = 0; y < 32; ++y) {
    auto* row = pixels + static_cast<size_t>(y) * rowBytes;
    for (int x = 0; x < 32; ++x) {
      row[x] = static_cast<uint8_t>((x + y) * 4);
    }
  }
  bitmap.unlockPixels();
  return Image::MakeFrom(bitmap);
}

}  // namespace

// SolidColorFill's paint color plus an analytic AA clip: the SolidColorFill and AnalyticClip
// modules and the quad-per-edge GP fields.
TGFX_TEST(AOTUniformABTest, SolidFillAndAnalyticClip) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto diff = PinUniformTrace(context, "SolidFillAndAnalyticClip", [&] {
    auto surface = Surface::Make(context, 64, 64);
    if (surface == nullptr) {
      return;
    }
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    Paint paint = {};
    paint.setColor(Color::FromRGBA(200, 40, 40, 255));
    canvas->drawRect(Rect::MakeXYWH(2, 2, 60, 22), paint);
    canvas->save();
    canvas->clipRect(Rect::MakeXYWH(4, 28, 40, 30), true);
    paint.setColor(Color::FromRGBA(20, 120, 220, 200));
    canvas->drawRect(Rect::MakeXYWH(2, 30, 60, 26), paint);
    canvas->restore();
  });
  EXPECT_TRUE(diff.empty()) << diff;
}

// A clamped image fill, a subsetted fill, and an alpha-only mask fill: the TextureSource module's
// plain, subset and alpha-only forms.
TGFX_TEST(AOTUniformABTest, ImageSubsetAndAlphaOnlyFill) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto image = MakeCheckerImage();
  auto alpha = MakeAlphaOnlyImage();
  ASSERT_NE(image, nullptr);
  ASSERT_NE(alpha, nullptr);
  auto subset = image->makeSubset(Rect::MakeXYWH(4, 4, 16, 16));
  auto diff = PinUniformTrace(context, "ImageSubsetAndAlphaOnlyFill", [&] {
    auto surface = Surface::Make(context, 64, 64);
    if (surface == nullptr) {
      return;
    }
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    Paint paint = {};
    paint.setShader(Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp));
    canvas->drawRect(Rect::MakeXYWH(2, 2, 60, 18), paint);
    if (subset != nullptr) {
      paint.setShader(Shader::MakeImageShader(subset, TileMode::Clamp, TileMode::Clamp));
      canvas->drawRect(Rect::MakeXYWH(2, 22, 60, 18), paint);
    }
    paint.setShader(Shader::MakeImageShader(alpha, TileMode::Clamp, TileMode::Clamp));
    paint.setColor(Color::FromRGBA(90, 60, 200, 255));
    canvas->drawRect(Rect::MakeXYWH(2, 42, 60, 18), paint);
  });
  EXPECT_TRUE(diff.empty()) << diff;
}

// Repeat/Mirror and Decal/Repeat fills: the TiledSource module's tile-mode forms.
TGFX_TEST(AOTUniformABTest, TiledImageFill) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto image = MakeCheckerImage();
  ASSERT_NE(image, nullptr);
  auto diff = PinUniformTrace(context, "TiledImageFill", [&] {
    auto surface = Surface::Make(context, 64, 64);
    if (surface == nullptr) {
      return;
    }
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    Paint paint = {};
    paint.setShader(Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Mirror));
    canvas->drawRect(Rect::MakeXYWH(2, 2, 60, 26), paint);
    paint.setShader(Shader::MakeImageShader(image, TileMode::Decal, TileMode::Repeat));
    canvas->drawRect(Rect::MakeXYWH(2, 34, 60, 26), paint);
  });
  EXPECT_TRUE(diff.empty()) << diff;
}

// A two-stop and a positioned three-stop fill: the Gradient module's single-interval and
// multi-interval colorizer forms.
TGFX_TEST(AOTUniformABTest, GradientFills) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto diff = PinUniformTrace(context, "GradientFills", [&] {
    auto surface = Surface::Make(context, 64, 64);
    if (surface == nullptr) {
      return;
    }
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    Paint paint = {};
    paint.setShader(
        Shader::MakeLinearGradient(Point{0, 0}, Point{64, 0}, {Color::Blue(), Color::Green()}, {}));
    canvas->drawRect(Rect::MakeXYWH(2, 2, 60, 26), paint);
    paint.setShader(Shader::MakeLinearGradient(
        Point{0, 0}, Point{64, 0}, {Color::Red(), Color::FromRGBA(255, 220, 0, 255), Color::Blue()},
        {0.f, 0.4f, 1.f}));
    canvas->drawRect(Rect::MakeXYWH(2, 34, 60, 26), paint);
  });
  EXPECT_TRUE(diff.empty()) << diff;
}

// Blend and matrix color filters over a filled image, then Multiply and DstIn blend modes: the
// PointwiseOp module's operators and the PorterDuffXP module (DstIn also binds the dst texture).
TGFX_TEST(AOTUniformABTest, ColorFilterAndBlendModes) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto image = MakeCheckerImage();
  ASSERT_NE(image, nullptr);
  auto diff = PinUniformTrace(context, "ColorFilterAndBlendModes", [&] {
    auto surface = Surface::Make(context, 64, 64);
    if (surface == nullptr) {
      return;
    }
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    Paint paint = {};
    paint.setShader(Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp));
    canvas->drawRect(Rect::MakeXYWH(2, 2, 60, 14), paint);
    paint.setShader(nullptr);
    paint.setColorFilter(ColorFilter::Blend(Color::Red(), BlendMode::Multiply));
    paint.setColor(Color::FromRGBA(255, 220, 120, 255));
    canvas->drawRect(Rect::MakeXYWH(2, 18, 60, 14), paint);
    paint.setColorFilter(ColorFilter::Matrix(std::array<float, 20>{
        0.9f, 0, 0, 0, 0, 0, 0.8f, 0, 0, 0, 0, 0, 0.7f, 0, 0, 0, 0, 0, 1, 0}));
    canvas->drawRect(Rect::MakeXYWH(2, 34, 60, 14), paint);
    paint.setColorFilter(nullptr);
    paint.setBlendMode(BlendMode::Multiply);
    paint.setColor(Color::FromRGBA(120, 200, 90, 255));
    canvas->drawRect(Rect::MakeXYWH(2, 50, 60, 12), paint);
    paint.setBlendMode(BlendMode::DstIn);
    paint.setColor(Color::FromRGBA(255, 255, 255, 180));
    canvas->drawRect(Rect::MakeXYWH(10, 6, 40, 50), paint);
  });
  EXPECT_TRUE(diff.empty()) << diff;
}

// A blurred image fill: the offscreen blur passes (the BlurKernel module over a tiled source)
// plus the composite fill.
TGFX_TEST(AOTUniformABTest, BlurImageFill) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto image = MakeCheckerImage();
  ASSERT_NE(image, nullptr);
  Point filterOffset = {};
  auto blurred = image->makeWithFilter(ImageFilter::Blur(5, 5, TileMode::Repeat), &filterOffset);
  ASSERT_NE(blurred, nullptr);
  auto diff = PinUniformTrace(context, "BlurImageFill", [&] {
    auto surface = Surface::Make(context, 64, 64);
    if (surface == nullptr) {
      return;
    }
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    Paint paint = {};
    paint.setShader(Shader::MakeImageShader(blurred, TileMode::Clamp, TileMode::Clamp));
    canvas->drawRect(Rect::MakeXYWH(2, 2, 60, 60), paint);
  });
  EXPECT_TRUE(diff.empty()) << diff;
}

// A non-analytic path clip rasterizes into a device-space mask texture, and the per-corner-radii
// rrect routes the draw through the complex-ellipse GP: the DeviceMask module's served form.
TGFX_TEST(AOTUniformABTest, ComplexClipDeviceMask) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto diff = PinUniformTrace(context, "ComplexClipDeviceMask", [&] {
    auto surface = Surface::Make(context, 160, 160);
    if (surface == nullptr) {
      return;
    }
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    std::array<Point, 4> radii = {Point::Make(28, 14), Point::Make(10, 24), Point::Make(20, 8),
                                  Point::Make(6, 12)};
    RRect shape = {};
    shape.setRectRadii(Rect::MakeXYWH(18, 18, 120, 120), radii);
    Paint paint = {};
    paint.setColor(Color::FromRGBA(200, 90, 60, 255));
    Path clipPath = {};
    clipPath.moveTo(12, 20);
    clipPath.lineTo(148, 34);
    clipPath.lineTo(96, 152);
    clipPath.close();
    canvas->clipPath(clipPath);
    canvas->drawRRect(shape, paint);
  });
  EXPECT_TRUE(diff.empty()) << diff;
}

// A blend of two image shaders in one draw: two TextureSource leaves in one program, the
// ordinal-addressed instances (Subset / Subset_1, ...) the structural scheme and the layout's
// (base, stride) form must agree on.
TGFX_TEST(AOTUniformABTest, BlendTwoImageFills) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto imageA = MakeCheckerImage();
  auto imageB = MakeCheckerImage();
  ASSERT_NE(imageA, nullptr);
  ASSERT_NE(imageB, nullptr);
  auto diff = PinUniformTrace(context, "BlendTwoImageFills", [&] {
    auto surface = Surface::Make(context, 64, 64);
    if (surface == nullptr) {
      return;
    }
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    Paint paint = {};
    auto dst = Shader::MakeImageShader(imageA, TileMode::Clamp, TileMode::Clamp);
    auto src = Shader::MakeImageShader(imageB, TileMode::Repeat, TileMode::Clamp);
    paint.setShader(Shader::MakeBlend(BlendMode::Multiply, dst, src));
    canvas->drawRect(Rect::MakeXYWH(2, 2, 60, 26), paint);
    paint.setShader(Shader::MakeBlend(BlendMode::SrcOver, dst, src));
    canvas->drawRect(Rect::MakeXYWH(2, 34, 60, 26), paint);
  });
  EXPECT_TRUE(diff.empty()) << diff;
}

// A fill onto an alpha-only target: the OutputAlphaSwizzle engine default takes its non-identity
// value on the precompiled route.
TGFX_TEST(AOTUniformABTest, AlphaOnlyTargetFill) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto image = MakeCheckerImage();
  ASSERT_NE(image, nullptr);
  auto diff = PinUniformTrace(context, "AlphaOnlyTargetFill", [&] {
    auto surface = Surface::Make(context, 64, 64, true);
    if (surface == nullptr) {
      return;
    }
    auto* canvas = surface->getCanvas();
    canvas->clear(Color::Transparent());
    Paint paint = {};
    paint.setShader(Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp));
    canvas->drawRect(Rect::MakeXYWH(2, 2, 60, 60), paint);
  });
  EXPECT_TRUE(diff.empty()) << diff;
}

}  // namespace tgfx

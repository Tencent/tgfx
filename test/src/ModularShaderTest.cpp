/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "gtest/gtest.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "utils/TestUtils.h"

namespace tgfx {

/**
 * Renders a solid color rectangle using the modular shader path and verifies the result.
 * This exercises: QuadPerEdgeAAGeometryProcessor + ConstColorProcessor + PorterDuffXferProcessor.
 */
TGFX_TEST(ModularShaderTest, SolidColorFill) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 100, 100);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  Paint paint;
  paint.setColor(Color::FromRGBA(204, 51, 102));
  canvas->drawRect(Rect::MakeXYWH(10, 10, 80, 80), paint);

  Bitmap bitmap;
  bitmap.allocPixels(100, 100);
  Pixmap pixmap(bitmap);
  auto result = surface->readPixels(pixmap.info(), pixmap.writablePixels());
  ASSERT_TRUE(result);

  // Verify the center pixel has the expected color (non-zero).
  auto centerPixel = pixmap.getColor(50, 50);
  EXPECT_GT(centerPixel.red, 0.0f);
  EXPECT_GT(centerPixel.alpha, 0.0f);

  // Verify a corner pixel is transparent (background).
  auto cornerPixel = pixmap.getColor(0, 0);
  EXPECT_FLOAT_EQ(cornerPixel.alpha, 0.0f);
}

/**
 * Renders a linear gradient rectangle using the modular shader path.
 * This exercises: QuadPerEdgeAAGeometryProcessor + ClampedGradientEffect (container) +
 *   LinearGradientLayout + SingleIntervalGradientColorizer + PorterDuffXferProcessor.
 */
TGFX_TEST(ModularShaderTest, LinearGradient) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 100, 100);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  Paint paint;
  auto shader = Shader::MakeLinearGradient(Point::Make(10, 10), Point::Make(90, 90),
                                           {Color::Red(), Color::Blue()}, {});
  paint.setShader(shader);
  canvas->drawRect(Rect::MakeXYWH(10, 10, 80, 80), paint);

  Bitmap bitmap;
  bitmap.allocPixels(100, 100);
  Pixmap pixmap(bitmap);
  auto result = surface->readPixels(pixmap.info(), pixmap.writablePixels());
  ASSERT_TRUE(result);

  // Verify the gradient rendered something (center pixel should be non-transparent).
  auto centerPixel = pixmap.getColor(50, 50);
  EXPECT_GT(centerPixel.alpha, 0.0f);

  // Verify gradient has variation — top-left should be redder, bottom-right bluer.
  auto topLeftPixel = pixmap.getColor(20, 20);
  auto bottomRightPixel = pixmap.getColor(80, 80);
  EXPECT_GT(topLeftPixel.red, bottomRightPixel.red);
  EXPECT_GT(bottomRightPixel.blue, topLeftPixel.blue);
}

/**
 * Minimal sanity check that the modular path doesn't crash on basic rendering.
 */
TGFX_TEST(ModularShaderTest, BasicRenderSanity) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 100, 100);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  Paint paint;
  paint.setColor(Color::White());
  canvas->drawRect(Rect::MakeXYWH(10, 10, 80, 80), paint);

  Bitmap bitmap;
  bitmap.allocPixels(100, 100);
  Pixmap pixmap(bitmap);
  auto result = surface->readPixels(pixmap.info(), pixmap.writablePixels());
  EXPECT_TRUE(result);
}

}  // namespace tgfx

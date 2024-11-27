/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "base/TGFXTest.h"
#include "gpu/opengl/GLCaps.h"
#include "gpu/opengl/GLUtil.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Size.h"
#include "tgfx/gpu/opengl/GLDevice.h"
#include "tgfx/svg/SVGGenerator.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(SVGGenerateTest, PureColor) {
  auto device = DevicePool::Make();
  ASSERT_TRUE(device != nullptr);
  auto* context = device->lockContext();
  ASSERT_TRUE(context != nullptr);

  tgfx::Paint paint;
  paint.setColor(Color::Blue());

  SVGGenerator SVGGenerator;
  auto* canvas = SVGGenerator.beginGenerate(context, ISize::Make(200, 200));
  canvas->drawRect(Rect::MakeXYWH(50, 50, 100, 100), paint);
  std::string SVGString = SVGGenerator.finishGenerate();
}

TGFX_TEST(SVGGenerateTest, OpacityColor) {
  tgfx::Paint paint;
  paint.setColor(Color::Blue());
  paint.setAlpha(0.5f);
}

TGFX_TEST(SVGGenerateTest, LinearGradient) {
  tgfx::Paint paint;
  auto shader = tgfx::Shader::MakeLinearGradient(
      tgfx::Point{50.f, 50.f}, tgfx::Point{150.f, 150.f},
      {tgfx::Color{0.f, 1.f, 0.f, 1.f}, tgfx::Color{0.f, 0.f, 0.f, 1.f}}, {});
  paint.setShader(shader);
}

TGFX_TEST(SVGGenerateTest, RadialGradient) {
  tgfx::Paint paint;
  tgfx::Point center{100.f, 100.f};
  auto shader = tgfx::Shader::MakeRadialGradient(
      center, 50, {tgfx::Color::Red(), tgfx::Color::Blue(), tgfx::Color::Black()}, {0, 0.5, 1.0});
  paint.setShader(shader);
}

TGFX_TEST(SVGGenerateTest, UnsupportedGradient) {
  tgfx::Paint paint;
  tgfx::Point center{100.f, 100.f};
  auto shader = tgfx::Shader::MakeConicGradient(
      center, 0, 360, {tgfx::Color::Red(), tgfx::Color::Blue(), tgfx::Color::Black()},
      {0, 0.5, 1.0});
  paint.setShader(shader);
}

TGFX_TEST(SVGGenerateTest, ImagePattern) {
}

TGFX_TEST(SVGGenerateTest, BlendMode) {
  Paint paintBackground;
  paintBackground.setColor(Color::White());
  // innerCanvas->drawRect(tgfx::Rect::MakeXYWH(0, 0, 200, 200), paintBackground);

  Paint paint;
  paint.setColor(Color::Red());
  paint.setBlendMode(BlendMode::Difference);
  // innerCanvas->drawRect(tgfx::Rect::MakeXYWH(50, 50, 100, 100), paint);
}

TGFX_TEST(SVGGenerateTest, StrokeWidth) {
  Paint paint;
  paint.setColor(Color::Red());
  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(5);
  // innerCanvas->drawRect(tgfx::Rect::MakeXYWH(50, 50, 100, 100), paint);
}

}  // namespace tgfx
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

#include <iostream>
#include <string>
#include "base/TGFXTest.h"
#include "gpu/opengl/GLCaps.h"
#include "gpu/opengl/GLUtil.h"
#include "gtest/gtest.h"
#include "svg/xml/XMLParser.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Size.h"
#include "tgfx/gpu/opengl/GLDevice.h"
#include "tgfx/svg/SVGGenerator.h"
#include "tgfx/svg/xml/XMLDOM.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(SVGExportTest, PureColor) {
  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"200\" height=\"200\"><rect "
      "fill=\"#00F\" x=\"50\" y=\"50\" width=\"100\" height=\"100\"/></svg>";

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  tgfx::Paint paint;
  paint.setColor(Color::Blue());

  SVGGenerator SVGGenerator;
  auto* canvas = SVGGenerator.beginGenerate(context, ISize::Make(200, 200), false);
  canvas->drawRect(Rect::MakeXYWH(50, 50, 100, 100), paint);
  std::string SVGString = SVGGenerator.finishGenerate();
  ASSERT_EQ(SVGString, compareString);
}

TGFX_TEST(SVGExportTest, OpacityColor) {
  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"200\" height=\"200\"><circle "
      "fill=\"#00007F\" fill-opacity=\"0.5\" cx=\"100\" cy=\"100\" r=\"100\"/></svg>";

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  tgfx::Paint paint;
  paint.setColor(Color::Blue());
  paint.setAlpha(0.5f);

  SVGGenerator SVGGenerator;
  auto* canvas = SVGGenerator.beginGenerate(context, ISize::Make(200, 200), false);
  canvas->drawCircle(100, 100, 100, paint);
  std::string SVGString = SVGGenerator.finishGenerate();

  ASSERT_EQ(SVGString, compareString);
}

TGFX_TEST(SVGExportTest, LinearGradient) {
  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"200\" "
      "height=\"200\"><defs><linearGradient id=\"gradient_0\" gradientUnits=\"userSpaceOnUse\" "
      "x1=\"50\" y1=\"50\" x2=\"150\" y2=\"150\"><stop offset=\"0\" stop-color=\"#0F0\"/><stop "
      "offset=\"1\" stop-color=\"#000\"/></linearGradient></defs><circle fill=\"url(#gradient_0)\" "
      "cx=\"100\" cy=\"100\" r=\"100\"/></svg>";

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  tgfx::Paint paint;
  auto shader = tgfx::Shader::MakeLinearGradient(
      tgfx::Point{50.f, 50.f}, tgfx::Point{150.f, 150.f},
      {tgfx::Color{0.f, 1.f, 0.f, 1.f}, tgfx::Color{0.f, 0.f, 0.f, 1.f}}, {});
  paint.setShader(shader);

  SVGGenerator SVGGenerator;
  auto* canvas = SVGGenerator.beginGenerate(context, ISize::Make(200, 200), false);
  canvas->drawCircle(100, 100, 100, paint);
  std::string SVGString = SVGGenerator.finishGenerate();

  ASSERT_EQ(SVGString, compareString);
}

TGFX_TEST(SVGExportTest, RadialGradient) {
  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"200\" "
      "height=\"200\"><defs><radialGradient id=\"gradient_0\" gradientUnits=\"userSpaceOnUse\" "
      "r=\"50\" cx=\"100\" cy=\"100\"><stop offset=\"0\" stop-color=\"#F00\"/><stop offset=\"0.5\" "
      "stop-color=\"#00F\"/><stop offset=\"1\" stop-color=\"#000\"/></radialGradient></defs><rect "
      "fill=\"url(#gradient_0)\" x=\"50\" y=\"50\" width=\"100\" height=\"100\"/></svg>";

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  tgfx::Paint paint;
  tgfx::Point center{100.f, 100.f};
  auto shader = tgfx::Shader::MakeRadialGradient(
      center, 50, {tgfx::Color::Red(), tgfx::Color::Blue(), tgfx::Color::Black()}, {0, 0.5, 1.0});
  paint.setShader(shader);

  SVGGenerator SVGGenerator;
  auto* canvas = SVGGenerator.beginGenerate(context, ISize::Make(200, 200), false);
  canvas->drawRect(Rect::MakeXYWH(50, 50, 100, 100), paint);
  std::string SVGString = SVGGenerator.finishGenerate();

  ASSERT_EQ(SVGString, compareString);
}

TGFX_TEST(SVGExportTest, UnsupportedGradient) {

  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"200\" "
      "height=\"200\"><defs><linearGradient id=\"gradient_0\" gradientUnits=\"objectBoundingBox\" "
      "x1=\"0\" y1=\"0\" x2=\"1\" y2=\"0\"><stop offset=\"0\" stop-color=\"#F00\"/><stop "
      "offset=\"0.5\" stop-color=\"#00F\"/><stop offset=\"1\" "
      "stop-color=\"#000\"/></linearGradient></defs><rect fill=\"url(#gradient_0)\" x=\"50\" "
      "y=\"50\" width=\"100\" height=\"100\"/></svg>";

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  tgfx::Paint paint;
  tgfx::Point center{100.f, 100.f};
  auto shader = tgfx::Shader::MakeConicGradient(
      center, 0, 360, {tgfx::Color::Red(), tgfx::Color::Blue(), tgfx::Color::Black()},
      {0, 0.5, 1.0});
  paint.setShader(shader);

  SVGGenerator SVGGenerator;
  auto* canvas = SVGGenerator.beginGenerate(context, ISize::Make(200, 200), false);
  canvas->drawRect(Rect::MakeXYWH(50, 50, 100, 100), paint);
  std::string SVGString = SVGGenerator.finishGenerate();

  ASSERT_EQ(SVGString, compareString);
}

TGFX_TEST(SVGExportTest, BlendMode) {
  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"200\" height=\"200\"><rect "
      "fill=\"#FFF\" width=\"100\" height=\"100\"/><rect fill=\"#F00\" fill-opacity=\"1\" "
      "style=\"mix-blend-mode:difference\" x=\"50\" y=\"50\" width=\"100\" height=\"100\"/></svg>";

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Paint paintBackground;
  paintBackground.setColor(Color::White());

  Paint paint;
  paint.setColor(Color::Red());
  paint.setBlendMode(BlendMode::Difference);

  SVGGenerator SVGGenerator;
  auto* canvas = SVGGenerator.beginGenerate(context, ISize::Make(200, 200), false);
  canvas->drawRect(tgfx::Rect::MakeXYWH(0, 0, 100, 100), paintBackground);
  canvas->drawRect(tgfx::Rect::MakeXYWH(50, 50, 100, 100), paint);
  std::string SVGString = SVGGenerator.finishGenerate();

  ASSERT_EQ(SVGString, compareString);
}

TGFX_TEST(SVGExportTest, StrokeWidth) {
  std::string compareString =
      "<?xml version=\"1.0\" encoding=\"utf-8\" ?><svg xmlns=\"http://www.w3.org/2000/svg\" "
      "xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"200\" height=\"200\"><path "
      "fill=\"#F00\" d=\"M47.5 47.5L152.5 47.5L152.5 152.5L47.5 152.5L47.5 47.5ZM52.5 52.5L52.5 "
      "147.5L147.5 147.5L147.5 52.5L52.5 52.5Z\"/></svg>";

  ContextScope scope;
  auto* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  Paint paint;
  paint.setColor(Color::Red());
  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(5);

  SVGGenerator SVGGenerator;
  auto* canvas = SVGGenerator.beginGenerate(context, ISize::Make(200, 200), false);
  canvas->drawRect(tgfx::Rect::MakeXYWH(50, 50, 100, 100), paint);
  std::string SVGString = SVGGenerator.finishGenerate();
}

}  // namespace tgfx
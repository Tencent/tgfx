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

#include <memory>
#include <vector>
#include "CornerPinEffect.h"
#include "core/filters/ColorImageFilter.h"
#include "core/filters/DropShadowImageFilter.h"
#include "core/filters/GaussianBlurImageFilter.h"
#include "core/filters/InnerShadowImageFilter.h"
#include "core/shaders/GradientShader.h"
#include "core/shaders/ImageShader.h"
#include "core/utils/MathExtra.h"
#include "gtest/gtest.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/GradientType.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Recorder.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Size.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/TileMode.h"
#include "utils/TestUtils.h"
#include "utils/common.h"

namespace tgfx {

TGFX_TEST(FilterTest, ColorMatrixFilter) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, image->width(), image->height());
  auto canvas = surface->getCanvas();
  Paint paint;
  std::array<float, 20> matrix = {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
  paint.setColorFilter(ColorFilter::Matrix(matrix));
  canvas->drawImage(image, &paint);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/identityMatrix"));
  canvas->clear();
  std::array<float, 20> greyColorMatrix = {0.21f, 0.72f, 0.07f, 0.41f, 0,  // red
                                           0.21f, 0.72f, 0.07f, 0.41f, 0,  // green
                                           0.21f, 0.72f, 0.07f, 0.41f, 0,  // blue
                                           0,     0,     0,     1.0f,  0};
  paint.setColorFilter(ColorFilter::Matrix(greyColorMatrix));
  canvas->drawImage(image, &paint);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/greyColorMatrix"));
}

TGFX_TEST(FilterTest, ModeColorFilter) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/rotation.jpg");
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, image->width() / 4, image->height() / 4);
  auto canvas = surface->getCanvas();
  canvas->scale(0.25f, 0.25f);
  Paint paint;
  auto modeColorFilter = ColorFilter::Blend(Color::Red(), BlendMode::Multiply);
  paint.setColorFilter(modeColorFilter);
  canvas->drawImage(image, &paint);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/ModeColorFilter"));
}

TGFX_TEST(FilterTest, ComposeColorFilter) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/rotation.jpg");
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, image->width() / 4, image->height() / 4);
  auto canvas = surface->getCanvas();
  canvas->scale(0.25f, 0.25f);
  Paint paint;
  auto matrixFilter = ColorFilter::Matrix({0.2f, 0,    0, 0, 0,  // red
                                           0,    0.2f, 0, 0, 0,  // green
                                           0,    0,    2, 0, 0,  // blue
                                           0,    0,    0, 1, 0});
  auto lumaFilter = ColorFilter::Matrix(lumaColorMatrix);
  auto composeFilter = ColorFilter::Compose(matrixFilter, lumaFilter);
  paint.setColorFilter(std::move(composeFilter));
  canvas->drawImage(image, &paint);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/ComposeColorFilter"));
}

TGFX_TEST(FilterTest, ShaderMaskFilter) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto mask = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(mask != nullptr);
  auto shader = Shader::MakeImageShader(mask);
  ASSERT_TRUE(shader != nullptr);
  auto lumaFilter = ColorFilter::Matrix(lumaColorMatrix);
  shader = shader->makeWithColorFilter(lumaFilter);
  ASSERT_TRUE(shader != nullptr);
  auto maskFilter = MaskFilter::MakeShader(shader);
  auto image = MakeImage("resources/apitest/rotation.jpg");
  image = image->makeOriented(Orientation::LeftBottom);
  image = image->makeMipmapped(true);
  image = ScaleImage(image, 0.25f);
  image = image->makeRasterized();
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, image->width(), image->height());
  auto canvas = surface->getCanvas();
  Paint paint;
  paint.setMaskFilter(maskFilter);
  canvas->drawImage(image, &paint);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/shaderMaskFilter"));
}

TGFX_TEST(FilterTest, Blur) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/rotation.jpg");
  ASSERT_TRUE(image != nullptr);
  auto imageMatrix = Matrix::MakeScale(0.2f, 0.2f);
  auto bounds = Rect::MakeWH(image->width(), image->height());
  imageMatrix.mapRect(&bounds);
  auto imageWidth = static_cast<float>(bounds.width());
  auto imageHeight = static_cast<float>(bounds.height());
  auto padding = 30.f;
  Paint paint;
  auto surface = Surface::Make(context, static_cast<int>(imageWidth * 2.f + padding * 3.f),
                               static_cast<int>(imageHeight * 2.f + padding * 3.f));
  auto canvas = surface->getCanvas();
  canvas->concat(Matrix::MakeTrans(padding, padding));
  canvas->save();
  canvas->concat(imageMatrix);
  canvas->drawImage(image, &paint);
  canvas->restore();
  Path path;
  path.addRect(Rect::MakeWH(imageWidth, imageHeight));
  Stroke stroke(1.f);
  stroke.applyToPath(&path);
  paint.setImageFilter(nullptr);
  paint.setColor(Color{1.f, 0.f, 0.f, 1.f});
  canvas->drawPath(path, paint);

  canvas->concat(Matrix::MakeTrans(imageWidth + padding, 0));
  canvas->save();
  canvas->concat(imageMatrix);
  // The blur filter is applied to the scaled image after it is drawn on the canvas.
  paint.setImageFilter(ImageFilter::Blur(130, 130, TileMode::Decal));
  canvas->drawImage(image, &paint);
  canvas->restore();
  paint.setImageFilter(nullptr);
  canvas->drawPath(path, paint);
  paint.setImageFilter(nullptr);

  canvas->concat(Matrix::MakeTrans(-imageWidth - padding, imageHeight + padding));
  canvas->save();
  canvas->concat(imageMatrix);
  Point filterOffset = {};
  auto cropRect = Rect::MakeXYWH(0, 0, image->width(), image->height());
  auto filterImage = image->makeWithFilter(ImageFilter::Blur(130, 130, TileMode::Repeat),
                                           &filterOffset, &cropRect);
  ASSERT_TRUE(filterImage != nullptr);
  EXPECT_EQ(filterImage->width(), image->width());
  EXPECT_EQ(filterImage->height(), image->height());
  EXPECT_EQ(filterOffset.x, 0.0f);
  EXPECT_EQ(filterOffset.y, 0.0f);
  canvas->drawImage(filterImage, &paint);
  canvas->restore();
  paint.setImageFilter(nullptr);
  canvas->drawPath(path, paint);

  canvas->concat(Matrix::MakeTrans(imageWidth + padding, 0));
  canvas->save();
  canvas->concat(imageMatrix);
  auto filter = ImageFilter::Blur(130, 130, TileMode::Clamp);
  cropRect = Rect::MakeLTRB(2000, -100, 3124, 2000);
  filterImage = image->makeWithFilter(filter, &filterOffset, &cropRect);
  canvas->drawImage(filterImage, 2000, -100, &paint);
  cropRect = Rect::MakeXYWH(1000, 1000, 1000, 1000);
  filterImage = image->makeWithFilter(filter, &filterOffset, &cropRect);
  canvas->drawImage(filterImage, 1000, 1000, &paint);
  cropRect = Rect::MakeXYWH(1000, 2000, 1000, 1000);
  filterImage = image->makeWithFilter(filter, &filterOffset, &cropRect);
  canvas->drawImage(filterImage, 1000, 2000, &paint);
  canvas->restore();
  canvas->drawPath(path, paint);

  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/blur"));
}

TGFX_TEST(FilterTest, DropShadow) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/image_as_mask.png");
  ASSERT_TRUE(image != nullptr);
  auto imageWidth = static_cast<float>(image->width());
  auto imageHeight = static_cast<float>(image->height());
  auto padding = 30.f;
  Paint paint;
  auto surface = Surface::Make(context, static_cast<int>(imageWidth * 2.f + padding * 3.f),
                               static_cast<int>(imageHeight * 2.f + padding * 3.f));
  auto canvas = surface->getCanvas();
  canvas->concat(Matrix::MakeTrans(padding, padding));
  paint.setImageFilter(ImageFilter::Blur(5, 5));
  canvas->drawImage(image, &paint);

  canvas->concat(Matrix::MakeTrans(imageWidth + padding, 0));
  paint.setImageFilter(ImageFilter::DropShadowOnly(0, 0, 5, 5, Color::White()));
  canvas->drawImage(image, &paint);

  canvas->concat(Matrix::MakeTrans(-imageWidth - padding, imageWidth + padding));
  paint.setImageFilter(ImageFilter::DropShadow(0, 0, 5, 5, Color::White()));
  canvas->drawImage(image, &paint);

  canvas->concat(Matrix::MakeTrans(imageWidth + padding, 0));
  auto filter = ImageFilter::DropShadow(3, 3, 0, 0, Color::White());
  paint.setImageFilter(filter);
  canvas->drawImage(image, &paint);

  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/dropShadow"));

  auto src = Rect::MakeXYWH(10, 10, 10, 10);
  auto bounds = filter->filterBounds(src);
  EXPECT_EQ(bounds, Rect::MakeXYWH(10, 10, 13, 13));
  bounds = ImageFilter::DropShadowOnly(3, 3, 0, 0, Color::White())->filterBounds(src);
  EXPECT_EQ(bounds, Rect::MakeXYWH(13, 13, 10, 10));
}

TGFX_TEST(FilterTest, BlurLargePixel) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/rotation.jpg");
  ASSERT_TRUE(image != nullptr);
  Matrix imageMatrix = {};
  image = image->makeRasterized();
  auto bounds = Rect::MakeWH(image->width(), image->height());
  imageMatrix.mapRect(&bounds);
  auto imageWidth = static_cast<float>(bounds.width());
  auto imageHeight = static_cast<float>(bounds.height());
  auto surface =
      Surface::Make(context, static_cast<int>(imageWidth * 2), static_cast<int>(imageHeight * 2));
  auto canvas = surface->getCanvas();
  canvas->concat(Matrix::MakeTrans(imageWidth / 2.0f, imageHeight / 2.0f));

  Paint paint;
  paint.setImageFilter(ImageFilter::Blur(5000, 1500));
  canvas->drawImage(image, &paint);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/blur-large-pixel"));
}

TGFX_TEST(FilterTest, ImageFilterShader) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/assets/bridge.jpg");
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, 720, 720);
  auto canvas = surface->getCanvas();
  image = image->makeMipmapped(true);
  auto filter = ImageFilter::DropShadow(0, 0, 90, 90, Color::Black());
  image = image->makeWithFilter(std::move(filter));
  auto imageSize = 480.0f;
  auto imageScale = imageSize / static_cast<float>(image->width());
  SamplingOptions sampling(FilterMode::Linear, MipmapMode::Linear);
  auto shader = Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Repeat, sampling);
  auto matrix = Matrix::MakeScale(imageScale);
  matrix.postTranslate(120, 120);
  shader = shader->makeWithMatrix(matrix);
  Paint paint = {};
  paint.setShader(std::move(shader));
  canvas->drawRect(Rect::MakeWH(720, 720), paint);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/ImageFilterShader"));
}

TGFX_TEST(FilterTest, ComposeImageFilter) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/assets/bridge.jpg");
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, 720, 720);
  auto canvas = surface->getCanvas();
  image = image->makeMipmapped(true);
  auto blueFilter = ImageFilter::DropShadow(100, 100, 0, 0, Color::Blue());
  auto greenFilter = ImageFilter::DropShadow(-100, -100, 0, 0, Color::Green());
  auto blackFilter = ImageFilter::DropShadow(0, 0, 100, 100, Color::Black());
  auto composeFilter = ImageFilter::Compose({blueFilter, greenFilter, blackFilter});
  auto filterImage = image->makeWithFilter(std::move(composeFilter));
  auto imageSize = 512.0f;
  auto imageScale = imageSize / static_cast<float>(filterImage->width());
  canvas->translate(104, 104);
  canvas->scale(imageScale, imageScale);
  canvas->drawImage(filterImage);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/ComposeImageFilter"));

  filterImage = image->makeWithFilter(blueFilter);
  auto filterBounds =
      greenFilter->filterBounds(Rect::MakeWH(filterImage->width(), filterImage->height()));
  filterImage = filterImage->makeWithFilter(greenFilter, nullptr, &filterBounds);
  filterBounds =
      blackFilter->filterBounds(Rect::MakeWH(filterImage->width(), filterImage->height()));
  filterBounds.inset(200, 200);
  filterImage = filterImage->makeWithFilter(blackFilter, nullptr, &filterBounds);
  canvas->clear();
  canvas->translate(200, 200);
  canvas->drawImage(filterImage);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/ComposeImageFilter2"));
}

TGFX_TEST(FilterTest, RuntimeEffect) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/assets/bridge.jpg");
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, 720, 720);
  auto canvas = surface->getCanvas();
  image = image->makeMipmapped(true);
  image = ScaleImage(image, 0.5f, SamplingOptions(FilterMode::Linear, MipmapMode::Linear));
  image = image->makeRasterized();

  auto effect1 = CornerPinEffect::Make(
      {0, 0}, {static_cast<float>(image->width()), 0},
      {static_cast<float>(image->width()), static_cast<float>(image->height())},
      {0, static_cast<float>(image->height())});
  auto effect2 = CornerPinEffect::Make({484, 54}, {764, 80}, {764, 504}, {482, 512});
  auto filter1 = ImageFilter::Runtime(std::move(effect1));
  auto filter2 = ImageFilter::Runtime(effect2);
  auto composeFilter = ImageFilter::Compose(filter1, filter2);
  image = image->makeWithFilter(std::move(composeFilter));
  canvas->drawImage(image, 200, 100);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/RuntimeEffect"));
}

TGFX_TEST(FilterTest, InnerShadow) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);
  auto imageWidth = static_cast<float>(image->width());
  auto imageHeight = static_cast<float>(image->height());
  auto padding = 30.f;
  Paint paint;
  auto surface = Surface::Make(context, static_cast<int>(imageWidth * 2.f + padding * 3.f),
                               static_cast<int>(imageHeight * 2.f + padding * 3.f));
  auto canvas = surface->getCanvas();
  canvas->concat(Matrix::MakeTrans(padding, padding));
  paint.setImageFilter(ImageFilter::Blur(15, 15));
  canvas->drawImage(image, &paint);

  canvas->concat(Matrix::MakeTrans(imageWidth + padding, 0));
  paint.setImageFilter(ImageFilter::InnerShadowOnly(0, 0, 15, 15, Color::White()));
  canvas->drawImage(image, &paint);

  canvas->concat(Matrix::MakeTrans(-imageWidth - padding, imageWidth + padding));
  paint.setImageFilter(ImageFilter::InnerShadow(0, 0, 15, 15, Color::White()));
  canvas->drawImage(image, &paint);

  canvas->concat(Matrix::MakeTrans(imageWidth + padding, 0));
  auto filter = ImageFilter::InnerShadow(3, 3, 0, 0, Color::White());
  paint.setImageFilter(filter);
  canvas->drawImage(image, &paint);

  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/innerShadow"));
}

TGFX_TEST(FilterTest, GetFilterProperties) {
  auto modeColorFilter = ColorFilter::Blend(Color::Red(), BlendMode::Multiply);
  Color color;
  BlendMode mode;
  bool ret = modeColorFilter->asColorMode(&color, &mode);
  EXPECT_TRUE(ret);
  EXPECT_EQ(color, Color::Red());
  EXPECT_EQ(mode, BlendMode::Multiply);

  auto lumaFilter = ColorFilter::Matrix(lumaColorMatrix);
  ret = lumaFilter->asColorMode(nullptr, nullptr);
  EXPECT_FALSE(ret);

  auto filter = ColorFilter::Compose(modeColorFilter, lumaFilter);
  ret = filter->asColorMode(nullptr, nullptr);
  EXPECT_FALSE(ret);

  {
    auto imageFilter = ImageFilter::Blur(20, 30);
    EXPECT_EQ(imageFilter->type(), ImageFilter::Type::Blur);
    Size blurSize = imageFilter->filterBounds({}).size();
    EXPECT_EQ(blurSize.width, 80.f);
    EXPECT_EQ(blurSize.height, 120.f);
  }

  {
    auto imageFilter = ImageFilter::DropShadow(15.f, 15.f, 20.f, 30.f, Color::White());
    EXPECT_EQ(imageFilter->type(), ImageFilter::Type::DropShadow);
    auto dropShadowFilter = std::static_pointer_cast<const DropShadowImageFilter>(imageFilter);
    Size blurSize = dropShadowFilter->blurFilter->filterBounds({}).size();
    EXPECT_EQ(blurSize.width, 80.f);
    EXPECT_EQ(blurSize.height, 120.f);
    EXPECT_EQ(dropShadowFilter->dx, 15.f);
    EXPECT_EQ(dropShadowFilter->dy, 15.f);
    EXPECT_EQ(dropShadowFilter->color, Color::White());
    EXPECT_EQ(dropShadowFilter->shadowOnly, false);
  }

  {
    auto imageFilter = ImageFilter::DropShadowOnly(15.f, 15.f, 20.f, 30.f, Color::White());
    EXPECT_EQ(imageFilter->type(), ImageFilter::Type::DropShadow);
    auto dropShadowFilter = std::static_pointer_cast<const DropShadowImageFilter>(imageFilter);
    Size blurSize = dropShadowFilter->blurFilter->filterBounds({}).size();
    EXPECT_EQ(blurSize.width, 80.f);
    EXPECT_EQ(blurSize.height, 120.f);
    EXPECT_EQ(dropShadowFilter->dx, 15.f);
    EXPECT_EQ(dropShadowFilter->dy, 15.f);
    EXPECT_EQ(dropShadowFilter->color, Color::White());
    EXPECT_EQ(dropShadowFilter->shadowOnly, true);
  }

  {
    auto imageFilter = ImageFilter::InnerShadow(15.f, 15.f, 20.f, 30.f, Color::White());
    EXPECT_EQ(imageFilter->type(), ImageFilter::Type::InnerShadow);
    auto innerShadowFilter = std::static_pointer_cast<InnerShadowImageFilter>(imageFilter);
    Size blurSize = innerShadowFilter->blurFilter->filterBounds({}).size();
    EXPECT_EQ(blurSize.width, 80.f);
    EXPECT_EQ(blurSize.height, 120.f);
    EXPECT_EQ(innerShadowFilter->dx, 15.f);
    EXPECT_EQ(innerShadowFilter->dy, 15.f);
    EXPECT_EQ(innerShadowFilter->color, Color::White());
    EXPECT_EQ(innerShadowFilter->shadowOnly, false);
  }

  {
    auto imageFilter = ImageFilter::InnerShadowOnly(15.f, 15.f, 20.f, 30.f, Color::White());
    EXPECT_EQ(imageFilter->type(), ImageFilter::Type::InnerShadow);
    auto innerShadowFilter = std::static_pointer_cast<InnerShadowImageFilter>(imageFilter);
    Size blurSize = innerShadowFilter->blurFilter->filterBounds({}).size();
    EXPECT_EQ(blurSize.width, 80.f);
    EXPECT_EQ(blurSize.height, 120.f);
    EXPECT_EQ(innerShadowFilter->dx, 15.f);
    EXPECT_EQ(innerShadowFilter->dy, 15.f);
    EXPECT_EQ(innerShadowFilter->color, Color::White());
    EXPECT_EQ(innerShadowFilter->shadowOnly, true);
  }

  {
    auto imageFilter = ImageFilter::ColorFilter(modeColorFilter);
    EXPECT_EQ(imageFilter->type(), ImageFilter::Type::Color);
    auto colorFilter = std::static_pointer_cast<ColorImageFilter>(imageFilter);
    Color color;
    BlendMode mode;
    bool ret = colorFilter->filter->asColorMode(&color, &mode);
    EXPECT_TRUE(ret);
    EXPECT_EQ(color, Color::Red());
    EXPECT_EQ(mode, BlendMode::Multiply);
  }

  {
    auto blueFilter = ImageFilter::DropShadow(100, 100, 0, 0, Color::Blue());
    auto greenFilter = ImageFilter::DropShadow(-100, -100, 0, 0, Color::Green());
    auto blackFilter = ImageFilter::DropShadow(0, 0, 300, 300, Color::Black());
    auto imageFilter = ImageFilter::Compose({blueFilter, greenFilter, blackFilter});
    EXPECT_EQ(imageFilter->type(), ImageFilter::Type::Compose);
  }

  {
    auto effect = CornerPinEffect::Make({484, 54}, {764, 80}, {764, 504}, {482, 512});
    auto imageFilter = ImageFilter::Runtime(std::move(effect));
    EXPECT_EQ(imageFilter->type(), ImageFilter::Type::Runtime);
  }
}

TGFX_TEST(FilterTest, GetShaderProperties) {

  {
    auto colorShader = Shader::MakeColorShader(Color::Red());
    ASSERT_TRUE(colorShader != nullptr);
    EXPECT_EQ(colorShader->type(), Shader::Type::Color);

    Color color = {};
    bool ret = colorShader->asColor(&color);
    EXPECT_TRUE(ret);
    EXPECT_EQ(color, Color::Red());
  }

  {
    auto inputImage = MakeImage("resources/apitest/imageReplacement.png");
    ASSERT_TRUE(inputImage != nullptr);
    auto shader = Shader::MakeImageShader(inputImage, TileMode::Mirror, TileMode::Repeat);
    ASSERT_TRUE(shader != nullptr);

    EXPECT_EQ(shader->type(), Shader::Type::Image);

    auto imageShader = std::static_pointer_cast<ImageShader>(shader);
    EXPECT_TRUE(imageShader != nullptr);
    EXPECT_EQ(imageShader->tileModeX, TileMode::Mirror);
    EXPECT_EQ(imageShader->tileModeY, TileMode::Repeat);
  }

  {
    auto redShader = Shader::MakeColorShader(Color::Red());
    auto greenShader = Shader::MakeColorShader(Color::Green());
    auto blendShader = Shader::MakeBlend(BlendMode::SrcOut, redShader, greenShader);
    ASSERT_TRUE(blendShader != nullptr);
    EXPECT_EQ(blendShader->type(), Shader::Type::Blend);
  }

  std::vector<Color> colors = {Color::Red(), Color::Green(), Color::Blue()};
  std::vector<float> positions = {0.f, 0.5f, 1.f};
  auto startPoint = Point::Make(0, 0);
  auto endPoint = Point::Make(100, 100);
  {
    auto shader = Shader::MakeLinearGradient(startPoint, endPoint, colors, positions);
    ASSERT_TRUE(shader != nullptr);
    EXPECT_EQ(shader->type(), Shader::Type::Gradient);

    auto gradientShader = std::static_pointer_cast<LinearGradientShader>(shader);

    GradientInfo info;
    auto gradientType = gradientShader->asGradient(&info);
    EXPECT_EQ(gradientType, GradientType::Linear);
    EXPECT_EQ(info.colors, colors);
    EXPECT_EQ(info.positions, positions);
    EXPECT_EQ(info.points[0], startPoint);
    EXPECT_EQ(info.points[1], endPoint);
  }

  auto center = Point::Make(50, 50);
  float radius = 50;
  {
    auto shader = Shader::MakeRadialGradient(center, radius, colors, positions);
    ASSERT_TRUE(shader != nullptr);
    EXPECT_EQ(shader->type(), Shader::Type::Gradient);

    auto gradientShader = std::static_pointer_cast<LinearGradientShader>(shader);

    GradientInfo info;
    auto gradientType = gradientShader->asGradient(&info);
    EXPECT_EQ(gradientType, GradientType::Radial);
    EXPECT_EQ(info.colors, colors);
    EXPECT_EQ(info.positions, positions);
    EXPECT_EQ(info.points[0], center);
    EXPECT_EQ(info.radiuses[0], radius);
  }

  {
    float startAngle = 0.f;
    float endAngle = 360.f;
    auto shader = Shader::MakeConicGradient(center, startAngle, endAngle, colors, positions);
    ASSERT_TRUE(shader != nullptr);
    EXPECT_EQ(shader->type(), Shader::Type::Gradient);

    auto gradientShader = std::static_pointer_cast<LinearGradientShader>(shader);

    GradientInfo info;
    auto gradientType = gradientShader->asGradient(&info);
    EXPECT_EQ(gradientType, GradientType::Conic);
    EXPECT_EQ(info.colors, colors);
    EXPECT_EQ(info.positions, positions);
    EXPECT_EQ(info.points[0], center);
    EXPECT_EQ(info.radiuses[0], startAngle);
    EXPECT_EQ(info.radiuses[1], endAngle);
  }

  center = Point::Make(50, 50);
  float halfDiagonal = 50;
  {
    auto shader = Shader::MakeDiamondGradient(center, halfDiagonal, colors, positions);
    ASSERT_TRUE(shader != nullptr);
    EXPECT_EQ(shader->type(), Shader::Type::Gradient);

    auto gradientShader = std::static_pointer_cast<DiamondGradientShader>(shader);

    GradientInfo info;
    auto gradientType = gradientShader->asGradient(&info);
    EXPECT_EQ(gradientType, GradientType::Diamond);
    EXPECT_EQ(info.colors, colors);
    EXPECT_EQ(info.positions, positions);
    EXPECT_FLOAT_EQ(info.points[0].x, center.x);
    EXPECT_FLOAT_EQ(info.points[0].y, center.y);
    EXPECT_FLOAT_EQ(info.radiuses[0], halfDiagonal);
  }
}

TGFX_TEST(FilterTest, AlphaThreshold) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 100, 100);
  auto canvas = surface->getCanvas();
  auto paint = Paint();
  paint.setColor(Color::FromRGBA(100, 0, 0, 128));
  auto opacityFilter = ColorFilter::AlphaThreshold(129.f / 255.f);
  paint.setColorFilter(opacityFilter);
  auto rect = Rect::MakeWH(100, 100);
  canvas->drawRect(rect, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/AlphaThreshold_empty"));
  opacityFilter = ColorFilter::AlphaThreshold(-1.f);
  paint.setColorFilter(opacityFilter);
  canvas->drawRect(rect, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/AlphaThreshold"));
}

TGFX_TEST(FilterTest, EmptyShadowTest) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 100, 100);
  auto canvas = surface->getCanvas();
  auto paint = Paint();
  paint.setColor(Color::FromRGBA(100, 0, 0, 255));
  auto filter = ImageFilter::DropShadow(20, 20, 0, 0, Color::Transparent());
  EXPECT_EQ(filter, nullptr);
  filter = ImageFilter::DropShadowOnly(20, 20, 0, 0, Color::Transparent());
  EXPECT_NE(filter, nullptr);
  paint.setImageFilter(filter);

  auto rect = Rect::MakeWH(100, 100);
  canvas->drawRect(rect, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/EmptyShadowTest"));

  filter = ImageFilter::InnerShadow(20, 20, 0, 0, Color::Transparent());
  EXPECT_EQ(filter, nullptr);

  filter = ImageFilter::InnerShadowOnly(20, 20, 0, 0, Color::Transparent());
  EXPECT_NE(filter, nullptr);
  paint.setImageFilter(filter);
  canvas->drawRect(rect, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/EmptyShadowTest"));
}

TGFX_TEST(FilterTest, OpacityShadowTest) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  auto canvas = surface->getCanvas();
  canvas->drawColor(Color::Black());

  auto paint = Paint();
  paint.setColor(Color::FromRGBA(255, 0, 0, 255));

  Color shadowColor{1.0, 1.0, 1.0, 0.5};
  paint.setImageFilter(ImageFilter::DropShadow(20, 20, 10, 10, shadowColor));
  canvas->drawRect(Rect::MakeXYWH(10, 10, 50, 50), paint);

  paint.setImageFilter(ImageFilter::DropShadowOnly(20, 20, 10, 10, shadowColor));
  canvas->drawRect(Rect::MakeXYWH(110, 10, 50, 50), paint);

  paint.setImageFilter(ImageFilter::InnerShadow(20, 20, 10, 10, shadowColor));
  canvas->drawRect(Rect::MakeXYWH(10, 110, 50, 50), paint);

  paint.setImageFilter(ImageFilter::InnerShadowOnly(20, 20, 10, 10, shadowColor));
  canvas->drawRect(Rect::MakeXYWH(110, 110, 50, 50), paint);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/OpacityShadowTest"));
}

TGFX_TEST(FilterTest, InnerShadowBadCase) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 400);
  auto canvas = surface->getCanvas();
  auto paint = Paint();
  paint.setColor(Color::FromRGBA(255, 0, 0, 255));
  auto filter = ImageFilter::InnerShadow(80, 80, 1, 1, Color::Green());
  paint.setImageFilter(filter);
  auto rect = Rect::MakeWH(250, 250);
  Path path;
  path.addOval(rect);
  canvas->drawPath(path, paint);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/InnerShadowBadCase"));
}

TGFX_TEST(FilterTest, ClipInnerShadowImageFilter) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  int surfaceWidth = 100;
  int surfaceHeight = 100;
  auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
  ASSERT_TRUE(surface != nullptr);

  auto image = MakeImage("resources/apitest/image_as_mask.png");
  ASSERT_TRUE(image != nullptr);
  auto shadowFilter = ImageFilter::InnerShadow(0, -10.5, 2, 2, Color::FromRGBA(0, 0, 0, 128));
  image = image->makeWithFilter(shadowFilter);
  auto canvas = surface->getCanvas();
  canvas->scale(0.8571f, 0.8571f);
  {
    AutoCanvasRestore restore(canvas);
    canvas->clipRect(Rect::MakeWH(100.f, 30.f));
    canvas->drawImage(image);
  }
  {
    AutoCanvasRestore restore(canvas);
    canvas->clipRect(Rect::MakeXYWH(0.f, 30.f, 100.f, 30.f));
    canvas->drawImage(image);
  }
  {
    AutoCanvasRestore restore(canvas);
    canvas->clipRect(Rect::MakeXYWH(0, 60, 100, 30));
    canvas->drawImage(image);
  }
  {
    AutoCanvasRestore restore(canvas);
    canvas->clipRect(Rect::MakeXYWH(0, 90, 100, 10));
    canvas->drawImage(image);
  }
  context->flushAndSubmit();
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/ClipInnerShadowImageFilter"));
}

TGFX_TEST(FilterTest, GaussianBlurImageFilter) {
  ContextScope scope;
  Context* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  std::shared_ptr<Image> simpleImage = MakeImage("resources/apitest/image_as_mask.png");
  ASSERT_TRUE(simpleImage != nullptr);
  // The Gaussian blur operation expands image boundaries to reserve space for effect validation.
  constexpr float simpleCanvasMargin = 25.0f;
  const int simpleSurfaceWidth = simpleImage->width() + static_cast<int>(simpleCanvasMargin) * 2;
  const int simpleSurfaceHeight = simpleImage->height() + static_cast<int>(simpleCanvasMargin) * 2;
  auto simpleSurface = Surface::Make(context, simpleSurfaceWidth, simpleSurfaceHeight);
  ASSERT_TRUE(simpleSurface != nullptr);
  Canvas* simpleCanvas = simpleSurface->getCanvas();

  // Simple two-dimensional image blur.
  {
    simpleCanvas->save();

    simpleCanvas->clear();
    auto gaussianBlurFilter =
        std::make_shared<GaussianBlurImageFilter>(3.0f, 3.0f, TileMode::Decal);
    auto image = simpleImage->makeWithFilter(gaussianBlurFilter);
    const float drawLeft = static_cast<float>(simpleSurfaceWidth - image->width()) * 0.5f;
    const float drawTop = static_cast<float>(simpleSurfaceHeight - image->height()) * 0.5f;
    simpleCanvas->drawImage(image, drawLeft, drawTop);
    context->flushAndSubmit();
    EXPECT_TRUE(Baseline::Compare(simpleSurface, "FilterTest/GaussianBlurImageFilterSimple2D"));

    simpleCanvas->restore();
  }

  // Complex one-dimensional image blur.
  {
    simpleCanvas->save();

    simpleCanvas->clear();
    constexpr float imageScale = 0.8f;
    simpleCanvas->scale(imageScale, imageScale);
    // Move the image center to the left-top corner of the canvas.
    simpleCanvas->translate(static_cast<float>(simpleSurfaceWidth) * -0.5f / imageScale,
                            static_cast<float>(simpleSurfaceHeight) * -0.5f / imageScale);
    // Set a value exceeding the maximum blur factor.
    auto gaussianBlurFilter =
        std::make_shared<GaussianBlurImageFilter>(12.0f, 0.0f, TileMode::Decal);
    auto image = simpleImage->makeWithFilter(gaussianBlurFilter);
    const float drawLeft =
        (static_cast<float>(simpleSurfaceWidth) - static_cast<float>(image->width()) * imageScale) *
        0.5f / imageScale;
    const float drawTop = (static_cast<float>(simpleSurfaceHeight) -
                           static_cast<float>(image->height()) * imageScale) *
                          0.5f / imageScale;
    simpleCanvas->drawImage(image, drawLeft, drawTop);
    context->flushAndSubmit();
    EXPECT_TRUE(Baseline::Compare(simpleSurface, "FilterTest/GaussianBlurImageFilterComplex1D"));

    simpleCanvas->restore();
  }

  std::shared_ptr<Image> opaqueImage = MakeImage("resources/apitest/imageReplacement.jpg");
  ASSERT_TRUE(opaqueImage != nullptr);
  // Simulate tile-based rendering to validate seamless pixel transitions between adjacent tiles.
  {
    constexpr float canvasMargin = 25.0f;
    constexpr float imageScale = 1.2f;
    const int surfaceWidth = static_cast<int>(
        static_cast<float>(opaqueImage->width()) * imageScale + canvasMargin * 2.0f);
    const int surfaceHeight = static_cast<int>(
        static_cast<float>(opaqueImage->height()) * imageScale + canvasMargin * 2.0f);
    auto surface = Surface::Make(context, surfaceWidth, surfaceHeight);
    ASSERT_TRUE(surface != nullptr);
    Canvas* canvas = surface->getCanvas();
    canvas->scale(imageScale, imageScale);
    canvas->translate(canvasMargin / imageScale, canvasMargin / imageScale);
    auto gaussianBlurFilter =
        std::make_shared<GaussianBlurImageFilter>(5.0f, 5.0f, TileMode::Decal);

    // Divide into 4 equal tiles.
    auto clipRect1 = Rect::MakeWH(std::floor(static_cast<float>(opaqueImage->width()) * 0.5f),
                                  std::floor(static_cast<float>(opaqueImage->height()) * 0.5f));
    auto image1 = opaqueImage->makeWithFilter(gaussianBlurFilter, nullptr, &clipRect1);
    canvas->drawImage(image1, 0.0f, 0.0f);

    auto clipRect2 =
        Rect(clipRect1.right, 0.0f, static_cast<float>(opaqueImage->width()), clipRect1.bottom);
    auto image2 = opaqueImage->makeWithFilter(gaussianBlurFilter, nullptr, &clipRect2);
    canvas->drawImage(image2, static_cast<float>(opaqueImage->width()) * 0.5f, 0.0f);

    auto clipRect3 =
        Rect(0.0f, clipRect1.bottom, clipRect1.right, static_cast<float>(opaqueImage->height()));
    auto image3 = opaqueImage->makeWithFilter(gaussianBlurFilter, nullptr, &clipRect3);
    canvas->drawImage(image3, 0.0f, static_cast<float>(opaqueImage->height()) * 0.5f);

    auto clipRect4 = Rect(clipRect2.left, clipRect2.bottom, clipRect2.right, clipRect3.bottom);
    auto image4 = opaqueImage->makeWithFilter(gaussianBlurFilter, nullptr, &clipRect4);
    canvas->drawImage(image4, static_cast<float>(opaqueImage->width()) * 0.5f,
                      static_cast<float>(opaqueImage->height()) * 0.5f);

    context->flushAndSubmit();
    EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/GaussianBlurImageFilterComplex2D"));
  }
}

TGFX_TEST(FilterTest, Transform3DImageFilter) {
  ContextScope scope;
  Context* context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  ASSERT_TRUE(surface != nullptr);
  Canvas* canvas = surface->getCanvas();
  auto image = MakeImage("resources/apitest/imageReplacement.jpg");
  Size imageSize(static_cast<float>(image->width()), static_cast<float>(image->height()));
  auto anchor = Point::Make(0.5f, 0.5f);
  auto offsetToAnchorMatrix =
      Matrix3D::MakeTranslate(-anchor.x * imageSize.width, -anchor.y * imageSize.height, 0);
  auto invOffsetToAnchorMatrix =
      Matrix3D::MakeTranslate(anchor.x * imageSize.width, anchor.y * imageSize.height, 0);

  // Test basic drawing with css perspective type.
  {
    canvas->save();
    canvas->clear();

    auto cssPerspectiveMatrix = Matrix3D::I();
    constexpr float eyeDistance = 1200.f;
    constexpr float farZ = -1000.f;
    constexpr float shift = 10.f;
    const float nearZ = eyeDistance - shift;
    const float m22 = (2 - (farZ + nearZ) / eyeDistance) / (farZ - nearZ);
    cssPerspectiveMatrix.setRowColumn(2, 2, m22);
    const float m23 = -1.f + nearZ / eyeDistance - cssPerspectiveMatrix.getRowColumn(2, 2) * nearZ;
    cssPerspectiveMatrix.setRowColumn(2, 3, m23);
    cssPerspectiveMatrix.setRowColumn(3, 2, -1.f / eyeDistance);

    auto modelMatrix = Matrix3D::MakeRotate({0.f, 1.f, 0.f}, 45.f);
    modelMatrix.postTranslate(0.f, 0.f, -100.f);
    auto transform =
        invOffsetToAnchorMatrix * cssPerspectiveMatrix * modelMatrix * offsetToAnchorMatrix;
    auto cssTransform3DFilter = ImageFilter::Transform3D(transform);
    Paint paint = {};
    paint.setImageFilter(cssTransform3DFilter);
    canvas->drawImage(image, 45.f, 45.f, &paint);
    EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/Transform3DImageFilterCSSBasic"));
    canvas->restore();
  }

  const float halfImageW = imageSize.width * 0.5f;
  const float halfImageH = imageSize.height * 0.5f;
  auto standardvViewportMatrix = Matrix3D::MakeScale(halfImageW, halfImageH, 1.f);
  auto invStandardViewportMatrix = Matrix3D::MakeScale(1.f / halfImageW, 1.f / halfImageH, 1.f);
  // The field of view (in degrees) for the standard perspective projection model.
  constexpr float standardFovYDegress = 45.f;
  // The maximum position of the near plane on the Z axis for the standard perspective projection model.
  constexpr float standardMaxNearZ = 0.25f;
  // The minimum position of the far plane on the Z axis for the standard perspective projection model.
  constexpr float standardMinFarZ = 1000.f;
  // The target position of the camera for the standard perspective projection model, in pixels.
  constexpr Vec3 standardEyeCenter = {0.f, 0.f, 0.f};
  // The up direction unit vector for the camera in the standard perspective projection model.
  static constexpr Vec3 StandardEyeUp = {0.f, 1.f, 0.f};
  const float eyePositionZ = 1.f / tanf(DegreesToRadians(standardFovYDegress * 0.5f));
  const Vec3 eyePosition = {0.f, 0.f, eyePositionZ};
  auto viewMatrix = Matrix3D::LookAt(eyePosition, standardEyeCenter, StandardEyeUp);
  // Ensure nearZ is not too far away or farZ is not too close to avoid precision issues. For
  // example, if the z value of the near plane is less than 0, the projected model will be
  // outside the clipping range, or if the far plane is too close, the projected model may
  // exceed the clipping range with a slight rotation.
  const float nearZ = std::min(standardMaxNearZ, eyePositionZ * 0.1f);
  const float farZ = std::max(standardMinFarZ, eyePositionZ * 10.f);
  auto perspectiveMatrix = Matrix3D::Perspective(
      standardFovYDegress, static_cast<float>(image->width()) / static_cast<float>(image->height()),
      nearZ, farZ);
  auto modelMatrix = Matrix3D::MakeRotate({0.f, 0.f, 1.f}, 45.f);
  // Rotate around the ZXY axes of the model coordinate system in sequence; the latest transformation
  // in the model coordinate system needs to be placed at the far right of the matrix multiplication
  // equation
  modelMatrix.preRotate({1.f, 0.f, 0.f}, 45.f);
  modelMatrix.preRotate({0.f, 1.f, 0.f}, 45.f);
  // Use Z-axis translation to simulate model depth
  modelMatrix.postTranslate(0.f, 0.f, -10.f / imageSize.width);
  auto standardTransform = invOffsetToAnchorMatrix * standardvViewportMatrix * perspectiveMatrix *
                           viewMatrix * modelMatrix * invStandardViewportMatrix *
                           offsetToAnchorMatrix;
  auto standardTransform3DFilter = ImageFilter::Transform3D(standardTransform);

  // Test scale drawing with standard perspective type.
  {
    canvas->save();
    canvas->clear();

    auto filteredImage = image->makeWithFilter(standardTransform3DFilter);
    canvas->setMatrix(Matrix::MakeScale(0.5f, 0.5f));
    canvas->drawImage(filteredImage, 45.f, 45.f, {});

    context->flushAndSubmit();
    EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/Transorm3DImageFilterStandardScale"));
    canvas->restore();
  }

  // Test basic drawing with standard perspective type.
  {
    canvas->save();
    canvas->clear();

    Paint paint = {};
    paint.setImageFilter(standardTransform3DFilter);
    canvas->drawImage(image, 45.f, 45.f, &paint);

    context->flushAndSubmit();
    EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/Transorm3DImageFilterStandardBasic"));
    canvas->restore();
  }

  // Test image clipping drawing with standard perspective type.
  {
    canvas->save();
    canvas->clear();

    auto filteredImage = image->makeWithFilter(standardTransform3DFilter);
    auto filteredBounds = standardTransform3DFilter->filterBounds(
        Rect::MakeWH(static_cast<float>(image->width()), static_cast<float>(image->height())));

    auto clipRectLT = Rect::MakeXYWH(filteredBounds.left, filteredBounds.top,
                                     filteredBounds.width() * 0.5f, filteredBounds.height() * 0.5f);
    auto imageLT = image->makeWithFilter(standardTransform3DFilter, nullptr, &clipRectLT);
    canvas->drawImage(imageLT, 0.f, 0.f);

    auto clipRectRT = Rect::MakeXYWH(clipRectLT.right, filteredBounds.top,
                                     filteredBounds.width() * 0.5f, clipRectLT.height());
    auto imageRT = image->makeWithFilter(standardTransform3DFilter, nullptr, &clipRectRT);
    canvas->drawImage(imageRT, static_cast<float>(imageLT->width()), 0.f);

    auto clipRectLB = Rect::MakeXYWH(filteredBounds.left, clipRectLT.bottom, clipRectLT.width(),
                                     filteredBounds.height() * 0.5f);
    auto imageLB = image->makeWithFilter(standardTransform3DFilter, nullptr, &clipRectLB);
    canvas->drawImage(imageLB, 0.f, static_cast<float>(imageLT->height()));

    auto clipRectRB =
        Rect::MakeXYWH(clipRectRT.left, clipRectRT.bottom, clipRectRT.width(), clipRectLB.height());
    auto imageRB = image->makeWithFilter(standardTransform3DFilter, nullptr, &clipRectRB);
    canvas->drawImage(imageRB, static_cast<float>(imageLT->width()),
                      static_cast<float>(imageLT->height()));

    context->flushAndSubmit();
    EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/Transform3DImageFilterStandardClip"));
    canvas->restore();
  }
}

TGFX_TEST(FilterTest, ReverseFilterBounds) {
  auto rect = Rect::MakeXYWH(0, 0, 100, 100);
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  EXPECT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto paint = Paint();
  canvas->translate(50, 50);
  canvas->clipRect(rect);
  Recorder recorder;

  auto blurFilter = ImageFilter::Blur(10.f, 10.f);
  auto dst = blurFilter->filterBounds(rect);
  auto src = blurFilter->filterBounds(dst, MapDirection::Reverse);
  EXPECT_TRUE(src == Rect::MakeXYWH(-40, -40, 180, 180));
  auto pictureCanvas = recorder.beginRecording();
  pictureCanvas->translate(-50, -50);
  pictureCanvas->clipRect(src);
  pictureCanvas->drawImage(image);
  auto picture = recorder.finishRecordingAsPicture();
  paint.setImageFilter(blurFilter);
  canvas->clear();
  canvas->drawPicture(picture, nullptr, &paint);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/ReverseFilterBounds_Blur"));

  auto dropShadowFilter = ImageFilter::DropShadowOnly(10.f, 10.f, 20.f, 20.f, Color::Black());
  dst = dropShadowFilter->filterBounds(rect);
  src = dropShadowFilter->filterBounds(dst, MapDirection::Reverse);
  EXPECT_TRUE(src == Rect::MakeXYWH(-80, -80, 260, 260));

  pictureCanvas = recorder.beginRecording();
  pictureCanvas->translate(-50, -50);
  pictureCanvas->clipRect(src);
  pictureCanvas->drawImage(image);
  picture = recorder.finishRecordingAsPicture();
  paint.setImageFilter(dropShadowFilter);
  canvas->clear();
  canvas->drawPicture(picture, nullptr, &paint);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/ReverseFilterBounds_dropShadowOnly"));

  auto colorFilter = ColorFilter::Blend(Color::Red(), BlendMode::Multiply);
  auto colorImageFilter = ImageFilter::ColorFilter(colorFilter);
  dst = colorImageFilter->filterBounds(rect);
  src = colorImageFilter->filterBounds(dst, MapDirection::Reverse);
  EXPECT_TRUE(rect == src);

  pictureCanvas = recorder.beginRecording();
  pictureCanvas->translate(-50, -50);
  pictureCanvas->clipRect(src);
  pictureCanvas->drawImage(image);
  picture = recorder.finishRecordingAsPicture();
  paint.setImageFilter(colorImageFilter);
  canvas->clear();
  canvas->drawPicture(picture, nullptr, &paint);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/ReverseFilterBounds_color"));

  auto innerShadowFilter = ImageFilter::InnerShadow(-10.f, -10.f, 5.f, 5.f, Color::White());
  dst = innerShadowFilter->filterBounds(rect);
  src = innerShadowFilter->filterBounds(dst, MapDirection::Reverse);
  EXPECT_TRUE(rect == src);

  pictureCanvas = recorder.beginRecording();
  pictureCanvas->translate(-50, -50);
  pictureCanvas->clipRect(src);
  pictureCanvas->drawImage(image);
  picture = recorder.finishRecordingAsPicture();
  paint.setImageFilter(innerShadowFilter);
  canvas->clear();
  canvas->drawPicture(picture, nullptr, &paint);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/ReverseFilterBounds_inner"));

  auto composeFilter =
      ImageFilter::Compose({blurFilter, dropShadowFilter, innerShadowFilter, colorImageFilter});
  dst = composeFilter->filterBounds(rect);
  src = composeFilter->filterBounds(dst, MapDirection::Reverse);
  EXPECT_TRUE(src == Rect::MakeXYWH(-120, -120, 340, 340));

  pictureCanvas = recorder.beginRecording();
  pictureCanvas->translate(-50, -50);
  pictureCanvas->clipRect(src);
  pictureCanvas->drawImage(image);
  picture = recorder.finishRecordingAsPicture();
  paint.setImageFilter(composeFilter);
  canvas->clear();
  canvas->drawPicture(picture, nullptr, &paint);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/ReverseFilterBounds_compose"));

  auto dropShadowFilter2 = ImageFilter::DropShadow(10.f, 10.f, 0, 0, Color::Black());
  dst = dropShadowFilter2->filterBounds(rect);
  src = dropShadowFilter2->filterBounds(dst, MapDirection::Reverse);
  EXPECT_TRUE(src == Rect::MakeXYWH(-10.f, -10.f, 120.f, 120.f));

  pictureCanvas = recorder.beginRecording();
  pictureCanvas->translate(-50, -50);
  pictureCanvas->clipRect(src);
  pictureCanvas->drawImage(image);
  picture = recorder.finishRecordingAsPicture();
  paint.setImageFilter(dropShadowFilter2);
  canvas->clear();
  canvas->drawPicture(picture, nullptr, &paint);
  EXPECT_TRUE(Baseline::Compare(surface, "FilterTest/ReverseFilterBounds_dropShadow"));
}
}  // namespace tgfx

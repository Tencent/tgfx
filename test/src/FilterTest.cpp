/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "core/filters/BlurImageFilter.h"
#include "core/filters/ColorImageFilter.h"
#include "core/filters/DropShadowImageFilter.h"
#include "core/filters/InnerShadowImageFilter.h"
#include "core/shaders/GradientShader.h"
#include "core/shaders/ImageShader.h"
#include "core/vectors/freetype/FTMask.h"
#include "gpu/opengl/GLUtil.h"
#include "gtest/gtest.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/GradientType.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Mask.h"
#include "tgfx/core/PathEffect.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Size.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/gpu/RuntimeEffect.h"
#include "tgfx/gpu/opengl/GLFunctions.h"
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
  image = image->makeScaled(0.25f);
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
  paint.setImageFilter(ImageFilter::Blur(130, 130, TileMode::Decal));
  canvas->drawImage(image, &paint);
  canvas->restore();
  paint.setImageFilter(nullptr);
  canvas->drawPath(path, paint);
  paint.setImageFilter(nullptr);

  canvas->concat(Matrix::MakeTrans(-imageWidth - padding, imageHeight + padding));
  canvas->save();
  canvas->concat(imageMatrix);
  Point filterOffset = Point::Zero();
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
  paint.setImageFilter(ImageFilter::Blur(15, 15));
  canvas->drawImage(image, &paint);

  canvas->concat(Matrix::MakeTrans(imageWidth + padding, 0));
  paint.setImageFilter(ImageFilter::DropShadowOnly(0, 0, 15, 15, Color::White()));
  canvas->drawImage(image, &paint);

  canvas->concat(Matrix::MakeTrans(-imageWidth - padding, imageWidth + padding));
  paint.setImageFilter(ImageFilter::DropShadow(0, 0, 15, 15, Color::White()));
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

TGFX_TEST(FilterTest, ImageFilterShader) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/assets/bridge.jpg");
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, 720, 720);
  auto canvas = surface->getCanvas();
  image = image->makeMipmapped(true);
  auto filter = ImageFilter::DropShadow(0, 0, 300, 300, Color::Black());
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
  auto blackFilter = ImageFilter::DropShadow(0, 0, 300, 300, Color::Black());
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
  image = image->makeScaled(0.5f, SamplingOptions(FilterMode::Linear, MipmapMode::Linear));
  image = image->makeMipmapped(true);
  auto effect = CornerPinEffect::Make({484, 54}, {764, 80}, {764, 504}, {482, 512});
  auto filter = ImageFilter::Runtime(std::move(effect));
  image = image->makeWithFilter(std::move(filter));
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
    auto blurFilter = std::static_pointer_cast<BlurImageFilter>(imageFilter);
    Size blurSize = blurFilter->filterBounds(Rect::MakeEmpty()).size();
    EXPECT_EQ(blurSize.width, 18);
    EXPECT_EQ(blurSize.height, 36);
  }

  {
    auto imageFilter = ImageFilter::DropShadow(15.f, 15.f, 20.f, 30.f, Color::White());
    EXPECT_EQ(imageFilter->type(), ImageFilter::Type::DropShadow);
    auto dropShadowFilter = std::static_pointer_cast<const DropShadowImageFilter>(imageFilter);
    Size blurSize = dropShadowFilter->blurFilter->filterBounds(Rect::MakeEmpty()).size();
    EXPECT_EQ(blurSize.width, 18);
    EXPECT_EQ(blurSize.height, 36);
    EXPECT_EQ(dropShadowFilter->dx, 15.f);
    EXPECT_EQ(dropShadowFilter->dy, 15.f);
    EXPECT_EQ(dropShadowFilter->color, Color::White());
    EXPECT_EQ(dropShadowFilter->shadowOnly, false);
  }

  {
    auto imageFilter = ImageFilter::DropShadowOnly(15.f, 15.f, 20.f, 30.f, Color::White());
    EXPECT_EQ(imageFilter->type(), ImageFilter::Type::DropShadow);
    auto dropShadowFilter = std::static_pointer_cast<const DropShadowImageFilter>(imageFilter);
    Size blurSize = dropShadowFilter->blurFilter->filterBounds(Rect::MakeEmpty()).size();
    EXPECT_EQ(blurSize.width, 18);
    EXPECT_EQ(blurSize.height, 36);
    EXPECT_EQ(dropShadowFilter->dx, 15.f);
    EXPECT_EQ(dropShadowFilter->dy, 15.f);
    EXPECT_EQ(dropShadowFilter->color, Color::White());
    EXPECT_EQ(dropShadowFilter->shadowOnly, true);
  }

  {
    auto imageFilter = ImageFilter::InnerShadow(15.f, 15.f, 20.f, 30.f, Color::White());
    EXPECT_EQ(imageFilter->type(), ImageFilter::Type::InnerShadow);
    auto innerShadowFilter = std::static_pointer_cast<InnerShadowImageFilter>(imageFilter);
    Size blurSize = innerShadowFilter->blurFilter->filterBounds(Rect::MakeEmpty()).size();
    EXPECT_EQ(blurSize.width, 18);
    EXPECT_EQ(blurSize.height, 36);
    EXPECT_EQ(innerShadowFilter->dx, 15.f);
    EXPECT_EQ(innerShadowFilter->dy, 15.f);
    EXPECT_EQ(innerShadowFilter->color, Color::White());
    EXPECT_EQ(innerShadowFilter->shadowOnly, false);
  }

  {
    auto imageFilter = ImageFilter::InnerShadowOnly(15.f, 15.f, 20.f, 30.f, Color::White());
    EXPECT_EQ(imageFilter->type(), ImageFilter::Type::InnerShadow);
    auto innerShadowFilter = std::static_pointer_cast<InnerShadowImageFilter>(imageFilter);
    Size blurSize = innerShadowFilter->blurFilter->filterBounds(Rect::MakeEmpty()).size();
    EXPECT_EQ(blurSize.width, 18);
    EXPECT_EQ(blurSize.height, 36);
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

    Color color = Color::White();
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
}  // namespace tgfx

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
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "core/utils/Types.h"
#include "tgfx/core/ImageInfo.h"
#include "tgfx/core/Surface.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/ShapeStyle.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/TextLayer.h"
#include "tgfx/layers/filters/BlurFilter.h"
#include "tgfx/layers/filters/DropShadowFilter.h"
#include "tgfx/layers/filters/NoiseFilter.h"
#include "tgfx/layers/filters/TextureFilter.h"
#include "tgfx/layers/layerstyles/DropShadowStyle.h"
#include "tgfx/layers/layerstyles/GlassStyle.h"
#include "utils/TestUtils.h"

namespace tgfx {

namespace {

constexpr int SurfaceSize = 520;
constexpr float ImageScale = 0.22f;
constexpr float ImageOffset = 50.0f;
constexpr float MaskLeft = 120.0f;
constexpr float MaskTop = 120.0f;
constexpr float MaskSize = 220.0f;

struct RenderedScene {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels = {};
};

struct DiffMetrics {
  double mean = 0.0;
  int max = 0;
  double fractionOverFour = 0.0;
};

DiffMetrics CompareScenes(const RenderedScene& first, const RenderedScene& second) {
  DiffMetrics result = {};
  if (first.width != second.width || first.height != second.height ||
      first.pixels.size() != second.pixels.size() || first.pixels.empty()) {
    result.mean = 255.0;
    result.max = 255;
    result.fractionOverFour = 1.0;
    return result;
  }
  uint64_t total = 0;
  uint64_t pixelsOverFour = 0;
  auto pixelCount = static_cast<uint64_t>(first.width) * static_cast<uint64_t>(first.height);
  for (uint64_t pixel = 0; pixel < pixelCount; ++pixel) {
    int maxDifference = 0;
    for (size_t channel = 0; channel < 4; ++channel) {
      auto index = pixel * 4 + channel;
      auto difference =
          std::abs(static_cast<int>(first.pixels[index]) - static_cast<int>(second.pixels[index]));
      total += static_cast<uint64_t>(difference);
      maxDifference = std::max(maxDifference, difference);
    }
    result.max = std::max(result.max, maxDifference);
    if (maxDifference > 4) {
      pixelsOverFour++;
    }
  }
  result.mean = static_cast<double>(total) / static_cast<double>(pixelCount * 4);
  result.fractionOverFour = static_cast<double>(pixelsOverFour) / static_cast<double>(pixelCount);
  return result;
}

Rect ChangedPixelBounds(const RenderedScene& first, const RenderedScene& second) {
  Rect bounds = Rect::MakeEmpty();
  for (int y = 0; y < first.height; ++y) {
    for (int x = 0; x < first.width; ++x) {
      auto offset = (static_cast<size_t>(y) * static_cast<size_t>(first.width) +
                     static_cast<size_t>(x)) *
                    4;
      for (size_t channel = 0; channel < 4; ++channel) {
        if (std::abs(static_cast<int>(first.pixels[offset + channel]) -
                     static_cast<int>(second.pixels[offset + channel])) > 4) {
          bounds.join(Rect::MakeXYWH(static_cast<float>(x), static_cast<float>(y), 1.0f, 1.0f));
          break;
        }
      }
    }
  }
  return bounds;
}

bool HasRenderedContent(const RenderedScene& scene) {
  if (scene.width <= 0 || scene.height <= 0 ||
      scene.pixels.size() !=
          static_cast<size_t>(scene.width) * static_cast<size_t>(scene.height) * 4) {
    return false;
  }
  int differingPixels = 0;
  for (size_t offset = 0; offset < scene.pixels.size(); offset += 4) {
    for (size_t channel = 0; channel < 4; ++channel) {
      if (std::abs(static_cast<int>(scene.pixels[offset + channel]) -
                   static_cast<int>(scene.pixels[channel])) > 4) {
        differingPixels++;
        break;
      }
    }
  }
  return differingPixels > 16;
}

int MaxAlphaDifference(const RenderedScene& first, const RenderedScene& second) {
  if (first.width != second.width || first.height != second.height ||
      first.pixels.size() != second.pixels.size()) {
    return 255;
  }
  int maxDifference = 0;
  for (size_t offset = 3; offset < first.pixels.size(); offset += 4) {
    maxDifference = std::max(maxDifference, std::abs(static_cast<int>(first.pixels[offset]) -
                                                     static_cast<int>(second.pixels[offset])));
  }
  return maxDifference;
}

using LayerFactory = std::function<std::shared_ptr<Layer>()>;

RenderedScene RenderLayerScene(Context* context, const LayerFactory& makeLayer,
                               const std::shared_ptr<LayerFilter>& filter) {
  constexpr int size = 320;
  RenderedScene result = {size, size, {}};
  result.pixels.resize(static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
  auto surface = Surface::Make(context, size, size);
  if (surface == nullptr) {
    return result;
  }
  DisplayList displayList;
  auto layer = makeLayer();
  if (filter != nullptr) {
    layer->setFilters({filter});
  }
  displayList.root()->addChild(layer);
  displayList.render(surface.get());
  auto info = ImageInfo::Make(size, size, ColorType::RGBA_8888, AlphaType::Premultiplied);
  if (!surface->readPixels(info, result.pixels.data())) {
    result.pixels.assign(result.pixels.size(), 0);
  }
  return result;
}

std::shared_ptr<TextureFilter> MakeTexture(bool clipToShape) {
  return TextureFilter::Make(25.0f, 25.0f, 20.0f, 3491, clipToShape);
}

std::shared_ptr<ShapeLayer> MakeCircleMask() {
  auto mask = ShapeLayer::Make();
  Path path = {};
  path.addOval(Rect::MakeXYWH(MaskLeft, MaskTop, MaskSize, MaskSize));
  mask->setPath(path);
  mask->setFillStyle(ShapeStyle::Make(Color::White()));
  return mask;
}

RenderedScene RenderScene(Context* context, const std::shared_ptr<Image>& image,
                          const std::vector<std::shared_ptr<LayerFilter>>& filters,
                          const std::vector<std::shared_ptr<LayerStyle>>& styles = {},
                          float layerAlpha = 1.0f, bool withMask = false,
                          BlendMode blendMode = BlendMode::SrcOver,
                          const Color& backgroundColor = Color::White(), float groupAlpha = 1.0f) {
  RenderedScene result = {SurfaceSize, SurfaceSize, {}};
  result.pixels.resize(static_cast<size_t>(SurfaceSize) * static_cast<size_t>(SurfaceSize) * 4);
  auto surface = Surface::Make(context, SurfaceSize, SurfaceSize);
  if (surface == nullptr) {
    return result;
  }

  DisplayList displayList;
  displayList.setBackgroundColor(backgroundColor);
  auto group = Layer::Make();
  displayList.root()->addChild(group);

  auto layer = ImageLayer::Make();
  layer->setImage(image);
  auto matrix = Matrix::MakeScale(ImageScale);
  matrix.postTranslate(ImageOffset, ImageOffset);
  layer->setMatrix(matrix);
  layer->setFilters(filters);
  layer->setLayerStyles(styles);
  layer->setAlpha(layerAlpha);
  layer->setBlendMode(blendMode);
  group->addChild(layer);

  if (withMask) {
    auto mask = MakeCircleMask();
    group->addChild(mask);
    layer->setMask(mask);
    layer->setMaskType(LayerMaskType::Alpha);
  }
  if (groupAlpha < 1.0f) {
    group->setAllowsGroupOpacity(true);
    group->setAlpha(groupAlpha);
  }
  displayList.render(surface.get());
  auto info =
      ImageInfo::Make(SurfaceSize, SurfaceSize, ColorType::RGBA_8888, AlphaType::Premultiplied);
  if (!surface->readPixels(info, result.pixels.data())) {
    result.pixels.assign(result.pixels.size(), 0);
  }
  return result;
}

std::shared_ptr<GlassStyle> MakeGlassStyle() {
  return GlassStyle::Make(70.0f, 50.0f, 20.0f, 25.0f, 20.0f, 135.0f, 60.0f);
}

RenderedScene RenderGlassScene(Context* context, const std::shared_ptr<Image>& background,
                               const std::vector<std::shared_ptr<LayerFilter>>& filters,
                               const std::vector<std::shared_ptr<LayerStyle>>& styles) {
  constexpr int glassSurfaceSize = 600;
  constexpr float glassWidth = 260.0f;
  constexpr float glassHeight = 180.0f;
  RenderedScene result = {glassSurfaceSize, glassSurfaceSize, {}};
  result.pixels.resize(static_cast<size_t>(glassSurfaceSize) *
                       static_cast<size_t>(glassSurfaceSize) * 4);
  auto surface = Surface::Make(context, glassSurfaceSize, glassSurfaceSize);
  if (surface == nullptr) {
    return result;
  }

  DisplayList displayList;
  auto backgroundLayer = ImageLayer::Make();
  backgroundLayer->setImage(background);
  backgroundLayer->setMatrix(Matrix::MakeScale(static_cast<float>(glassSurfaceSize) /
                                               static_cast<float>(background->width())));
  displayList.root()->addChild(backgroundLayer);

  auto glass = SolidLayer::Make();
  glass->setWidth(glassWidth);
  glass->setHeight(glassHeight);
  glass->setRadiusX(30.0f);
  glass->setRadiusY(30.0f);
  glass->setColor(Color::FromRGBA(255, 255, 255, 90));
  glass->setMatrix(Matrix::MakeTrans((static_cast<float>(glassSurfaceSize) - glassWidth) * 0.5f,
                                     (static_cast<float>(glassSurfaceSize) - glassHeight) * 0.5f));
  glass->setLayerStyles(styles);
  glass->setFilters(filters);
  displayList.root()->addChild(glass);

  displayList.render(surface.get());
  auto info = ImageInfo::Make(glassSurfaceSize, glassSurfaceSize, ColorType::RGBA_8888,
                              AlphaType::Premultiplied);
  if (!surface->readPixels(info, result.pixels.data())) {
    result.pixels.assign(result.pixels.size(), 0);
  }
  return result;
}

int CountAlphaOutsideMask(const RenderedScene& scene) {
  constexpr float centerX = MaskLeft + MaskSize * 0.5f;
  constexpr float centerY = MaskTop + MaskSize * 0.5f;
  constexpr float radius = MaskSize * 0.5f + 1.0f;
  int count = 0;
  for (int y = 0; y < scene.height; ++y) {
    for (int x = 0; x < scene.width; ++x) {
      auto deltaX = static_cast<float>(x) + 0.5f - centerX;
      auto deltaY = static_cast<float>(y) + 0.5f - centerY;
      auto offset =
          (static_cast<size_t>(y) * static_cast<size_t>(scene.width) + static_cast<size_t>(x)) * 4;
      if (deltaX * deltaX + deltaY * deltaY > radius * radius && scene.pixels[offset + 3] > 0) {
        count++;
      }
    }
  }
  return count;
}

}  // namespace

TGFX_TEST(TextureLayerIntegrationTest, ComplexImageAndFilterInteractions) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/assets/bridge.jpg");
  ASSERT_TRUE(image != nullptr);

  auto original = RenderScene(context, image, {});
  auto clipped = RenderScene(context, image, {MakeTexture(true)});
  auto unclipped = RenderScene(context, image, {MakeTexture(false)});

  auto textureThenShadow = RenderScene(
      context, image,
      {MakeTexture(false),
       DropShadowFilter::Make(18.0f, 18.0f, 8.0f, 8.0f, Color::FromRGBA(0, 0, 0, 170))});
  auto shadowThenTexture =
      RenderScene(context, image,
                  {DropShadowFilter::Make(18.0f, 18.0f, 8.0f, 8.0f, Color::FromRGBA(0, 0, 0, 170)),
                   MakeTexture(false)});

  auto textureThenBlur =
      RenderScene(context, image, {MakeTexture(false), BlurFilter::Make(5.0f, 5.0f)});
  auto blurThenTexture =
      RenderScene(context, image, {BlurFilter::Make(5.0f, 5.0f), MakeTexture(false)});

  auto textureThenNoise = RenderScene(
      context, image,
      {MakeTexture(false), NoiseFilter::MakeMono(8.0f, 0.45f, Color::FromRGBA(255, 255, 255, 80),
                                                 7.0f, BlendMode::Overlay)});

  auto withShadowStyle =
      RenderScene(context, image, {MakeTexture(true)},
                  {DropShadowStyle::Make(18.0f, 18.0f, 8.0f, 8.0f, Color::FromRGBA(0, 0, 0, 170))});

  auto withMask = RenderScene(context, image, {MakeTexture(false)}, {}, 1.0f, true,
                              BlendMode::SrcOver, Color::Transparent());

  auto withLayerOpacity = RenderScene(context, image, {MakeTexture(false)}, {}, 0.5f);
  auto withGroupOpacity = RenderScene(context, image, {MakeTexture(false)}, {}, 1.0f, false,
                                      BlendMode::SrcOver, Color::White(), 0.5f);

  auto withBlend = RenderScene(context, image, {MakeTexture(false)}, {}, 1.0f, false,
                               BlendMode::Screen, Color::FromRGBA(20, 35, 50));

  const std::vector<std::pair<std::string, RenderedScene*>> outputs = {
      {"layer-complex-original", &original},
      {"layer-complex-texture-clipped", &clipped},
      {"layer-complex-texture-unclipped", &unclipped},
      {"layer-order-texture-then-shadow", &textureThenShadow},
      {"layer-order-shadow-then-texture", &shadowThenTexture},
      {"layer-order-texture-then-blur", &textureThenBlur},
      {"layer-order-blur-then-texture", &blurThenTexture},
      {"layer-interaction-texture-then-noise", &textureThenNoise},
      {"layer-interaction-texture-with-shadow-style", &withShadowStyle},
      {"layer-interaction-texture-with-mask", &withMask},
      {"layer-interaction-texture-layer-opacity", &withLayerOpacity},
      {"layer-interaction-texture-group-opacity", &withGroupOpacity},
      {"layer-interaction-texture-blend-screen", &withBlend},
  };
  for (const auto& output : outputs) {
    EXPECT_TRUE(HasRenderedContent(*output.second)) << output.first;
  }

  auto textureDifference = CompareScenes(clipped, original);
  EXPECT_GT(textureDifference.mean, 1.0);
  auto clipDifference = CompareScenes(unclipped, clipped);
  EXPECT_GT(clipDifference.mean, 0.5);
  auto shadowOrderDifference = CompareScenes(textureThenShadow, shadowThenTexture);
  EXPECT_GT(shadowOrderDifference.mean, 0.05);
  auto blurOrderDifference = CompareScenes(textureThenBlur, blurThenTexture);
  EXPECT_GT(blurOrderDifference.mean, 0.1);
  auto noiseDifference = CompareScenes(textureThenNoise, unclipped);
  EXPECT_GT(noiseDifference.mean, 0.1);
  auto styleDifference = CompareScenes(withShadowStyle, clipped);
  EXPECT_GT(styleDifference.mean, 0.5);
  auto layerOpacityDifference = CompareScenes(withLayerOpacity, unclipped);
  EXPECT_GT(layerOpacityDifference.mean, 1.0);
  auto groupOpacityDifference = CompareScenes(withGroupOpacity, unclipped);
  EXPECT_GT(groupOpacityDifference.mean, 1.0);
  auto blendDifference = CompareScenes(withBlend, unclipped);
  EXPECT_GT(blendDifference.mean, 1.0);
  EXPECT_EQ(CountAlphaOutsideMask(withMask), 0);
}

TGFX_TEST(TextureLayerIntegrationTest, LayerAlphaSanity) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto renderAlpha = [&](float alpha) {
    auto surface = Surface::Make(context, 20, 20);
    EXPECT_TRUE(surface != nullptr);
    DisplayList displayList;
    displayList.setBackgroundColor(Color::White());
    auto layer = SolidLayer::Make();
    layer->setColor(Color::Red());
    layer->setWidth(20.0f);
    layer->setHeight(20.0f);
    layer->setAlpha(alpha);
    displayList.root()->addChild(layer);
    displayList.render(surface.get());
    std::array<uint8_t, 4> pixel = {0, 0, 0, 0};
    auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
    EXPECT_TRUE(surface->readPixels(info, pixel.data(), 10, 10));
    return pixel;
  };

  auto opaque = renderAlpha(1.0f);
  auto half = renderAlpha(0.5f);
  EXPECT_GT(half[1], opaque[1]);
  EXPECT_GT(half[2], opaque[2]);

  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_TRUE(image != nullptr);
  auto renderImageAlpha = [&](float alpha, const std::shared_ptr<LayerFilter>& filter) {
    auto surface = Surface::Make(context, 20, 20);
    EXPECT_TRUE(surface != nullptr);
    DisplayList displayList;
    displayList.setBackgroundColor(Color::White());
    auto layer = ImageLayer::Make();
    layer->setImage(image);
    layer->setMatrix(Matrix::MakeScale(20.0f / static_cast<float>(image->width())));
    layer->setAlpha(alpha);
    if (filter) {
      layer->setFilters({filter});
    }
    displayList.root()->addChild(layer);
    displayList.render(surface.get());
    std::array<uint8_t, 4> pixel = {0, 0, 0, 0};
    auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
    EXPECT_TRUE(surface->readPixels(info, pixel.data(), 10, 10));
    return pixel;
  };
  auto opaqueImage = renderImageAlpha(1.0f, nullptr);
  auto halfImage = renderImageAlpha(0.5f, nullptr);
  EXPECT_GT(halfImage[0] + halfImage[1] + halfImage[2],
            opaqueImage[0] + opaqueImage[1] + opaqueImage[2]);
  auto opaqueTextureImage =
      renderImageAlpha(1.0f, TextureFilter::Make(16.0f, 16.0f, 2.0f, 1, true));
  auto halfTextureImage = renderImageAlpha(0.5f, TextureFilter::Make(16.0f, 16.0f, 2.0f, 1, true));
  EXPECT_GT(halfTextureImage[0] + halfTextureImage[1] + halfTextureImage[2],
            opaqueTextureImage[0] + opaqueTextureImage[1] + opaqueTextureImage[2]);

  auto opaqueBlurImage = renderImageAlpha(1.0f, BlurFilter::Make(2.0f, 2.0f));
  auto halfBlurImage = renderImageAlpha(0.5f, BlurFilter::Make(2.0f, 2.0f));
  EXPECT_GT(halfBlurImage[0] + halfBlurImage[1] + halfBlurImage[2],
            opaqueBlurImage[0] + opaqueBlurImage[1] + opaqueBlurImage[2]);
}

TGFX_TEST(TextureLayerIntegrationTest, FilterBoundsScaleAndClip) {
  auto srcRect = Rect::MakeWH(100.0f, 80.0f);
  auto unclipped = MakeTexture(false);
  auto clipped = MakeTexture(true);
  auto unclippedBounds = unclipped->filterBounds(srcRect, 1.0f);
  auto unclippedBounds2x = unclipped->filterBounds(srcRect, 2.0f);
  auto clippedBounds = clipped->filterBounds(srcRect, 2.0f);

  EXPECT_GT(unclippedBounds.width(), srcRect.width());
  EXPECT_GT(unclippedBounds.height(), srcRect.height());
  EXPECT_GT(unclippedBounds2x.width(), unclippedBounds.width());
  EXPECT_GT(unclippedBounds2x.height(), unclippedBounds.height());
  EXPECT_EQ(clippedBounds, unclippedBounds2x);

  auto invalid = MakeTexture(false);
  invalid->setSizeX(std::numeric_limits<float>::infinity());
  invalid->setSizeY(std::numeric_limits<float>::quiet_NaN());
  invalid->setRadius(std::numeric_limits<float>::quiet_NaN());
  EXPECT_EQ(invalid->sizeX(), 25.0f);
  EXPECT_EQ(invalid->sizeY(), 25.0f);
  EXPECT_EQ(invalid->radius(), 20.0f);
}

TGFX_TEST(TextureLayerIntegrationTest, GlassAndTextureInteractions) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto background = MakeImage("resources/assets/bridge.jpg");
  ASSERT_TRUE(background != nullptr);

  auto glassOnly = RenderGlassScene(context, background, {}, {MakeGlassStyle()});
  auto glassTextureClipped =
      RenderGlassScene(context, background, {MakeTexture(true)}, {MakeGlassStyle()});
  auto glassTextureUnclipped =
      RenderGlassScene(context, background, {MakeTexture(false)}, {MakeGlassStyle()});
  auto glassTextureNoise = RenderGlassScene(
      context, background,
      {MakeTexture(false), NoiseFilter::MakeMono(8.0f, 0.45f, Color::FromRGBA(255, 255, 255, 80),
                                                 11.0f, BlendMode::Overlay)},
      {MakeGlassStyle()});
  auto glassTextureShadowFilterAfter = RenderGlassScene(
      context, background,
      {MakeTexture(false),
       DropShadowFilter::Make(18.0f, 18.0f, 8.0f, 8.0f, Color::FromRGBA(0, 0, 0, 180))},
      {MakeGlassStyle()});
  auto glassTextureShadowFilterBefore = RenderGlassScene(
      context, background,
      {DropShadowFilter::Make(18.0f, 18.0f, 8.0f, 8.0f, Color::FromRGBA(0, 0, 0, 180)),
       MakeTexture(false)},
      {MakeGlassStyle()});
  auto glassTextureShadowStyle = RenderGlassScene(
      context, background, {MakeTexture(true)},
      {MakeGlassStyle(),
       DropShadowStyle::Make(18.0f, 18.0f, 8.0f, 8.0f, Color::FromRGBA(0, 0, 0, 180))});
  auto spreadShadowStyle =
      DropShadowStyle::Make(18.0f, 18.0f, 8.0f, 8.0f, Color::FromRGBA(0, 0, 0, 180));
  spreadShadowStyle->setSpread(12.0f);
  auto glassTextureShadowSpread = RenderGlassScene(context, background, {MakeTexture(false)},
                                                   {MakeGlassStyle(), spreadShadowStyle});
  auto shadowStyleTexture = RenderGlassScene(
      context, background, {MakeTexture(true)},
      {DropShadowStyle::Make(18.0f, 18.0f, 8.0f, 8.0f, Color::FromRGBA(0, 0, 0, 180)),
       MakeGlassStyle()});

  const std::vector<std::pair<std::string, RenderedScene*>> outputs = {
      {"glass-texture-glass-only", &glassOnly},
      {"glass-texture-clipped", &glassTextureClipped},
      {"glass-texture-unclipped", &glassTextureUnclipped},
      {"glass-texture-noise", &glassTextureNoise},
      {"glass-texture-shadow-filter-after", &glassTextureShadowFilterAfter},
      {"glass-texture-shadow-filter-before", &glassTextureShadowFilterBefore},
      {"glass-texture-shadow-style", &glassTextureShadowStyle},
      {"glass-texture-shadow-style-spread", &glassTextureShadowSpread},
      {"glass-texture-shadow-style-before", &shadowStyleTexture},
  };
  for (const auto& output : outputs) {
    EXPECT_TRUE(HasRenderedContent(*output.second)) << output.first;
  }

  auto textureDifference = CompareScenes(glassTextureClipped, glassOnly);
  auto clipDifference = CompareScenes(glassTextureUnclipped, glassTextureClipped);
  auto noiseDifference = CompareScenes(glassTextureNoise, glassTextureUnclipped);
  auto shadowFilterOrderDifference =
      CompareScenes(glassTextureShadowFilterAfter, glassTextureShadowFilterBefore);
  auto shadowStyleOrderDifference = CompareScenes(glassTextureShadowStyle, shadowStyleTexture);
  auto shadowSpreadDifference = CompareScenes(glassTextureShadowSpread, glassTextureUnclipped);
  EXPECT_GT(textureDifference.mean, 0.05);
  EXPECT_GT(clipDifference.mean, 0.2);
  EXPECT_GT(noiseDifference.mean, 0.05);
  EXPECT_GT(shadowFilterOrderDifference.mean, 0.05);
  EXPECT_GT(shadowStyleOrderDifference.mean, 0.05);
  EXPECT_GT(shadowSpreadDifference.mean, 0.05);
}

TGFX_TEST(TextureLayerIntegrationTest, PublicFilterContract) {
  auto filter = TextureFilter::Make(25.0f, 18.0f, 12.0f, 3491, true);
  ASSERT_TRUE(filter != nullptr);
  EXPECT_EQ(filter->sizeX(), 25.0f);
  EXPECT_EQ(filter->sizeY(), 18.0f);
  EXPECT_EQ(filter->radius(), 12.0f);
  EXPECT_EQ(filter->seed(), 3491U);
  EXPECT_TRUE(filter->clipToShape());
  EXPECT_EQ(Types::Get(filter.get()), Types::LayerFilterType::TextureFilter);

  filter->setSizeX(32.0f);
  filter->setSizeY(24.0f);
  filter->setRadius(16.0f);
  filter->setSeed(7);
  filter->setClipToShape(false);
  EXPECT_EQ(filter->sizeX(), 32.0f);
  EXPECT_EQ(filter->sizeY(), 24.0f);
  EXPECT_EQ(filter->radius(), 16.0f);
  EXPECT_EQ(filter->seed(), 7U);
  EXPECT_FALSE(filter->clipToShape());
  filter->setSeed(0xF1234567U);
  EXPECT_EQ(filter->seed(), 0xF1234567U);

  filter->setSizeX(0.0f);
  filter->setSizeY(std::numeric_limits<float>::infinity());
  filter->setRadius(-1.0f);
  filter->setRadius(std::numeric_limits<float>::quiet_NaN());
  EXPECT_EQ(filter->sizeX(), 32.0f);
  EXPECT_EQ(filter->sizeY(), 24.0f);
  EXPECT_EQ(filter->radius(), 16.0f);

  auto zeroRadius = TextureFilter::Make(25.0f, 25.0f, 0.0f);
  ASSERT_TRUE(zeroRadius != nullptr);
  auto srcRect = Rect::MakeXYWH(10.0f, 20.0f, 100.0f, 80.0f);
  EXPECT_EQ(zeroRadius->filterBounds(srcRect, 1.0f), srcRect);

  auto expandedBounds = filter->filterBounds(srcRect, 2.0f);
  EXPECT_EQ(filter->filterBounds(srcRect, 2.0f, MapDirection::Reverse), expandedBounds);
  EXPECT_GT(expandedBounds.width(), srcRect.width());
  EXPECT_GT(expandedBounds.height(), srcRect.height());

  EXPECT_TRUE(TextureFilter::Make(-1.0f, 10.0f, 4.0f) == nullptr);
  EXPECT_TRUE(TextureFilter::Make(10.0f, 10.0f, -1.0f) == nullptr);
  EXPECT_TRUE(TextureFilter::Make(std::numeric_limits<float>::quiet_NaN(), 10.0f, 4.0f) == nullptr);
}

TGFX_TEST(TextureLayerIntegrationTest, LayerTypeCompatibility) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/assets/bridge.jpg");
  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(image != nullptr);
  ASSERT_TRUE(typeface != nullptr);

  std::vector<std::pair<std::string, LayerFactory>> layerFactories = {};
  layerFactories.emplace_back("solid", [] {
    auto layer = SolidLayer::Make();
    layer->setWidth(140.0f);
    layer->setHeight(90.0f);
    layer->setColor(Color::Red());
    layer->setMatrix(Matrix::MakeTrans(90.0f, 115.0f));
    return std::static_pointer_cast<Layer>(layer);
  });
  layerFactories.emplace_back("shape", [] {
    auto layer = ShapeLayer::Make();
    Path path = {};
    path.addOval(Rect::MakeWH(150.0f, 100.0f));
    layer->setPath(path);
    layer->setFillStyle(ShapeStyle::Make(Color::Green()));
    layer->setMatrix(Matrix::MakeTrans(85.0f, 110.0f));
    return std::static_pointer_cast<Layer>(layer);
  });
  layerFactories.emplace_back("image", [image] {
    auto layer = ImageLayer::Make();
    layer->setImage(image);
    layer->setMatrix(Matrix::MakeScale(0.12f));
    return std::static_pointer_cast<Layer>(layer);
  });
  layerFactories.emplace_back("text", [typeface] {
    auto layer = TextLayer::Make();
    layer->setText("Texture");
    layer->setTextColor(Color::Blue());
    layer->setFont(Font(typeface, 52.0f));
    layer->setMatrix(Matrix::MakeTrans(40.0f, 120.0f));
    return std::static_pointer_cast<Layer>(layer);
  });
  layerFactories.emplace_back("group", [] {
    auto group = Layer::Make();
    auto child = SolidLayer::Make();
    child->setWidth(130.0f);
    child->setHeight(90.0f);
    child->setColor(Color::FromRGBA(240, 150, 40));
    child->setMatrix(Matrix::MakeTrans(10.0f, 10.0f));
    group->addChild(child);
    group->setMatrix(Matrix::MakeTrans(85.0f, 105.0f));
    return group;
  });

  for (const auto& entry : layerFactories) {
    auto original = RenderLayerScene(context, entry.second, nullptr);
    EXPECT_TRUE(HasRenderedContent(original)) << entry.first;
    auto clipped = RenderLayerScene(context, entry.second,
                                    TextureFilter::Make(16.0f, 16.0f, 8.0f, 3491, true));
    auto unclipped = RenderLayerScene(context, entry.second,
                                      TextureFilter::Make(16.0f, 16.0f, 8.0f, 3491, false));
    EXPECT_LE(MaxAlphaDifference(original, clipped), 2) << entry.first;
    EXPECT_GT(MaxAlphaDifference(original, unclipped), 0) << entry.first;
    EXPECT_GT(CompareScenes(original, unclipped).mean, 0.001) << entry.first;
  }
}

TGFX_TEST(TextureLayerIntegrationTest, ContentOriginAnchorStability) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto makeLayer = [](bool offsetPath) {
    auto layer = ShapeLayer::Make();
    Path path = {};
    path.addRect(Rect::MakeXYWH(offsetPath ? 60.0f : 0.0f, 0.0f, 120.0f, 90.0f));
    layer->setPath(path);
    layer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(60, 120, 200)));
    layer->setMatrix(Matrix::MakeTrans(offsetPath ? 0.0f : 60.0f, 70.0f));
    return std::static_pointer_cast<Layer>(layer);
  };
  auto filter = [] { return TextureFilter::Make(16.0f, 12.0f, 8.0f, 3491, false); };

  auto offsetContent = RenderLayerScene(
      context, [makeLayer] { return makeLayer(true); }, filter());
  auto transformedContent = RenderLayerScene(
      context, [makeLayer] { return makeLayer(false); }, filter());
  auto difference = CompareScenes(offsetContent, transformedContent);
  EXPECT_LE(difference.mean, 0.1);
  EXPECT_LE(difference.fractionOverFour, 0.0005);
  EXPECT_LE(difference.max, 8);
}

TGFX_TEST(TextureLayerIntegrationTest, ParameterResponse) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/assets/bridge.jpg");
  ASSERT_TRUE(image != nullptr);

  auto makeLayer = [image] {
    auto layer = ImageLayer::Make();
    layer->setImage(image);
    layer->setMatrix(Matrix::MakeScale(0.16f));
    return std::static_pointer_cast<Layer>(layer);
  };
  auto render = [&](float sizeX, float sizeY, uint32_t seed, bool clipToShape) {
    return RenderLayerScene(context, makeLayer,
                            TextureFilter::Make(sizeX, sizeY, 8.0f, seed, clipToShape));
  };

  auto base = render(16.0f, 16.0f, 3491, false);
  ASSERT_TRUE(HasRenderedContent(base));
  auto changedSize = render(24.0f, 12.0f, 3491, false);
  auto changedSeed = render(16.0f, 16.0f, 7, false);
  auto clipped = render(16.0f, 16.0f, 3491, true);
  EXPECT_GT(CompareScenes(base, changedSize).mean, 0.01);
  EXPECT_GT(CompareScenes(base, changedSeed).mean, 0.01);
  EXPECT_GT(CompareScenes(base, clipped).mean, 0.01);
}

TGFX_TEST(TextureLayerIntegrationTest, RenderModesAndCacheInvalidation) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_TRUE(image != nullptr);

  auto render = [&](RenderMode mode, float filterSize, bool mutate, bool secondAutoClear = false) {
    constexpr int size = 320;
    RenderedScene result = {size, size, {}};
    result.pixels.resize(static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
    auto surface = Surface::Make(context, size, size);
    if (surface == nullptr) {
      return result;
    }
    DisplayList displayList;
    displayList.setRenderMode(mode);
    displayList.setBackgroundColor(Color::White());
    displayList.setZoomScale(1.25f);
    displayList.setContentOffset(20.0f, 15.0f);
    if (mode == RenderMode::Tiled) {
      displayList.setTileSize(128);
      displayList.setMaxTileCount(64);
    }
    auto filter = TextureFilter::Make(filterSize, 16.0f, 6.0f, 3491, false);
    auto layer = SolidLayer::Make();
    layer->setWidth(170.0f);
    layer->setHeight(110.0f);
    layer->setColor(Color::FromRGBA(40, 120, 210));
    layer->setMatrix(Matrix::MakeTrans(75.0f, 105.0f));
    layer->setFilters({filter});
    displayList.root()->addChild(layer);
    displayList.render(surface.get());

    if (mutate) {
      filter->setSizeX(24.0f);
      displayList.render(surface.get(), secondAutoClear);
    }

    auto info = ImageInfo::Make(size, size, ColorType::RGBA_8888, AlphaType::Premultiplied);
    if (!surface->readPixels(info, result.pixels.data())) {
      result.pixels.assign(result.pixels.size(), 0);
    }
    return result;
  };

  auto directInitial = render(RenderMode::Direct, 16.0f, false);
  auto partialInitial = render(RenderMode::Partial, 16.0f, false);
  auto tiledInitial = render(RenderMode::Tiled, 16.0f, false);
  auto direct = render(RenderMode::Direct, 16.0f, true);
  auto partial = render(RenderMode::Partial, 16.0f, true);
  auto tiled = render(RenderMode::Tiled, 16.0f, true);
  auto tiledCleared = render(RenderMode::Tiled, 16.0f, true, true);
  auto tiledFresh = render(RenderMode::Tiled, 24.0f, false);
  auto renderFinal = [&] {
    constexpr int size = 320;
    RenderedScene result = {size, size, {}};
    result.pixels.resize(static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
    auto surface = Surface::Make(context, size, size);
    if (surface == nullptr) {
      return result;
    }
    DisplayList displayList;
    displayList.setRenderMode(RenderMode::Direct);
    displayList.setBackgroundColor(Color::White());
    displayList.setZoomScale(1.25f);
    displayList.setContentOffset(20.0f, 15.0f);
    auto filter = TextureFilter::Make(24.0f, 16.0f, 6.0f, 3491, false);
    auto layer = SolidLayer::Make();
    layer->setWidth(170.0f);
    layer->setHeight(110.0f);
    layer->setColor(Color::FromRGBA(40, 120, 210));
    layer->setMatrix(Matrix::MakeTrans(75.0f, 105.0f));
    layer->setFilters({filter});
    displayList.root()->addChild(layer);
    displayList.render(surface.get());
    auto info = ImageInfo::Make(size, size, ColorType::RGBA_8888, AlphaType::Premultiplied);
    if (!surface->readPixels(info, result.pixels.data())) {
      result.pixels.assign(result.pixels.size(), 0);
    }
    return result;
  };
  auto final = renderFinal();
  auto directFinalDifference = CompareScenes(direct, final);
  auto partialFinalDifference = CompareScenes(partial, final);
  auto tiledFinalDifference = CompareScenes(tiled, final);
  auto tiledFreshDifference = CompareScenes(tiledFresh, final);
  auto tiledClearedFreshDifference = CompareScenes(tiledCleared, tiledFresh);
  auto partialDifference = CompareScenes(direct, partial);
  auto tiledDifference = CompareScenes(direct, tiled);
  auto initialPartialDifference = CompareScenes(directInitial, partialInitial);
  auto initialTiledDifference = CompareScenes(directInitial, tiledInitial);
  auto directMutationDifference = CompareScenes(directInitial, direct);
  auto tiledMutationDifference = CompareScenes(tiledInitial, tiled);
  EXPECT_LE(initialPartialDifference.mean, 0.1);
  EXPECT_LE(initialTiledDifference.mean, 0.1);
  EXPECT_GT(directMutationDifference.mean, 0.01);
  EXPECT_GT(tiledMutationDifference.mean, 0.01);
  EXPECT_LE(directFinalDifference.mean, 0.1);
  EXPECT_LE(partialFinalDifference.mean, 0.1);
  EXPECT_LE(tiledFinalDifference.mean, 0.1);
  EXPECT_LE(directFinalDifference.fractionOverFour, 0.0005);
  EXPECT_LE(partialFinalDifference.fractionOverFour, 0.0005);
  EXPECT_LE(tiledFinalDifference.fractionOverFour, 0.0005);
  EXPECT_LE(directFinalDifference.max, 8);
  EXPECT_LE(partialFinalDifference.max, 8);
  EXPECT_LE(tiledFinalDifference.max, 8);
  EXPECT_LE(tiledFreshDifference.mean, 0.1);
  EXPECT_LE(tiledClearedFreshDifference.mean, 0.1);
  EXPECT_LE(partialDifference.mean, 0.1);
  EXPECT_LE(partialDifference.fractionOverFour, 0.0005);
  EXPECT_LE(partialDifference.max, 8);
  EXPECT_LE(tiledDifference.mean, 0.1);
  EXPECT_LE(tiledDifference.fractionOverFour, 0.0005);
  EXPECT_LE(tiledDifference.max, 8);
  auto tiledBounds = ChangedPixelBounds(tiled, tiledFresh);
  auto tiledClearedBounds = ChangedPixelBounds(tiledCleared, tiledFresh);
  EXPECT_TRUE(tiledBounds.isEmpty())
      << "tiled diff bounds=" << tiledBounds.left << "," << tiledBounds.top << " "
      << tiledBounds.right << "," << tiledBounds.bottom;
  EXPECT_TRUE(tiledClearedBounds.isEmpty())
      << "tiled cleared diff bounds=" << tiledClearedBounds.left << "," << tiledClearedBounds.top
      << " " << tiledClearedBounds.right << "," << tiledClearedBounds.bottom;

  auto renderCached = [&](int cacheSize, float initialSizeX, bool mutateFilter,
                          bool replaceFilter = false) {
    constexpr int size = 320;
    RenderedScene result = {size, size, {}};
    result.pixels.resize(static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
    auto surface = Surface::Make(context, size, size);
    if (surface == nullptr) {
      return result;
    }
    DisplayList displayList;
    displayList.setRenderMode(RenderMode::Direct);
    displayList.setSubtreeCacheMaxSize(cacheSize);
    displayList.setBackgroundColor(Color::White());
    auto filter = TextureFilter::Make(initialSizeX, 16.0f, 6.0f, 3491, true);
    auto layer = ImageLayer::Make();
    layer->setImage(image);
    layer->setMatrix(Matrix::MakeScale(170.0f / static_cast<float>(image->width())));
    layer->setFilters({filter});
    displayList.root()->addChild(layer);
    displayList.render(surface.get());
    displayList.render(surface.get());
    if (mutateFilter) {
      if (replaceFilter) {
        layer->setFilters({TextureFilter::Make(24.0f, 16.0f, 6.0f, 3491, true)});
      } else {
        filter->setSizeX(24.0f);
      }
    }
    displayList.render(surface.get());
    displayList.render(surface.get());
    auto info = ImageInfo::Make(size, size, ColorType::RGBA_8888, AlphaType::Premultiplied);
    if (!surface->readPixels(info, result.pixels.data())) {
      result.pixels.assign(result.pixels.size(), 0);
    }
    return result;
  };

  auto cachedStable = renderCached(204, 16.0f, false);
  auto cachedMutated = renderCached(204, 16.0f, true);
  auto cacheDifference = CompareScenes(cachedStable, cachedMutated);
  EXPECT_GT(cacheDifference.mean, 0.01);

  constexpr int cacheSurfaceSize = 320;
  RenderedScene freshMutated = {cacheSurfaceSize, cacheSurfaceSize, {}};
  freshMutated.pixels.resize(static_cast<size_t>(cacheSurfaceSize) *
                             static_cast<size_t>(cacheSurfaceSize) * 4);
  auto freshSurface = Surface::Make(context, cacheSurfaceSize, cacheSurfaceSize);
  ASSERT_TRUE(freshSurface != nullptr);
  DisplayList freshDisplayList;
  freshDisplayList.setRenderMode(RenderMode::Direct);
  freshDisplayList.setBackgroundColor(Color::White());
  auto freshFilter = TextureFilter::Make(24.0f, 16.0f, 6.0f, 3491, true);
  auto freshLayer = ImageLayer::Make();
  freshLayer->setImage(image);
  freshLayer->setMatrix(Matrix::MakeScale(170.0f / static_cast<float>(image->width())));
  freshLayer->setFilters({freshFilter});
  freshDisplayList.root()->addChild(freshLayer);
  freshDisplayList.render(freshSurface.get());
  freshDisplayList.render(freshSurface.get());
  auto freshInfo = ImageInfo::Make(cacheSurfaceSize, cacheSurfaceSize, ColorType::RGBA_8888,
                                   AlphaType::Premultiplied);
  ASSERT_TRUE(freshSurface->readPixels(freshInfo, freshMutated.pixels.data()));
  auto freshDifference = CompareScenes(cachedMutated, freshMutated);
  EXPECT_LE(freshDifference.mean, 0.1);
  EXPECT_LE(freshDifference.fractionOverFour, 0.0005);
  EXPECT_LE(freshDifference.max, 8);

  // A larger subtree cache may resample its rasterized result, so compare both results through the
  // same cache configuration.
  auto resampledStable = renderCached(256, 16.0f, false);
  auto resampledStableAgain = renderCached(256, 16.0f, false);
  auto resampledMutated = renderCached(256, 16.0f, true);
  auto resampledReplaced = renderCached(256, 16.0f, true, true);
  auto resampledFresh = renderCached(256, 24.0f, false);
  auto stableDifference = CompareScenes(resampledStable, resampledStableAgain);
  auto resampledMutationDifference = CompareScenes(resampledStable, resampledMutated);
  auto resampledFreshDifference = CompareScenes(resampledMutated, resampledFresh);
  auto resampledReplacedDifference = CompareScenes(resampledReplaced, resampledFresh);
  auto replacedMutationDifference = CompareScenes(resampledReplaced, resampledMutated);
  auto resampledFreshBounds = ChangedPixelBounds(resampledMutated, resampledFresh);
  auto resampledReplacedBounds = ChangedPixelBounds(resampledReplaced, resampledFresh);
  auto replacedMutationBounds = ChangedPixelBounds(resampledReplaced, resampledMutated);
  EXPECT_LE(stableDifference.mean, 0.1);
  EXPECT_LE(stableDifference.fractionOverFour, 0.0005);
  EXPECT_LE(stableDifference.max, 8);
  EXPECT_GT(resampledMutationDifference.mean, 0.01);
  EXPECT_LE(resampledFreshDifference.mean, 0.1)
      << "bounds=" << resampledFreshBounds.left << "," << resampledFreshBounds.top << " "
      << resampledFreshBounds.right << "," << resampledFreshBounds.bottom;
  EXPECT_LE(resampledFreshDifference.fractionOverFour, 0.0005);
  EXPECT_LE(resampledFreshDifference.max, 8);
  EXPECT_LE(resampledReplacedDifference.mean, 0.1)
      << "bounds=" << resampledReplacedBounds.left << "," << resampledReplacedBounds.top << " "
      << resampledReplacedBounds.right << "," << resampledReplacedBounds.bottom;
  EXPECT_LE(resampledReplacedDifference.fractionOverFour, 0.0005);
  EXPECT_LE(resampledReplacedDifference.max, 8);
  EXPECT_LE(replacedMutationDifference.mean, 0.1)
      << "bounds=" << replacedMutationBounds.left << "," << replacedMutationBounds.top << " "
      << replacedMutationBounds.right << "," << replacedMutationBounds.bottom;
  EXPECT_LE(replacedMutationDifference.fractionOverFour, 0.0005);
  EXPECT_LE(replacedMutationDifference.max, 8);
}

TGFX_TEST(TextureLayerIntegrationTest, TiledNoiseCacheControl) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto render = [&](RenderMode mode, float noiseSize, bool mutate) {
    constexpr int size = 320;
    RenderedScene result = {size, size, {}};
    result.pixels.resize(static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
    auto surface = Surface::Make(context, size, size);
    if (surface == nullptr) {
      return result;
    }
    DisplayList displayList;
    displayList.setRenderMode(mode);
    displayList.setBackgroundColor(Color::White());
    displayList.setZoomScale(1.25f);
    displayList.setContentOffset(20.0f, 15.0f);
    if (mode == RenderMode::Tiled) {
      displayList.setTileSize(128);
      displayList.setMaxTileCount(64);
    }
    auto filter =
        NoiseFilter::MakeMono(noiseSize, 0.65f, Color::FromRGBA(255, 255, 255, 180), 3491);
    auto layer = SolidLayer::Make();
    layer->setWidth(170.0f);
    layer->setHeight(110.0f);
    layer->setColor(Color::FromRGBA(40, 120, 210));
    layer->setMatrix(Matrix::MakeTrans(75.0f, 105.0f));
    layer->setFilters({filter});
    displayList.root()->addChild(layer);
    displayList.render(surface.get());
    if (mutate) {
      filter->setSize(24.0f);
      displayList.render(surface.get(), false);
    }
    auto info = ImageInfo::Make(size, size, ColorType::RGBA_8888, AlphaType::Premultiplied);
    if (!surface->readPixels(info, result.pixels.data())) {
      result.pixels.assign(result.pixels.size(), 0);
    }
    return result;
  };

  auto tiled = render(RenderMode::Tiled, 16.0f, true);
  auto tiledFresh = render(RenderMode::Tiled, 24.0f, false);
  auto difference = CompareScenes(tiled, tiledFresh);
  auto bounds = ChangedPixelBounds(tiled, tiledFresh);
  EXPECT_TRUE(bounds.isEmpty())
      << "noise diff bounds=" << bounds.left << "," << bounds.top << " " << bounds.right << ","
      << bounds.bottom;
  EXPECT_LE(difference.mean, 0.1);
}

}  // namespace tgfx

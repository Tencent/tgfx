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
#include "tgfx/layers/filters/TextureFilter.h"
#include "tgfx/layers/layerstyles/DropShadowStyle.h"
#include "tgfx/layers/layerstyles/GlassStyle.h"
#include "utils/Baseline.h"
#include "utils/TestUtils.h"

namespace tgfx {

namespace {

constexpr int SurfaceSize = 520;
constexpr float ImageScale = 0.22f;
constexpr float ImageOffset = 50.0f;
constexpr int CompositionGap = 4;
constexpr int CompositionSize = SurfaceSize * 2 + CompositionGap;

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

RenderedScene ReadScene(const std::shared_ptr<Surface>& surface) {
  if (surface == nullptr) {
    return {};
  }
  RenderedScene result = {surface->width(), surface->height(), {}};
  result.pixels.resize(static_cast<size_t>(surface->width()) *
                       static_cast<size_t>(surface->height()) * 4);
  auto info =
      ImageInfo::Make(result.width, result.height, ColorType::RGBA_8888, AlphaType::Premultiplied);
  if (!surface->readPixels(info, result.pixels.data())) {
    result.pixels.assign(result.pixels.size(), 0);
  }
  return result;
}

RenderedScene RenderLayerScene(Context* context, const LayerFactory& makeLayer,
                               const std::shared_ptr<LayerFilter>& filter) {
  constexpr int size = 320;
  auto surface = Surface::Make(context, size, size);
  if (surface == nullptr) {
    return {};
  }
  DisplayList displayList;
  auto layer = makeLayer();
  if (filter != nullptr) {
    layer->setFilters({filter});
  }
  displayList.root()->addChild(layer);
  displayList.render(surface.get());
  return ReadScene(surface);
}

std::shared_ptr<TextureFilter> MakeTexture(bool clipToShape) {
  return TextureFilter::Make(25.0f, 25.0f, 20.0f, 3491, clipToShape);
}

std::shared_ptr<Surface> ComposeScenes(Context* context,
                                       const std::shared_ptr<Surface> (&scenes)[4]) {
  auto composition = Surface::Make(context, CompositionSize, CompositionSize);
  if (composition == nullptr) {
    return nullptr;
  }
  auto canvas = composition->getCanvas();
  canvas->clear(Color::White());
  for (int index = 0; index < 4; ++index) {
    auto left = static_cast<float>((index % 2) * (SurfaceSize + CompositionGap));
    auto top = static_cast<float>((index / 2) * (SurfaceSize + CompositionGap));
    canvas->drawImage(scenes[index]->makeImageSnapshot(), left, top);
  }
  return composition;
}

std::shared_ptr<Surface> RenderSceneSurface(
    Context* context, const std::shared_ptr<Image>& image,
    const std::vector<std::shared_ptr<LayerFilter>>& filters,
    const std::vector<std::shared_ptr<LayerStyle>>& styles = {}) {
  auto surface = Surface::Make(context, SurfaceSize, SurfaceSize);
  if (surface == nullptr) {
    return nullptr;
  }

  DisplayList displayList;
  displayList.setBackgroundColor(Color::White());

  auto layer = ImageLayer::Make();
  layer->setImage(image);
  auto matrix = Matrix::MakeScale(ImageScale);
  matrix.postTranslate(ImageOffset, ImageOffset);
  layer->setMatrix(matrix);
  layer->setFilters(filters);
  layer->setLayerStyles(styles);
  displayList.root()->addChild(layer);
  displayList.render(surface.get());
  return surface;
}

std::shared_ptr<GlassStyle> MakeGlassStyle() {
  return GlassStyle::Make(70.0f, 50.0f, 20.0f, 25.0f, 20.0f, 135.0f, 60.0f);
}

std::shared_ptr<Surface> RenderGlassSceneSurface(
    Context* context, const std::shared_ptr<Image>& background,
    const std::vector<std::shared_ptr<LayerFilter>>& filters,
    const std::vector<std::shared_ptr<LayerStyle>>& styles) {
  constexpr float glassWidth = 260.0f;
  constexpr float glassHeight = 180.0f;
  auto surface = Surface::Make(context, SurfaceSize, SurfaceSize);
  if (surface == nullptr) {
    return nullptr;
  }

  DisplayList displayList;
  auto backgroundLayer = ImageLayer::Make();
  backgroundLayer->setImage(background);
  backgroundLayer->setMatrix(
      Matrix::MakeScale(static_cast<float>(SurfaceSize) / static_cast<float>(background->width())));
  displayList.root()->addChild(backgroundLayer);

  auto glass = SolidLayer::Make();
  glass->setWidth(glassWidth);
  glass->setHeight(glassHeight);
  glass->setRadiusX(30.0f);
  glass->setRadiusY(30.0f);
  glass->setColor(Color::FromRGBA(255, 255, 255, 90));
  glass->setMatrix(Matrix::MakeTrans((static_cast<float>(SurfaceSize) - glassWidth) * 0.5f,
                                     (static_cast<float>(SurfaceSize) - glassHeight) * 0.5f));
  glass->setLayerStyles(styles);
  glass->setFilters(filters);
  displayList.root()->addChild(glass);

  displayList.render(surface.get());
  return surface;
}

}  // namespace

TGFX_TEST(TextureLayerIntegrationTest, ClipComposition) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/assets/bridge.jpg");
  ASSERT_TRUE(image != nullptr);

  auto renderSolid = [&](bool clipToShape) {
    auto surface = Surface::Make(context, SurfaceSize, SurfaceSize);
    if (surface == nullptr) {
      return surface;
    }
    DisplayList displayList;
    displayList.setBackgroundColor(Color::White());
    auto layer = SolidLayer::Make();
    layer->setWidth(280.0f);
    layer->setHeight(180.0f);
    layer->setColor(Color::FromRGBA(249, 190, 113));
    layer->setMatrix(Matrix::MakeTrans(120.0f, 170.0f));
    layer->setFilters({MakeTexture(clipToShape)});
    displayList.root()->addChild(layer);
    displayList.render(surface.get());
    return surface;
  };

  const std::shared_ptr<Surface> scenes[] = {
      RenderSceneSurface(context, image, {MakeTexture(false)}),
      RenderSceneSurface(context, image, {MakeTexture(true)}),
      renderSolid(false),
      renderSolid(true),
  };
  for (const auto& scene : scenes) {
    ASSERT_TRUE(scene != nullptr);
  }

  EXPECT_TRUE(Baseline::Compare(ComposeScenes(context, scenes),
                                "TextureLayerIntegrationTest/ClipComposition"));
}

TGFX_TEST(TextureLayerIntegrationTest, EffectComposition) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto image = MakeImage("resources/assets/bridge.jpg");
  ASSERT_TRUE(image != nullptr);

  auto shadow = [] {
    return DropShadowStyle::Make(18.0f, 18.0f, 8.0f, 8.0f, Color::FromRGBA(0, 0, 0, 180));
  };
  const std::shared_ptr<Surface> scenes[] = {
      RenderSceneSurface(
          context, image,
          {MakeTexture(false),
           DropShadowFilter::Make(18.0f, 18.0f, 8.0f, 8.0f, Color::FromRGBA(0, 0, 0, 180))}),
      RenderSceneSurface(context, image, {MakeTexture(false), BlurFilter::Make(5.0f, 5.0f)}),
      RenderGlassSceneSurface(context, image, {MakeTexture(true)}, {MakeGlassStyle(), shadow()}),
      RenderGlassSceneSurface(context, image, {MakeTexture(true)}, {shadow(), MakeGlassStyle()}),
  };
  for (const auto& scene : scenes) {
    ASSERT_TRUE(scene != nullptr);
  }

  EXPECT_TRUE(Baseline::Compare(ComposeScenes(context, scenes),
                                "TextureLayerIntegrationTest/EffectComposition"));
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

  auto bounds = filter->filterBounds(srcRect, 1.0f);
  auto expandedBounds = filter->filterBounds(srcRect, 2.0f);
  EXPECT_EQ(filter->filterBounds(srcRect, 2.0f, MapDirection::Reverse), expandedBounds);
  EXPECT_GT(expandedBounds.width(), srcRect.width());
  EXPECT_GT(expandedBounds.height(), srcRect.height());
  EXPECT_GT(expandedBounds.width(), bounds.width());
  EXPECT_GT(expandedBounds.height(), bounds.height());

  filter->setClipToShape(true);
  EXPECT_EQ(filter->filterBounds(srcRect, 2.0f), expandedBounds);

  EXPECT_TRUE(TextureFilter::Make(-1.0f, 10.0f, 4.0f) == nullptr);
  EXPECT_TRUE(TextureFilter::Make(10.0f, 10.0f, -1.0f) == nullptr);
  EXPECT_TRUE(TextureFilter::Make(std::numeric_limits<float>::quiet_NaN(), 10.0f, 4.0f) ==
              nullptr);
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

TGFX_TEST(TextureLayerIntegrationTest, RenderModesAndCacheInvalidation) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto render = [&](RenderMode mode, float initialSizeX, bool mutate) {
    constexpr int size = 320;
    auto surface = Surface::Make(context, size, size);
    if (surface == nullptr) {
      return RenderedScene{};
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
    auto filter = TextureFilter::Make(initialSizeX, 16.0f, 6.0f, 3491, false);
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
      displayList.render(surface.get());
    }
    return ReadScene(surface);
  };

  auto direct = render(RenderMode::Direct, 16.0f, false);
  auto partial = render(RenderMode::Partial, 16.0f, false);
  auto tiled = render(RenderMode::Tiled, 16.0f, false);
  EXPECT_LE(CompareScenes(direct, partial).mean, 0.1);
  EXPECT_LE(CompareScenes(direct, tiled).mean, 0.1);

  auto fresh = render(RenderMode::Direct, 24.0f, false);
  const RenderMode modes[] = {RenderMode::Direct, RenderMode::Partial, RenderMode::Tiled};
  const RenderedScene* initials[] = {&direct, &partial, &tiled};
  for (size_t index = 0; index < 3; ++index) {
    auto mutated = render(modes[index], 16.0f, true);
    EXPECT_GT(CompareScenes(*initials[index], mutated).mean, 0.01);
    auto difference = CompareScenes(mutated, fresh);
    EXPECT_LE(difference.mean, 0.1);
    EXPECT_LE(difference.max, 8);
  }
}

}  // namespace tgfx

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

#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/TextLayer.h"
#include "tgfx/layers/filters/NoiseFilter.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(NoiseLayerTest, NoiseShiftText) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1000, 1000);
  ASSERT_TRUE(surface != nullptr);
  auto displayList = std::make_unique<DisplayList>();

  auto back = SolidLayer::Make();
  back->setColor(Color::White());
  back->setWidth(1000);
  back->setHeight(1000);

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);

  auto noise = NoiseFilter::MakeMono(6.0f, 0.5f, Color::FromRGBA(0, 0, 0, 128), 42.0f,
                                     BlendMode::DstIn);

  auto textLayer = TextLayer::Make();
  textLayer->setText("ABC");
  textLayer->setFont(Font(typeface, 100.f));
  textLayer->setTextColor(Color::Blue());
  textLayer->setFilters({noise});
  back->addChild(textLayer);
  displayList->root()->addChild(back);

  for (int i = 0; i < 8; i++) {
    surface->getCanvas()->clear();
    textLayer->setMatrix(Matrix::MakeTrans(-50.f + static_cast<float>(i) * 30.f, 100.f));
    displayList->render(surface.get());
    EXPECT_TRUE(
        Baseline::Compare(surface, "NoiseLayerTest/NoiseShiftText_frame" + std::to_string(i)));
  }
}

TGFX_TEST(NoiseLayerTest, NoiseRotateText) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1000, 1000);
  ASSERT_TRUE(surface != nullptr);
  auto displayList = std::make_unique<DisplayList>();

  auto back = SolidLayer::Make();
  back->setColor(Color::White());
  back->setWidth(1000);
  back->setHeight(1000);

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);

  auto noise = NoiseFilter::MakeMono(6.0f, 0.5f, Color::FromRGBA(0, 0, 0, 128), 42.0f,
                                     BlendMode::DstIn);

  auto textLayer = TextLayer::Make();
  textLayer->setText("ABC");
  textLayer->setFont(Font(typeface, 100.f));
  textLayer->setTextColor(Color::Blue());
  textLayer->setFilters({noise});
  back->addChild(textLayer);
  displayList->root()->addChild(back);

  auto bounds = textLayer->getBounds();
  auto cx = bounds.left + bounds.width() * 0.5f;
  auto cy = bounds.top + bounds.height() * 0.5f;

  for (int i = 0; i < 8; i++) {
    surface->getCanvas()->clear();
    float degrees = static_cast<float>(i) * 20.f;
    float scale = powf(0.9f, static_cast<float>(i));
    auto matrix = Matrix::MakeTrans(500.f, 500.f) * Matrix::MakeRotate(degrees) *
                  Matrix::MakeScale(scale) * Matrix::MakeTrans(-cx, -cy);
    textLayer->setMatrix(matrix);
    displayList->render(surface.get());
    EXPECT_TRUE(
        Baseline::Compare(surface, "NoiseLayerTest/NoiseRotateText_frame" + std::to_string(i)));
  }
}

TGFX_TEST(NoiseLayerTest, NoiseWithSubtreeCache) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 300);
  ASSERT_TRUE(surface != nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Direct);
  displayList->setSubtreeCacheMaxSize(2048);

  auto noise = NoiseFilter::MakeMono(6.0f, 0.5f, Color::FromRGBA(0, 0, 0, 128), 42.0f,
                                     BlendMode::DstIn);

  // Put the noise filter on the root layer so the root subtree gets cached on the second
  // render, which exercises the SubtreeCache rendering path for NoiseFilter.
  auto root = displayList->root();
  root->setFilters({noise});
  root->setPassThroughBackground(false);

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);

  auto child = TextLayer::Make();
  child->setText("cache");
  child->setFont(Font(typeface, 100.f));
  child->setTextColor(Color::Red());
  child->setMatrix(Matrix::MakeTrans(-50, 50));
  root->addChild(child);

  // First render sets the staticSubtree flag.
  displayList->render(surface.get());
  // Second render builds the subtree cache (createSubtreeCacheImage -> applyFilters).
  displayList->render(surface.get());
  // Third render hits the existing subtree cache and draws using the cached image.
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "NoiseLayerTest/NoiseWithSubtreeCache"));
}

TGFX_TEST(NoiseLayerTest, NoiseWithoutSubtreeCache) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 300);
  ASSERT_TRUE(surface != nullptr);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Direct);
  // SubtreeCache disabled (default maxSize is 0); this drives the regular offscreen
  // rendering path through applyFilters, allowing a side-by-side comparison with
  // the SubtreeCache version.

  auto noise = NoiseFilter::MakeMono(6.0f, 0.5f, Color::FromRGBA(0, 0, 0, 128), 42.0f,
                                     BlendMode::DstIn);

  auto root = displayList->root();
  root->setFilters({noise});
  root->setPassThroughBackground(false);

  auto typeface = MakeTypeface("resources/font/NotoSansSC-Regular.otf");
  ASSERT_TRUE(typeface != nullptr);

  auto child = TextLayer::Make();
  child->setText("cache");
  child->setFont(Font(typeface, 100.f));
  child->setTextColor(Color::Red());
  child->setMatrix(Matrix::MakeTrans(-50, 50));
  root->addChild(child);

  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "NoiseLayerTest/NoiseWithoutSubtreeCache"));
}

}  // namespace tgfx

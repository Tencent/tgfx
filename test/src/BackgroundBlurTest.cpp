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

#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/filters/BlurFilter.h"
#include "tgfx/layers/layerstyles/BackgroundBlurStyle.h"
#include "tgfx/layers/layerstyles/DropShadowStyle.h"
#include "tgfx/layers/layerstyles/InnerShadowStyle.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(BackgroundBlurTest, BackgroundBlur) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 150, 150);
  auto canvas = surface->getCanvas();
  canvas->clipRect(Rect::MakeWH(150, 150));
  auto displayList = std::make_unique<DisplayList>();
  auto solidLayer = SolidLayer::Make();
  solidLayer->setColor(Color::Blue());
  solidLayer->setWidth(150);
  solidLayer->setHeight(150);
  displayList->root()->addChild(solidLayer);

  auto background = ImageLayer::Make();
  background->setImage(MakeImage("resources/apitest/imageReplacement.png"));
  displayList->root()->addChild(background);

  auto layer = ShapeLayer::Make();
  layer->setMatrix(Matrix::MakeTrans(30, 30));
  Path path;
  path.addRect(Rect::MakeWH(100, 100));
  layer->setPath(path);
  auto strokeStyle = ShapeStyle::Make(Color::FromRGBA(100, 0, 0, 100));
  layer->setStrokeStyle(strokeStyle);
  layer->setLineWidth(10);
  layer->setStrokeOnTop(true);
  layer->setExcludeChildEffectsInLayerStyle(true);
  auto filter = BackgroundBlurStyle::Make(2, 2);
  auto dropShadow = DropShadowStyle::Make(10, 10, 0, 0, Color::FromRGBA(0, 0, 0, 100));
  dropShadow->setShowBehindLayer(true);
  layer->setExcludeChildEffectsInLayerStyle(true);
  layer->setLayerStyles({dropShadow, filter});

  auto blurFilter = BlurFilter::Make(1, 2);
  layer->setFilters({blurFilter});

  auto silbing = ShapeLayer::Make();
  Path rect;
  rect.addRect(Rect::MakeWH(50, 50));
  silbing->setPath(rect);
  silbing->setMatrix(Matrix::MakeTrans(-10, 0));
  auto newBackgroundBlur = BackgroundBlurStyle::Make(3, 3);
  silbing->setLayerStyles({dropShadow, newBackgroundBlur});
  silbing->setFillStyle(ShapeStyle::Make(Color::FromRGBA(0, 0, 100, 100)));
  layer->addChild(silbing);

  auto clipLayer = Layer::Make();
  clipLayer->setMatrix(Matrix::MakeTrans(2, 40));
  clipLayer->setScrollRect(Rect::MakeXYWH(10, 10, 20, 20));
  layer->addChild(clipLayer);

  auto child = ShapeLayer::Make();

  child->setPath(rect);
  child->setMatrix(Matrix::MakeScale(0.5, 0.5));
  auto fillStyle2 = ShapeStyle::Make(Color::FromRGBA(0, 100, 0, 100));
  child->setFillStyle(fillStyle2);
  auto backgroundBlur = BackgroundBlurStyle::Make(5, 5);
  child->setLayerStyles({backgroundBlur});
  child->setBlendMode(BlendMode::Multiply);
  clipLayer->addChild(child);

  displayList->root()->addChild(layer);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "BackgroundBlurTest/backgroundLayerBlur"));
}

TGFX_TEST(BackgroundBlurTest, BackgroundBlurStyleTest) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 300);
  auto oldGroupOpacity = Layer::DefaultAllowsGroupOpacity();
  Layer::SetDefaultAllowsGroupOpacity(true);
  auto displayList = std::make_unique<DisplayList>();
  displayList->showDirtyRegions(false);
  auto rootLayer = Layer::Make();
  displayList->root()->addChild(rootLayer);
  auto shapeLayer1 = ShapeLayer::Make();
  shapeLayer1->setFillStyle(ShapeStyle::Make(Color::FromRGBA(0, 0, 0, 2)));
  auto path1 = Path();
  path1.addRect(Rect::MakeXYWH(40.5f, 40.5f, 80.f, 80.f));
  shapeLayer1->setPath(path1);
  shapeLayer1->setMatrix(Matrix::MakeTrans(0.5f, 0.5f));
  shapeLayer1->setLayerStyles({BackgroundBlurStyle::Make(4, 4)});
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  auto imageLayer = ImageLayer::Make();
  imageLayer->setImage(image);
  rootLayer->addChildAt(imageLayer, 0);

  auto shapeLayer2 = ShapeLayer::Make();
  auto path2 = Path();
  path2.addRect(Rect::MakeXYWH(50, 20, 100, 100));
  shapeLayer2->setPath(path2);
  shapeLayer2->setFillStyle(ShapeStyle::Make(Shader::MakeLinearGradient(
      {50, 20}, {150, 120}, {{0.f, 0.f, 1.f, 1.f}, {0.f, 1.f, 0.f, 1.f}})));
  rootLayer->addChildAt(shapeLayer2, 0);

  auto layer2 = Layer::Make();
  layer2->addChild(shapeLayer1);
  rootLayer->addChild(layer2);
  displayList->setZoomScale(2.0f);
  displayList->setContentOffset(-50, -50);
  displayList->setRenderMode(RenderMode::Direct);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "BackgroundBlurTest/BackgroundBlurStyleTest1"));
  layer2->setBlendMode(BlendMode::Difference);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "BackgroundBlurTest/BackgroundBlurStyleTest2"));
  surface->getCanvas()->clear();
  surface->getCanvas()->resetMatrix();
  layer2->draw(surface->getCanvas());
  EXPECT_TRUE(Baseline::Compare(surface, "BackgroundBlurTest/BackgroundBlurStyleTest3"));
  auto maskLayer = ShapeLayer::Make();
  auto maskPath = Path();
  maskPath.addRect(Rect::MakeXYWH(80, 80, 200, 200));
  maskLayer->setPath(maskPath);
  maskLayer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(0, 0, 0, 255)));
  imageLayer->setMask(maskLayer);
  rootLayer->addChild(maskLayer);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "BackgroundBlurTest/BackgroundBlurStyleTest4"));

  auto canvas = surface->getCanvas();
  canvas->clear();
  canvas->setMatrix(Matrix::MakeScale(2.0f, 2.0f));
  canvas->translate(-50, -50);
  layer2->draw(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "BackgroundBlurTest/BackgroundBlurStyleTest5"));
  Layer::SetDefaultAllowsGroupOpacity(oldGroupOpacity);
}

/**
 * Test case where subBackgroundContext is larger than parent->backgroundContext.
 * The blurLayer's blur expansion area exceeds the parent's backgroundContext bounds.
 */
TGFX_TEST(BackgroundBlurTest, SimpleBackgroundBlur) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 300);
  DisplayList displayList;

  // solidLayer: (50,50) to (250,250)
  auto solidLayer = SolidLayer::Make();
  solidLayer->setColor(Color::FromRGBA(0, 100, 200, 255));
  solidLayer->setWidth(200);
  solidLayer->setHeight(200);
  solidLayer->setMatrix(Matrix::MakeTrans(50, 50));

  // blurLayer: (150,150) to (300,300), partially overlaps solidLayer
  auto blurLayer = SolidLayer::Make();
  blurLayer->setColor(Color::FromRGBA(255, 255, 255, 50));
  blurLayer->setWidth(150);
  blurLayer->setHeight(150);
  blurLayer->setMatrix(Matrix::MakeTrans(150, 150));
  blurLayer->setLayerStyles({BackgroundBlurStyle::Make(10, 10)});
  blurLayer->setPassThroughBackground(false);

  auto rootLayer = displayList.root();
  rootLayer->addChild(solidLayer);
  rootLayer->addChild(blurLayer);

  displayList.setRenderMode(RenderMode::Tiled);
  displayList.render(surface.get());

  displayList.setZoomScale(1.5f);
  displayList.setContentOffset(-100, -100);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "BackgroundBlurTest/SimpleBackgroundBlur_tiled"));
}

/**
 * Test PassThrough mode with BackgroundBlurStyle.
 * In pass-through mode, the image drawn to canvas contains the blended background,
 * while the image drawn to backgroundCanvas should be the layer content without background blending.
 */
TGFX_TEST(BackgroundBlurTest, PassThroughWithBackgroundBlur) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 300);
  auto oldGroupOpacity = Layer::DefaultAllowsGroupOpacity();
  Layer::SetDefaultAllowsGroupOpacity(true);
  auto displayList = std::make_unique<DisplayList>();
  displayList->showDirtyRegions(false);

  // Create full screen blue background
  auto backgroundLayer = SolidLayer::Make();
  backgroundLayer->setColor(Color::FromRGBA(0, 100, 200, 255));
  backgroundLayer->setWidth(300);
  backgroundLayer->setHeight(300);

  // Create a container layer with alpha to trigger offscreen rendering
  auto containerLayer = Layer::Make();
  containerLayer->setAlpha(0.9f);

  // Create a child layer with blend mode that triggers pass-through
  auto childLayer = ShapeLayer::Make();
  Path childPath;
  childPath.addRect(Rect::MakeXYWH(75, 75, 150, 150));
  childLayer->setPath(childPath);
  childLayer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 0, 0, 200)));
  childLayer->setBlendMode(BlendMode::Exclusion);
  containerLayer->addChild(childLayer);

  // Create a layer with BackgroundBlurStyle, drawn after the container
  auto blurLayer = SolidLayer::Make();
  blurLayer->setColor(Color::FromRGBA(255, 255, 255, 50));
  blurLayer->setWidth(100);
  blurLayer->setHeight(100);
  blurLayer->setMatrix(Matrix::MakeTrans(150, 60));
  blurLayer->setLayerStyles({BackgroundBlurStyle::Make(5, 5)});

  auto rootLayer = displayList->root();
  rootLayer->addChild(backgroundLayer);
  rootLayer->addChild(containerLayer);
  rootLayer->addChild(blurLayer);

  displayList->setRenderMode(RenderMode::Tiled);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "BackgroundBlurTest/PassThroughWithBackgroundBlur"));

  // Test with tiled render mode
  displayList->setZoomScale(1.5f);
  displayList->setContentOffset(-100, -100);
  displayList->render(surface.get());
  EXPECT_TRUE(
      Baseline::Compare(surface, "BackgroundBlurTest/PassThroughWithBackgroundBlur_clipped"));
  Layer::SetDefaultAllowsGroupOpacity(oldGroupOpacity);
}

TGFX_TEST(BackgroundBlurTest, PartialBackgroundBlur) {
  ContextScope scope;
  auto context = scope.getContext();
  auto surface = Surface::Make(context, 300, 300);
  EXPECT_TRUE(context != nullptr);
  DisplayList displayList;
  auto rootLayer = Layer::Make();
  displayList.root()->addChild(rootLayer);
  auto background = ShapeLayer::Make();
  Path backgroundPath;
  backgroundPath.addRect(Rect::MakeXYWH(0, 0, 300, 300));
  background->setPath(backgroundPath);
  background->addFillStyle(
      ShapeStyle::Make(Shader::MakeRadialGradient({150, 150}, 360, {Color::Red(), Color::Blue()})));
  rootLayer->addChild(background);
  auto solidLayer = SolidLayer::Make();
  solidLayer->setColor(Color::FromRGBA(0, 0, 0, 50));
  solidLayer->setWidth(200);
  solidLayer->setHeight(200);
  solidLayer->setMatrix(Matrix::MakeTrans(50, 50));
  solidLayer->setLayerStyles({BackgroundBlurStyle::Make(10, 10)});
  rootLayer->addChild(solidLayer);
  auto solidLayer2 = SolidLayer::Make();
  solidLayer2->setColor(Color::FromRGBA(0, 0, 0, 10));
  solidLayer2->setWidth(50);
  solidLayer2->setHeight(50);
  solidLayer2->setMatrix(Matrix::MakeTrans(100, 100));
  rootLayer->addChild(solidLayer2);
  displayList.setRenderMode(RenderMode::Partial);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "BackgroundBlurTest/PartialBackgroundBlur"));
  solidLayer2->removeFromParent();
  rootLayer->addChild(solidLayer2);
  EXPECT_TRUE(displayList.root()->bitFields.dirtyDescendents);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "BackgroundBlurTest/PartialBackgroundBlur_partial"));
  solidLayer2->setMatrix(Matrix::MakeTrans(120, 120));
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "BackgroundBlurTest/PartialBackgroundBlur_move"));
}

TGFX_TEST(BackgroundBlurTest, NotRectBackgroundBlur) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  auto canvas = surface->getCanvas();
  canvas->clear();
  auto backgroundLayer = ShapeLayer::Make();
  Path backgroundPath;
  backgroundPath.addRect(Rect::MakeXYWH(0, 0, 200, 200));
  backgroundLayer->setPath(backgroundPath);
  backgroundLayer->addFillStyle(
      ShapeStyle::Make(Shader::MakeRadialGradient({100, 100}, 100, {Color::Red(), Color::Blue()})));
  DisplayList displayList;
  displayList.root()->addChild(backgroundLayer);
  auto layer = ShapeLayer::Make();
  Path path;
  path.addOval(Rect::MakeXYWH(50, 50, 100, 100));
  layer->setPath(path);
  layer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 0, 0, 10)));
  layer->setLayerStyles({BackgroundBlurStyle::Make(10, 10)});
  displayList.root()->addChild(layer);
  layer->draw(canvas);
  EXPECT_TRUE(Baseline::Compare(surface, "BackgroundBlurTest/NotRectBackgroundBlur"));
}

/**
 * Test background blur with group mask.
 * Structure:
 * Root
 * - background (image)
 * - group (with mask)
 *   - blur1 (larger than group, with background blur, fill alpha=128)
 *   - mask (same size and position as group, used as blur1's mask)
 * - blur2 (with background blur, fill alpha=128)
 */
TGFX_TEST(BackgroundBlurTest, BackgroundBlurWithMask) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  auto displayList = std::make_unique<DisplayList>();

  // Background image
  auto background = ImageLayer::Make();
  background->setName("background");
  background->setImage(MakeImage("resources/apitest/imageReplacement.png"));
  displayList->root()->addChild(background);

  // Group layer with mask
  auto group = Layer::Make();
  group->setName("group");
  group->setMatrix(Matrix::MakeTrans(30, 30));
  displayList->root()->addChild(group);

  // blur1: larger than group, with background blur
  auto blur1 = ShapeLayer::Make();
  blur1->setName("blur1");
  Path blur1Path;
  blur1Path.addRect(Rect::MakeXYWH(-20, -20, 140, 140));  // Larger than group
  blur1->setPath(blur1Path);
  auto blur1Fill = ShapeStyle::Make(Color::FromRGBA(255, 0, 0, 128));  // Red with alpha=128
  blur1->setFillStyle(blur1Fill);
  auto blur1Style = BackgroundBlurStyle::Make(5, 5);
  blur1->setLayerStyles({blur1Style});
  blur1->setMatrix(Matrix::MakeTrans(20, 20));
  group->addChild(blur1);

  // mask: same size and position as group
  auto mask = ShapeLayer::Make();
  mask->setName("mask");
  Path maskPath;
  maskPath.addRect(Rect::MakeWH(100, 100));  // Same as group's logical size
  mask->setPath(maskPath);
  auto maskFill = ShapeStyle::Make(Color::White());
  mask->setFillStyle(maskFill);
  mask->setMatrix(Matrix::MakeTrans(60, 20));
  group->addChild(mask);

  // Set mask for group
  blur1->setMask(mask);

  // blur2: outside of group, with background blur
  auto blur2 = ShapeLayer::Make();
  blur2->setName("blur2");
  blur2->setMatrix(Matrix::MakeTrans(100, 100));
  Path blur2Path;
  blur2Path.addRect(Rect::MakeWH(80, 80));
  blur2->setPath(blur2Path);
  auto blur2Fill = ShapeStyle::Make(Color::FromRGBA(0, 0, 255, 10));  // Blue with alpha=128
  blur2->setFillStyle(blur2Fill);
  auto blur2Style = BackgroundBlurStyle::Make(5, 5);
  blur2->setLayerStyles({blur2Style});
  displayList->root()->addChild(blur2);
  displayList->render(surface.get());
  displayList->setZoomScale(0.8f);
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "BackgroundBlurTest/BackgroundBlurWithMask"));
}

TGFX_TEST(BackgroundBlurTest, BackgroundLayerIndexWithNestedHierarchy) {
  ContextScope scope;
  auto context = scope.getContext();
  auto surface = Surface::Make(context, 300, 300);
  auto displayList = std::make_unique<DisplayList>();

  // Structure:
  // root
  // ├─ p (parent)
  // │  ├─ rect1 (shape layer)
  // │  ├─ rect2 (shape layer)
  // │  ├─ rect3 (shape layer)
  // │  │  └─ rect4 (nested shape layer)
  // |- backblur (shape layer with background blur style)
  // └─ (other root children)

  auto p = Layer::Make();
  p->setName("p");
  displayList->root()->addChild(p);

  // Create rect1, rect2, rect3 as shape layers
  auto rect1 = ShapeLayer::Make();
  rect1->setName("rect1");
  Path path1;
  path1.addRect(Rect::MakeXYWH(10, 10, 40, 40));
  rect1->setPath(path1);
  rect1->setFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 0, 0, 255)));  // Red
  p->addChild(rect1);

  auto rect2 = ShapeLayer::Make();
  rect2->setName("rect2");
  Path path2;
  path2.addRect(Rect::MakeXYWH(60, 10, 40, 40));
  rect2->setPath(path2);
  rect2->setFillStyle(ShapeStyle::Make(Color::FromRGBA(0, 255, 0, 255)));  // Green
  p->addChild(rect2);

  auto rect3 = ShapeLayer::Make();
  rect3->setName("rect3");
  Path path3;
  path3.addRect(Rect::MakeXYWH(110, 10, 40, 40));
  rect3->setPath(path3);
  rect3->setFillStyle(ShapeStyle::Make(Color::FromRGBA(0, 0, 255, 255)));  // Blue
  p->addChild(rect3);

  // Create rect4 as child of rect3
  auto rect4 = ShapeLayer::Make();
  rect4->setName("rect4");
  Path path4;
  path4.addRect(Rect::MakeXYWH(5, 5, 20, 20));
  rect4->setPath(path4);
  rect4->setFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 255, 0, 255)));  // Yellow
  rect3->addChild(rect4);

  // Create backblur as shape layer with background blur style
  auto backblur = ShapeLayer::Make();
  backblur->setName("backblur");
  Path blurPath;
  blurPath.addRect(Rect::MakeXYWH(10, 0, 140, 50));
  backblur->setPath(blurPath);
  backblur->setFillStyle(ShapeStyle::Make(Color::FromRGBA(200, 200, 200, 100)));
  auto blurStyle = BackgroundBlurStyle::Make(5, 5);
  backblur->setLayerStyles({blurStyle});
  displayList->root()->addChild(backblur);

  // Render and verify no crash
  displayList->render(surface.get());

  EXPECT_TRUE(
      Baseline::Compare(surface, "BackgroundBlurTest/BackgroundLayerIndexWithNestedHierarchy"));
}

TGFX_TEST(BackgroundBlurTest, ScaledInnerShadowWithBackgroundBlur) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 300);
  auto displayList = std::make_unique<DisplayList>();

  // Background layer
  auto background = ShapeLayer::Make();
  Path backgroundPath;
  backgroundPath.addRect(Rect::MakeWH(300, 300));
  background->setPath(backgroundPath);
  background->setFillStyle(ShapeStyle::Make(
      Shader::MakeLinearGradient({0, 0}, {300, 300}, {Color::Red(), Color::Blue()})));
  displayList->root()->addChild(background);

  // Layer with InnerShadow and BackgroundBlur
  auto layer = ShapeLayer::Make();
  layer->setMatrix(Matrix::MakeTrans(50, 50));
  Path path;
  path.addRoundRect(Rect::MakeWH(100, 100), 10, 10);
  layer->setPath(path);
  layer->setFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 255, 255, 200)));
  auto innerShadow = InnerShadowStyle::Make(5, 5, 5, 5, Color::FromRGBA(0, 0, 0, 128));
  auto backgroundBlur = BackgroundBlurStyle::Make(5, 5);
  layer->setLayerStyles({backgroundBlur, innerShadow});
  displayList->root()->addChild(layer);

  // Render with 20x scale to test rasterization quality
  displayList->setZoomScale(20);
  displayList->setContentOffset(-1000, -1000);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "BackgroundBlurTest/ScaledInnerShadowWithBackgroundBlur"));
}

}  // namespace tgfx

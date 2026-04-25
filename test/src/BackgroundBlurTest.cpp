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

#include <algorithm>
#include "layers/BackgroundSource.h"
#include "layers/RootLayer.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/PictureRecorder.h"
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
  auto filter = BackgroundBlurStyle::Make(2, 2);
  filter->setExcludeChildEffects(true);
  auto dropShadow = DropShadowStyle::Make(10, 10, 0, 0, Color::FromRGBA(0, 0, 0, 100));
  dropShadow->setShowBehindLayer(true);
  dropShadow->setExcludeChildEffects(true);
  layer->setLayerStyles({dropShadow, filter});

  auto blurFilter = BlurFilter::Make(1, 2);
  layer->setFilters({blurFilter});

  auto silbing = ShapeLayer::Make();
  Path rect;
  rect.addRect(Rect::MakeWH(50, 50));
  silbing->setPath(rect);
  silbing->setMatrix(Matrix::MakeTrans(-10, 0));
  auto newBackgroundBlur = BackgroundBlurStyle::Make(3, 3);
  auto silbingDropShadow = DropShadowStyle::Make(10, 10, 0, 0, Color::FromRGBA(0, 0, 0, 100));
  silbingDropShadow->setShowBehindLayer(true);
  silbing->setLayerStyles({silbingDropShadow, newBackgroundBlur});
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
 * Test case where the sub BackgroundSource is larger than the parent BackgroundSource.
 * The blurLayer's blur expansion area exceeds the parent's BackgroundSource bounds.
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
  TGFX_PRIVATE_ACCESS(EXPECT_TRUE(displayList.root()->bitFields.dirtyDescendents);)
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

/**
 * Test background blur with perspective transform (preserve3D = false).
 * When preserve3D is false, layers with perspective transforms should still be able to
 * apply background blur.
 */
TGFX_TEST(BackgroundBlurTest, NestedFlat3DLayer) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 250, 250);
  auto displayList = std::make_unique<DisplayList>();

  // Background image layer
  auto backImage = MakeImage("resources/assets/HappyNewYear.png");
  auto background = ImageLayer::Make();
  background->setImage(backImage);
  background->setMatrix(Matrix::MakeScale(250.f / 1024.f));
  displayList->root()->addChild(background);

  // Parent layer with preserve3D = false (default) and perspective transform
  auto parentLayer = SolidLayer::Make();
  parentLayer->setColor(Color::FromRGBA(200, 200, 200, 100));
  parentLayer->setWidth(150);
  parentLayer->setHeight(150);
  {
    auto size = Size::Make(150, 150);
    auto anchor = Point::Make(0.5f, 0.5f);
    auto offsetToAnchor =
        Matrix3D::MakeTranslate(-anchor.x * size.width, -anchor.y * size.height, 0);
    auto invOffsetToAnchor =
        Matrix3D::MakeTranslate(anchor.x * size.width, anchor.y * size.height, 0);
    auto rotate = Matrix3D::MakeRotate({0, 1, 0}, 25);
    auto perspective = Matrix3D::I();
    perspective.setRowColumn(3, 2, -1.0f / 400.0f);
    auto origin = Matrix3D::MakeTranslate(50, 50, 0);
    parentLayer->setMatrix3D(origin * invOffsetToAnchor * perspective * rotate * offsetToAnchor);
  }
  parentLayer->setPreserve3D(false);
  parentLayer->setLayerStyles({BackgroundBlurStyle::Make(5, 5)});
  displayList->root()->addChild(parentLayer);

  // Child layer also with preserve3D = false and perspective transform + background blur
  auto childLayer = SolidLayer::Make();
  childLayer->setColor(Color::FromRGBA(255, 255, 255, 150));
  childLayer->setWidth(80);
  childLayer->setHeight(80);
  {
    auto size = Size::Make(80, 80);
    auto anchor = Point::Make(0.5f, 0.5f);
    auto offsetToAnchor =
        Matrix3D::MakeTranslate(-anchor.x * size.width, -anchor.y * size.height, 0);
    auto invOffsetToAnchor =
        Matrix3D::MakeTranslate(anchor.x * size.width, anchor.y * size.height, 0);
    auto rotate = Matrix3D::MakeRotate({1, 0, 0}, 20);
    auto perspective = Matrix3D::I();
    perspective.setRowColumn(3, 2, -1.0f / 300.0f);
    auto origin = Matrix3D::MakeTranslate(35, 35, 0);
    childLayer->setMatrix3D(origin * invOffsetToAnchor * perspective * rotate * offsetToAnchor);
  }
  childLayer->setPreserve3D(false);
  childLayer->setLayerStyles({BackgroundBlurStyle::Make(3, 3)});
  parentLayer->addChild(childLayer);

  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "BackgroundBlurTest/NestedFlat3DLayer"));
}

namespace {
bool DirtyRectLess(const Rect& a, const Rect& b) {
  if (a.top != b.top) return a.top < b.top;
  if (a.left != b.left) return a.left < b.left;
  if (a.bottom != b.bottom) return a.bottom < b.bottom;
  return a.right < b.right;
}

std::vector<Rect> SortedDirtyRects(RootLayer* root) {
  auto regions = root->updateDirtyRegions();
  std::sort(regions.begin(), regions.end(), DirtyRectLess);
  return regions;
}
}  // namespace

/**
 * Verify that moving a child inside a background-blur parent that has no content of its own
 * (a pure container) only dirties the child footprints. The child paints above the blur
 * result, so its movement must not trigger blur-radius expansion of the dirty rects.
 */
TGFX_TEST(BackgroundBlurTest, ContainerBlurChildMoveSkipsExpansion) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 300);
  auto displayList = std::make_unique<DisplayList>();
  auto root = static_cast<RootLayer*>(displayList->root());

  auto background = SolidLayer::Make();
  background->setColor(Color::FromRGBA(100, 100, 100, 255));
  background->setWidth(300);
  background->setHeight(300);
  root->addChild(background);

  // Pure container blur layer: no content of its own, just groups children. The blur parent's
  // renderBounds come entirely from the child subtree.
  auto blurContainer = Layer::Make();
  blurContainer->setLayerStyles({BackgroundBlurStyle::Make(10, 10)});
  blurContainer->setMatrix(Matrix::MakeTrans(50, 50));
  root->addChild(blurContainer);

  auto child = SolidLayer::Make();
  child->setColor(Color::Red());
  child->setWidth(40);
  child->setHeight(40);
  child->setMatrix(Matrix::MakeTrans(50, 50));
  blurContainer->addChild(child);

  displayList->render(surface.get());

  // Move child within the blur container. The child's old and new footprints are the only
  // things that change; the blur background itself has not changed, so no blur-radius
  // expansion should occur.
  child->setMatrix(Matrix::MakeTrans(80, 80));
  auto dirty = SortedDirtyRects(root);
  // Old footprint (100,100)-(140,140) joined with new (130,130)-(170,170) gives a union whose
  // bounding box is (100,100)-(170,170). DecomposeRects may split the union into multiple
  // non-overlapping rects with a small amount of redundant coverage, but the bounding box must
  // match and the total area must stay well below what blur-radius expansion would produce.
  Rect unionRect = Rect::MakeEmpty();
  auto totalArea = 0.f;
  for (auto& r : dirty) {
    totalArea += r.width() * r.height();
    if (unionRect.isEmpty()) {
      unionRect = r;
    } else {
      unionRect.join(r);
    }
  }
  EXPECT_EQ(unionRect, Rect::MakeLTRB(100, 100, 170, 170));
  // Without expansion, total area is bounded by the union bounding box 70 * 70 = 4900. With
  // blur-radius 10 expansion, each 40x40 footprint would grow to roughly 60x60 and the union
  // bounding box would grow past 90x90 = 8100. We pick 5000 as a conservative upper bound.
  EXPECT_LE(totalArea, 5000.f);
}

/**
 * Verify that a dirty rect produced OUTSIDE the blur subtree (e.g. a sibling painted BEFORE
 * the blur layer) still triggers blur-radius expansion when it intersects the blur bounds,
 * because such a rect changes the blur sampling input.
 */
TGFX_TEST(BackgroundBlurTest, BackgroundSiblingChangeExpandsInsideBlur) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 300);
  auto displayList = std::make_unique<DisplayList>();
  auto root = static_cast<RootLayer*>(displayList->root());

  // Background sibling painted first.
  auto backgroundSibling = SolidLayer::Make();
  backgroundSibling->setColor(Color::FromRGBA(100, 100, 100, 255));
  backgroundSibling->setWidth(80);
  backgroundSibling->setHeight(80);
  backgroundSibling->setMatrix(Matrix::MakeTrans(60, 60));
  root->addChild(backgroundSibling);

  // Pure container blur layer positioned so the sibling's dirty area lies well inside it.
  auto blurContainer = Layer::Make();
  blurContainer->setLayerStyles({BackgroundBlurStyle::Make(10, 10)});
  root->addChild(blurContainer);

  auto blurChild = SolidLayer::Make();
  blurChild->setColor(Color::FromRGBA(0, 0, 0, 1));
  blurChild->setWidth(300);
  blurChild->setHeight(300);
  blurContainer->addChild(blurChild);

  displayList->render(surface.get());

  // Move the background sibling. This is a dirty rect that existed before the blur layer's
  // subtree ran (part of the snapshot), so blur expansion must apply on BOTH the old and new
  // footprints because both intersect the blur bounds.
  backgroundSibling->setMatrix(Matrix::MakeTrans(100, 100));
  auto dirty = SortedDirtyRects(root);
  // Expansion by blur radius 10 (which filterBackground expands by blur-dependent amount,
  // but at minimum one blur radius per side). Rather than hard-code the exact blur kernel
  // extent we verify the dirty area exceeds the raw footprint sum.
  auto rawArea = 80.f * 80.f * 2.f;  // two 80x80 footprints, the raw minimum
  auto totalArea = 0.f;
  for (auto& r : dirty) {
    totalArea += r.width() * r.height();
  }
  EXPECT_GT(totalArea, rawArea);
}

/**
 * Verify that changing the blur layer's OWN content bounds does not trigger blur-radius
 * expansion either. The layer's own content paints above the blur result, not into it.
 */
TGFX_TEST(BackgroundBlurTest, BlurOwnContentChangeSkipsExpansion) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 300);
  auto displayList = std::make_unique<DisplayList>();
  auto root = static_cast<RootLayer*>(displayList->root());

  auto backgroundFill = SolidLayer::Make();
  backgroundFill->setColor(Color::FromRGBA(200, 200, 200, 255));
  backgroundFill->setWidth(300);
  backgroundFill->setHeight(300);
  root->addChild(backgroundFill);

  // Blur layer WITH its own content.
  auto blurLayer = SolidLayer::Make();
  blurLayer->setColor(Color::Red());
  blurLayer->setWidth(100);
  blurLayer->setHeight(100);
  blurLayer->setMatrix(Matrix::MakeTrans(80, 80));
  blurLayer->setLayerStyles({BackgroundBlurStyle::Make(10, 10)});
  root->addChild(blurLayer);

  displayList->render(surface.get());

  // Grow the blur layer's own content. Its old and new bounds go into the dirty list, but
  // because they appear AFTER the snapshot is taken, blur expansion must not apply.
  blurLayer->setWidth(160);
  auto dirty = SortedDirtyRects(root);
  ASSERT_EQ(dirty.size(), 1u);
  // Old content bounds (80,80)-(180,180) joined with new (80,80)-(240,180).
  EXPECT_EQ(dirty[0], Rect::MakeLTRB(80, 80, 240, 180));
}

/**
 * Verify the fast path: when the blur subtree itself has no pending dirtiness but an ancestor
 * or sibling upstream produced a dirty rect, checkBackgroundStyles() still applies blur
 * expansion to that rect, using the current dirty list directly. This reuses the existing
 * propagateLayerState-driven fast path and must keep working after the snapshot refactor.
 */
TGFX_TEST(BackgroundBlurTest, FastPathBackgroundChangeExpandsInsideBlur) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 300);
  auto displayList = std::make_unique<DisplayList>();
  auto root = static_cast<RootLayer*>(displayList->root());

  auto backgroundSibling = SolidLayer::Make();
  backgroundSibling->setColor(Color::FromRGBA(100, 100, 100, 255));
  backgroundSibling->setWidth(60);
  backgroundSibling->setHeight(60);
  backgroundSibling->setMatrix(Matrix::MakeTrans(40, 40));
  root->addChild(backgroundSibling);

  // Grandparent container. Blur layer lives two levels down, so when only the sibling
  // changes, the grandparent enters updateRenderBounds via the fast path (dirtyDescendents
  // stays false within its subtree, but maxBackgroundOutset triggers checkBackgroundStyles).
  auto grandparent = Layer::Make();
  root->addChild(grandparent);
  auto parent = Layer::Make();
  grandparent->addChild(parent);

  auto blurLayer = Layer::Make();
  blurLayer->setLayerStyles({BackgroundBlurStyle::Make(10, 10)});
  parent->addChild(blurLayer);

  auto blurContent = SolidLayer::Make();
  blurContent->setColor(Color::FromRGBA(0, 0, 0, 1));
  blurContent->setWidth(300);
  blurContent->setHeight(300);
  blurLayer->addChild(blurContent);

  displayList->render(surface.get());

  // The blur subtree stays entirely untouched. Only the background sibling moves.
  backgroundSibling->setMatrix(Matrix::MakeTrans(80, 80));
  auto dirty = SortedDirtyRects(root);
  auto rawArea = 60.f * 60.f * 2.f;
  auto totalArea = 0.f;
  for (auto& r : dirty) {
    totalArea += r.width() * r.height();
  }
  EXPECT_GT(totalArea, rawArea);
}

// Exercises the picture-backed sub BackgroundSource flush path. On flush, onGetOwnContents
// finishes the carrier recorder's current segment, reopens a new recording, replays the segment,
// then restores the caller's canvas state so follow-on drawing keeps working. If the replay
// baseline is wrong, a draw made AFTER the flush lands at a different pixel than the same draw
// made BEFORE the flush. This test detects that divergence end-to-end via pixel readback.
TGFX_TEST(BackgroundBlurTest, PictureSubFlushPreservesCarrierMatrix) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  // Top-level picture-backed source with a non-trivial world matrix so any accidental
  // matrix doubling on flush would shift pixels visibly.
  auto topSource =
      BackgroundSource::Make(/*context=*/nullptr, Rect::MakeWH(200, 200), /*maxOutset=*/0,
                             /*minOutset=*/0, Matrix::I(), ColorSpace::SRGB());
  ASSERT_TRUE(topSource != nullptr);

  PictureRecorder carrier;
  auto* carrierCanvas = carrier.beginRecording();
  // Carrier local origin maps to world (100, 50).
  auto carrierMatrix = Matrix::MakeTrans(100, 50);
  carrierCanvas->setMatrix(carrierMatrix);

  auto subSource = topSource->createSubPicture(&carrier, Rect::MakeWH(100, 100),
                                               /*localToWorld=*/carrierMatrix,
                                               /*localToSurface=*/carrierMatrix);
  ASSERT_TRUE(subSource != nullptr);

  Paint redPaint;
  redPaint.setColor(Color::Red());
  // Marker A at local (10, 10) -> world (110, 60).
  carrierCanvas->drawRect(Rect::MakeXYWH(10, 10, 4, 4), redPaint);

  // Force a segment flush mid-recording.
  auto flushed = subSource->getBackgroundImage();
  EXPECT_TRUE(flushed != nullptr);

  // Canvas pointer may have been swapped by the reopen.
  carrierCanvas = carrier.getRecordingCanvas();
  ASSERT_TRUE(carrierCanvas != nullptr);

  Paint greenPaint;
  greenPaint.setColor(Color::Green());
  // Marker B at local (20, 20) -> world (120, 70). If the flush doubled the baseline
  // matrix we would instead see it near (220, 120).
  carrierCanvas->drawRect(Rect::MakeXYWH(20, 20, 4, 4), greenPaint);

  auto carrierPicture = carrier.finishRecordingAsPicture();
  ASSERT_TRUE(carrierPicture != nullptr);

  auto surface = Surface::Make(context, 200, 200);
  ASSERT_TRUE(surface != nullptr);
  auto* surfaceCanvas = surface->getCanvas();
  surfaceCanvas->clear();
  surfaceCanvas->drawPicture(carrierPicture);

  Bitmap bitmap = {};
  bitmap.allocPixels(200, 200);
  auto pixels = bitmap.lockPixels();
  ASSERT_TRUE(surface->readPixels(bitmap.info(), pixels));
  bitmap.unlockPixels();

  auto markerA = bitmap.getColor(112, 62);
  auto markerB = bitmap.getColor(122, 72);
  EXPECT_EQ(markerA.red, 1.0f);
  EXPECT_EQ(markerA.green, 0.0f);
  EXPECT_EQ(markerA.blue, 0.0f);
  EXPECT_EQ(markerB.red, 0.0f);
  EXPECT_EQ(markerB.green, 1.0f);
  EXPECT_EQ(markerB.blue, 0.0f);

  // If the replay doubled the baseline matrix, marker B would end up near (220, 120); verify
  // that region is clear so a stray fallback cannot mask the bug.
  auto wrongSpot = bitmap.getColor(198, 122);
  EXPECT_EQ(wrongSpot.alpha, 0.0f);
}

// Regression: when a container layer has alpha < 1 and allowsGroupOpacity = true, it creates an
// offscreen carrier whose pixel origin differs from the root surface. A BackgroundBlurStyle child
// nested inside that container must still produce the correct background image without a visible
// shift at the top-left corner.
TGFX_TEST(BackgroundBlurTest, GroupOpacityNestedBackgroundBlur) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 300);
  auto oldGroupOpacity = Layer::DefaultAllowsGroupOpacity();
  Layer::SetDefaultAllowsGroupOpacity(true);
  DisplayList displayList;

  // Use an image background so any pixel offset is clearly visible in the blur result.
  auto background = ImageLayer::Make();
  background->setImage(MakeImage("resources/apitest/imageReplacement.png"));
  background->setMatrix(Matrix::MakeScale(2));

  // Container with alpha < 1 triggers offscreen carrier under groupOpacity.
  auto container = Layer::Make();
  container->setAlpha(0.8f);
  container->setMatrix(Matrix::MakeTrans(50, 50) * Matrix::MakeScale(1.5f));

  // The blur child sits inside the container. Its surface-to-world path exercises
  // createSubSurface with a non-zero carrier origin offset.
  auto blurChild = SolidLayer::Make();
  blurChild->setColor(Color::FromRGBA(255, 255, 255, 60));
  blurChild->setWidth(100);
  blurChild->setHeight(80);
  blurChild->setMatrix(Matrix::MakeTrans(15, 15));
  blurChild->setLayerStyles({BackgroundBlurStyle::Make(8, 8)});
  container->addChild(blurChild);

  auto rootLayer = displayList.root();
  rootLayer->addChild(background);
  rootLayer->addChild(container);

  displayList.setZoomScale(1.5);
  displayList.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "BackgroundBlurTest/GroupOpacityNestedBackgroundBlur"));

  Layer::SetDefaultAllowsGroupOpacity(oldGroupOpacity);
}

// Regression: when Layer::draw is invoked against a canvas that has no backing Surface (e.g. a
// PictureRecorder canvas used for PDF/SVG export), args.context is nullptr and the capture pass
// creates a Picture-backed top-level BackgroundSource. Nested group-opacity carriers inside the
// capture tree then walk RenderContentOnPicture(wantsSubBackground=true) — a path that must
// still stamp the sub's ownImage at the carrier's imageClip top-left, not at segment (0,0).
TGFX_TEST(BackgroundBlurTest, GroupOpacityNestedBackgroundBlurPicturePath) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto oldGroupOpacity = Layer::DefaultAllowsGroupOpacity();
  Layer::SetDefaultAllowsGroupOpacity(true);

  // Same layer hierarchy as GroupOpacityNestedBackgroundBlur. Zoom is set on the recording canvas
  // rather than baked into layer matrices so that Layer::draw computes the correct backgroundMatrix.
  auto background = ImageLayer::Make();
  background->setImage(MakeImage("resources/apitest/imageReplacement.png"));
  background->setMatrix(Matrix::MakeScale(2));

  auto container = Layer::Make();
  container->setAlpha(0.8f);
  container->setMatrix(Matrix::MakeTrans(50, 50) * Matrix::MakeScale(1.5f));

  auto blurChild = SolidLayer::Make();
  blurChild->setColor(Color::FromRGBA(255, 255, 255, 60));
  blurChild->setWidth(100);
  blurChild->setHeight(80);
  blurChild->setMatrix(Matrix::MakeTrans(15, 15));
  blurChild->setLayerStyles({BackgroundBlurStyle::Make(8, 8)});
  container->addChild(blurChild);

  auto rootLayer = Layer::Make();
  rootLayer->addChild(background);
  rootLayer->addChild(container);

  // Recorder canvas has no backing Surface; Layer::draw will walk the capture pass against a
  // Picture-backed top-level BackgroundSource because context stays nullptr. Set the zoom scale
  // on the canvas so Layer::draw sees the correct viewMatrix for background source creation.
  PictureRecorder recorder;
  auto* recordingCanvas = recorder.beginRecording();
  recordingCanvas->scale(1.5f, 1.5f);
  rootLayer->draw(recordingCanvas);
  auto picture = recorder.finishRecordingAsPicture();
  ASSERT_TRUE(picture != nullptr);

  // Replay the picture onto a real GPU surface for pixel comparison.
  auto surface = Surface::Make(context, 300, 300);
  ASSERT_TRUE(surface != nullptr);
  auto* canvas = surface->getCanvas();
  canvas->clear();
  canvas->drawPicture(picture);

  EXPECT_TRUE(
      Baseline::Compare(surface, "BackgroundBlurTest/GroupOpacityNestedBackgroundBlurPicturePath"));

  Layer::SetDefaultAllowsGroupOpacity(oldGroupOpacity);
}

}  // namespace tgfx

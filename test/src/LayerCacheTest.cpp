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

#include "layers/SubtreeCache.h"
#include "layers/TileCache.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/filters/BlurFilter.h"
#include "tgfx/layers/filters/DropShadowFilter.h"
#include "tgfx/layers/layerstyles/DropShadowStyle.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(LayerCacheTest, LayerCache) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 400);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Direct);

  // Test default value is 0 (cache disabled)
  EXPECT_EQ(displayList->subtreeCacheMaxSize(), 0);

  // Test setting subtreeCacheMaxSize
  displayList->setSubtreeCacheMaxSize(2048);
  EXPECT_EQ(displayList->subtreeCacheMaxSize(), 2048);

  // Test negative value is clamped to 0
  displayList->setSubtreeCacheMaxSize(-1);
  EXPECT_EQ(displayList->subtreeCacheMaxSize(), 0);

  // Enable cache
  displayList->setSubtreeCacheMaxSize(2048);

  // Create a parent layer with child (cache requires children/styles/filters)
  auto parent = Layer::Make();
  parent->setName("parent");
  parent->setMatrix(Matrix::MakeTrans(20, 20));

  auto child = ShapeLayer::Make();
  Path path;
  path.addRect(Rect::MakeWH(50, 50));
  child->setPath(path);
  child->setFillStyle(ShapeStyle::Make(Color::Red()));
  parent->addChild(child);

  displayList->root()->addChild(parent);
  // Disable passThroughBackground to allow caching (root layer has passThroughBackground=true by default)
  displayList->root()->setPassThroughBackground(false);

  // First render - staticSubtree flag is not set yet
  displayList->render(surface.get());
  auto root = displayList->root();
  EXPECT_TRUE(root->subtreeCache == nullptr);

  // Second render - creates subtreeCache
  displayList->render(surface.get());
  EXPECT_TRUE(root->subtreeCache != nullptr);
  int expectedLongEdge = 64;
  EXPECT_TRUE(root->subtreeCache->hasCache(context, expectedLongEdge));
}

TGFX_TEST(LayerCacheTest, LayerCacheInvalidation) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 400);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Direct);
  displayList->setSubtreeCacheMaxSize(2048);

  auto parent = Layer::Make();
  parent->setMatrix(Matrix::MakeTrans(20, 20));

  auto child = ShapeLayer::Make();
  Path path;
  path.addRect(Rect::MakeWH(50, 50));
  child->setPath(path);
  child->setFillStyle(ShapeStyle::Make(Color::Green()));
  parent->addChild(child);

  auto root = displayList->root();
  root->addChild(parent);
  // Disable passThroughBackground to allow caching (root layer has passThroughBackground=true by default)
  root->setPassThroughBackground(false);

  // First render - staticSubtree flag is not set yet
  displayList->render(surface.get());
  EXPECT_TRUE(root->subtreeCache == nullptr);

  // Second render - creates subtreeCache
  displayList->render(surface.get());
  EXPECT_TRUE(root->subtreeCache != nullptr);

  // Adding a new child - should invalidate root's cache
  auto newChild = ShapeLayer::Make();
  Path newPath;
  newPath.addRect(Rect::MakeWH(20, 20));
  newChild->setPath(newPath);
  newChild->setMatrix(Matrix::MakeTrans(60, 0));
  newChild->setFillStyle(ShapeStyle::Make(Color::FromRGBA(255, 255, 0, 255)));
  parent->addChild(newChild);

  // Cache should be invalidated after adding child
  EXPECT_TRUE(root->subtreeCache == nullptr);

  // First render after modification - staticSubtree flag is not set yet
  displayList->render(surface.get());
  EXPECT_TRUE(root->subtreeCache == nullptr);

  // Second render - creates subtreeCache again
  displayList->render(surface.get());
  EXPECT_TRUE(root->subtreeCache != nullptr);

  // Modifying child transform - should invalidate cache
  child->setMatrix(Matrix::MakeTrans(10, 10));
  EXPECT_TRUE(root->subtreeCache == nullptr);

  // Render twice to recreate cache
  displayList->render(surface.get());
  displayList->render(surface.get());
  EXPECT_TRUE(root->subtreeCache != nullptr);
}

TGFX_TEST(LayerCacheTest, LayerCacheWithEffects) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1000, 1000);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Direct);
  displayList->setSubtreeCacheMaxSize(2048);

  // Parent with child that has filter
  auto parent1 = Layer::Make();
  parent1->setMatrix(Matrix::MakeTrans(20, 20));

  auto child1 = ShapeLayer::Make();
  Path path1;
  path1.addRect(Rect::MakeWH(80, 80));
  child1->setPath(path1);
  child1->setFillStyle(ShapeStyle::Make(Color::Red()));
  auto filter = DropShadowFilter::Make(5, 5, 3, 3, Color::Black());
  child1->setFilters({filter});
  parent1->addChild(child1);

  auto root = displayList->root();
  root->addChild(parent1);

  // Parent with child that has layer style
  auto parent2 = Layer::Make();
  parent2->setMatrix(Matrix::MakeTrans(150, 30));

  auto child2 = ShapeLayer::Make();
  Path path2;
  path2.addRect(Rect::MakeWH(60, 60));
  child2->setPath(path2);
  child2->setFillStyle(ShapeStyle::Make(Color::Blue()));
  auto style = DropShadowStyle::Make(8, 8, 4, 4, Color::Black(), false);
  child2->setLayerStyles({style});
  parent2->addChild(child2);

  root->addChild(parent2);
  // Disable passThroughBackground to allow caching (root layer has passThroughBackground=true by default)
  root->setPassThroughBackground(false);

  // First render - staticSubtree flag is not set yet
  displayList->render(surface.get());
  EXPECT_TRUE(root->subtreeCache == nullptr);

  // Second render - creates subtreeCache
  displayList->render(surface.get());
  EXPECT_TRUE(root->subtreeCache != nullptr);

  EXPECT_TRUE(Baseline::Compare(surface, "LayerCacheTest/LayerCacheWithEffects"));
}

TGFX_TEST(LayerCacheTest, LayerCacheWithTransform) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 400);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Direct);
  displayList->setSubtreeCacheMaxSize(2048);

  auto parent = Layer::Make();
  parent->setMatrix(Matrix::MakeTrans(5, 5));

  auto child = ShapeLayer::Make();
  Path path;
  path.addRect(Rect::MakeWH(50, 50));
  child->setPath(path);
  child->setFillStyle(ShapeStyle::Make(Color::Red()));
  parent->addChild(child);

  auto root = displayList->root();
  root->addChild(parent);
  // Disable passThroughBackground to allow caching (root layer has passThroughBackground=true by default)
  root->setPassThroughBackground(false);

  // First render - staticSubtree flag is not set yet
  displayList->render(surface.get());
  EXPECT_TRUE(root->subtreeCache == nullptr);

  // Second render - creates subtreeCache
  displayList->render(surface.get());
  EXPECT_TRUE(root->subtreeCache != nullptr);

  // Change zoomScale - cache should still be valid (just uses different mipmap level)
  displayList->setZoomScale(1.5f);
  displayList->render(surface.get());
  EXPECT_TRUE(root->subtreeCache != nullptr);

  // Change parent's transform - should invalidate root's cache
  parent->setMatrix(Matrix::MakeTrans(10, 10));
  EXPECT_TRUE(root->subtreeCache == nullptr);

  // First render after modification - staticSubtree flag is not set yet
  displayList->render(surface.get());
  EXPECT_TRUE(root->subtreeCache == nullptr);

  // Second render - recreate cache
  displayList->render(surface.get());
  EXPECT_TRUE(root->subtreeCache != nullptr);
}

TGFX_TEST(LayerCacheTest, LayerCacheContentScale) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 400, 400);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Direct);
  displayList->setSubtreeCacheMaxSize(400);

  auto parent = Layer::Make();
  parent->setMatrix(Matrix::MakeTrans(10, 10));

  auto child = ShapeLayer::Make();
  Path path;
  path.addRect(Rect::MakeWH(100, 100));
  child->setPath(path);
  child->setFillStyle(ShapeStyle::Make(Color::Blue()));
  parent->addChild(child);

  auto root = displayList->root();
  root->addChild(parent);
  // Disable passThroughBackground to allow caching (root layer has passThroughBackground=true by default)
  root->setPassThroughBackground(false);

  // First render - staticSubtree flag is not set yet
  displayList->render(surface.get());
  EXPECT_TRUE(root->subtreeCache == nullptr);

  // Second render - creates subtreeCache
  displayList->render(surface.get());
  EXPECT_TRUE(root->subtreeCache != nullptr);

  // At zoom 1.0, longEdge should be 100
  int expectedLongEdge1_0 = 100;
  EXPECT_TRUE(root->subtreeCache->hasCache(context, expectedLongEdge1_0));

  // Render at zoom 0.5 - cache should still exist
  displayList->setZoomScale(0.5f);
  displayList->render(surface.get());
  EXPECT_TRUE(root->subtreeCache != nullptr);

  // At zoom 0.5, longEdge should be 50
  int expectedLongEdge0_5 = 50;
  EXPECT_TRUE(root->subtreeCache->hasCache(context, expectedLongEdge0_5));

  // Render at zoom 2.0
  displayList->setZoomScale(2.0f);
  displayList->render(surface.get());
  EXPECT_TRUE(root->subtreeCache != nullptr);

  // At zoom 2.0, longEdge should be 200
  int expectedLongEdge2_0 = 200;
  EXPECT_TRUE(root->subtreeCache->hasCache(context, expectedLongEdge2_0));

  // Render at zoom 1.0 again
  displayList->setZoomScale(1.0f);
  displayList->render(surface.get());
  EXPECT_TRUE(root->subtreeCache != nullptr);

  // At zoom 1.0 again, cache should still be valid for longEdge 100
  EXPECT_TRUE(root->subtreeCache->hasCache(context, expectedLongEdge1_0));

  // Render at extreme zoom out
  displayList->setZoomScale(0.1f);
  displayList->render(surface.get());
  EXPECT_TRUE(root->subtreeCache != nullptr);

  // At zoom 0.1, longEdge < minLongEdge, cache should be 50
  int expectedLongEdge0_1 = 50;
  EXPECT_TRUE(root->subtreeCache->cacheEntries.size() == 3);
  EXPECT_TRUE(root->subtreeCache->hasCache(context, expectedLongEdge0_1));
}

TGFX_TEST(LayerCacheTest, StaticSubtree) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 350, 350);
  auto displayList = std::make_unique<DisplayList>();

  auto rootLayer = Layer::Make();
  auto childLayer = ShapeLayer::Make();
  Path childPath;
  childPath.addRect(Rect::MakeWH(100, 100));
  childLayer->setPath(childPath);
  childLayer->setFillStyle(ShapeStyle::Make(Color::Red()));
  rootLayer->addChild(childLayer);

  displayList->root()->addChild(rootLayer);
  EXPECT_FALSE(rootLayer->bitFields.staticSubtree);
  EXPECT_FALSE(childLayer->bitFields.staticSubtree);

  // After first render, staticSubtree should be true
  displayList->render(surface.get());
  EXPECT_TRUE(rootLayer->bitFields.staticSubtree);
  EXPECT_TRUE(childLayer->bitFields.staticSubtree);

  // After adding filter, both should be false
  auto filter = BlurFilter::Make(10.f, 10.f);
  childLayer->setFilters({filter});
  EXPECT_FALSE(rootLayer->bitFields.staticSubtree);
  EXPECT_FALSE(childLayer->bitFields.staticSubtree);

  // After render, both should be true again
  displayList->render(surface.get());
  EXPECT_TRUE(rootLayer->bitFields.staticSubtree);
  EXPECT_TRUE(childLayer->bitFields.staticSubtree);

  // After adding layer style, both should be false
  auto style = DropShadowStyle::Make(5, 5, 0, 0, Color::Black(), false);
  childLayer->setLayerStyles({style});
  EXPECT_FALSE(rootLayer->bitFields.staticSubtree);
  EXPECT_FALSE(childLayer->bitFields.staticSubtree);

  // After render, both should be true again
  displayList->render(surface.get());
  EXPECT_TRUE(rootLayer->bitFields.staticSubtree);
  EXPECT_TRUE(childLayer->bitFields.staticSubtree);

  // After invalidating descendents, both should be false
  rootLayer->invalidateDescendents();
  EXPECT_FALSE(rootLayer->bitFields.staticSubtree);
  EXPECT_TRUE(childLayer->bitFields.staticSubtree);

  // After render, both should be true again
  displayList->render(surface.get());
  EXPECT_TRUE(rootLayer->bitFields.staticSubtree);
  EXPECT_TRUE(childLayer->bitFields.staticSubtree);
}

/**
 * Test that simple Rect/RRect leaf nodes skip subtree caching.
 */
TGFX_TEST(LayerCacheTest, SimpleShapeSkipsCache) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Direct);
  displayList->setSubtreeCacheMaxSize(2048);

  auto root = displayList->root();
  root->setPassThroughBackground(false);

  // Test 1: Simple SolidLayer (Rect) - should NOT create subtree cache
  auto rectLayer = SolidLayer::Make();
  rectLayer->setWidth(50);
  rectLayer->setHeight(50);
  rectLayer->setColor(Color::Red());
  rectLayer->setMatrix(Matrix::MakeTrans(10, 10));
  root->addChild(rectLayer);

  // Render twice to trigger cache creation attempt
  displayList->render(surface.get());
  displayList->render(surface.get());

  // Simple Rect layer should not have subtree cache
  EXPECT_TRUE(rectLayer->subtreeCache == nullptr);

  // Test 2: Simple SolidLayer (RRect) - should NOT create subtree cache
  auto rrectLayer = SolidLayer::Make();
  rrectLayer->setWidth(50);
  rrectLayer->setHeight(50);
  rrectLayer->setRadiusX(10);
  rrectLayer->setRadiusY(10);
  rrectLayer->setColor(Color::Blue());
  rrectLayer->setMatrix(Matrix::MakeTrans(70, 10));
  root->addChild(rrectLayer);

  displayList->render(surface.get());
  displayList->render(surface.get());

  // Simple RRect layer should not have subtree cache
  EXPECT_TRUE(rrectLayer->subtreeCache == nullptr);

  // Test 3: ShapeLayer with Path (not Rect/RRect) - SHOULD create subtree cache
  auto pathLayer = ShapeLayer::Make();
  Path path = {};
  path.addOval(Rect::MakeWH(50, 50));
  pathLayer->setPath(path);
  pathLayer->setFillStyle(ShapeStyle::Make(Color::Green()));
  pathLayer->setMatrix(Matrix::MakeTrans(130, 10));
  root->addChild(pathLayer);

  displayList->render(surface.get());
  displayList->render(surface.get());

  // Path layer (not Rect/RRect) should have subtree cache
  EXPECT_TRUE(pathLayer->subtreeCache != nullptr);

  // Test 4: SolidLayer with filter - SHOULD create subtree cache
  auto rectWithFilter = SolidLayer::Make();
  rectWithFilter->setWidth(50);
  rectWithFilter->setHeight(50);
  rectWithFilter->setColor(Color::FromRGBA(255, 255, 0, 255));
  rectWithFilter->setMatrix(Matrix::MakeTrans(10, 70));
  rectWithFilter->setFilters({BlurFilter::Make(2, 2)});
  root->addChild(rectWithFilter);

  displayList->render(surface.get());
  displayList->render(surface.get());

  // Rect with filter should have subtree cache
  EXPECT_TRUE(rectWithFilter->subtreeCache != nullptr);

  // Test 5: SolidLayer with layer style - SHOULD create subtree cache
  auto rectWithStyle = SolidLayer::Make();
  rectWithStyle->setWidth(50);
  rectWithStyle->setHeight(50);
  rectWithStyle->setColor(Color::FromRGBA(255, 0, 255, 255));
  rectWithStyle->setMatrix(Matrix::MakeTrans(70, 70));
  rectWithStyle->setLayerStyles({DropShadowStyle::Make(3, 3, 2, 2, Color::Black(), false)});
  root->addChild(rectWithStyle);

  displayList->render(surface.get());
  displayList->render(surface.get());

  // Rect with layer style should have subtree cache
  EXPECT_TRUE(rectWithStyle->subtreeCache != nullptr);

  // Test 6: Layer with Rect child - SHOULD create subtree cache (not a leaf node)
  auto parentLayer = Layer::Make();
  parentLayer->setMatrix(Matrix::MakeTrans(130, 70));
  auto childRect = SolidLayer::Make();
  childRect->setWidth(50);
  childRect->setHeight(50);
  childRect->setColor(Color::FromRGBA(0, 255, 255, 255));
  parentLayer->addChild(childRect);
  root->addChild(parentLayer);

  displayList->render(surface.get());
  displayList->render(surface.get());

  // Parent layer with children should have subtree cache
  EXPECT_TRUE(parentLayer->subtreeCache != nullptr);
  // Child rect should not have subtree cache (simple leaf)
  EXPECT_TRUE(childRect->subtreeCache == nullptr);
}

TGFX_TEST(LayerCacheTest, DirtyFlag) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto displayList = std::make_unique<DisplayList>();
  auto surface = Surface::Make(context, 100, 100);
  auto child = Layer::Make();
  displayList->root()->addChild(child);

  auto grandChild = ImageLayer::Make();
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  EXPECT_TRUE(image != nullptr);
  grandChild->setImage(image);
  grandChild->setMatrix(Matrix::MakeTrans(10, 10));
  grandChild->setVisible(false);
  child->addChild(grandChild);

  auto child2 = ImageLayer::Make();
  child2->setImage(image);
  displayList->root()->addChild(child2);

  displayList->render(surface.get());

  auto root = displayList->root();
  EXPECT_TRUE(grandChild->bitFields.dirtyDescendents);
  EXPECT_TRUE(grandChild->layerContent == nullptr && grandChild->bitFields.dirtyContent);
  EXPECT_TRUE(!child->bitFields.dirtyDescendents && !child->bitFields.dirtyContent);
  EXPECT_TRUE(!root->bitFields.dirtyDescendents && !root->bitFields.dirtyContent);

  grandChild->setVisible(true);
  EXPECT_TRUE(grandChild->bitFields.dirtyDescendents);
  EXPECT_TRUE(grandChild->layerContent == nullptr && grandChild->bitFields.dirtyContent);
  EXPECT_TRUE(child->bitFields.dirtyDescendents);
  EXPECT_TRUE(root->bitFields.dirtyDescendents);
  displayList->render(surface.get());

  EXPECT_TRUE(!grandChild->bitFields.dirtyDescendents && !grandChild->bitFields.dirtyContent);
  EXPECT_TRUE(grandChild->layerContent != nullptr);
  EXPECT_TRUE(!child->bitFields.dirtyDescendents && !child->bitFields.dirtyContent);
  EXPECT_TRUE(!root->bitFields.dirtyDescendents && !root->bitFields.dirtyContent);

  child->setVisible(false);
  EXPECT_TRUE(!grandChild->bitFields.dirtyDescendents && !grandChild->bitFields.dirtyContent);
  EXPECT_TRUE(grandChild->layerContent != nullptr);
  EXPECT_TRUE(!child->bitFields.dirtyDescendents && !child->bitFields.dirtyContent);
  EXPECT_TRUE(root->bitFields.dirtyDescendents && !root->bitFields.dirtyContent);
}

TGFX_TEST(LayerCacheTest, DirtyRegionTest) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1024, 800);
  auto displayList = std::make_unique<DisplayList>();
  displayList->showDirtyRegions(false);
  auto rootLayer = Layer::Make();
  displayList->root()->addChild(rootLayer);

  auto shapeLayer1 = ShapeLayer::Make();
  shapeLayer1->setStrokeStyle(ShapeStyle::Make(Color::Black()));
  auto path1 = Path();
  path1.addRect(Rect::MakeXYWH(40, 40, 100, 140));
  shapeLayer1->setPath(path1);
  // rootLayer->addChild(shapeLayer1);
  auto bounds1 = shapeLayer1->getBounds();
  shapeLayer1->getGlobalMatrix().mapRect(&bounds1);

  auto shapeLayer2 = ShapeLayer::Make();
  shapeLayer2->setStrokeStyle(ShapeStyle::Make(Color::Black()));
  auto path2 = Path();
  path2.addRect(Rect::MakeXYWH(120, 20, 60, 220));
  shapeLayer2->setPath(path2);
  // rootLayer->addChild(shapeLayer2);
  auto bounds2 = shapeLayer2->getBounds();
  shapeLayer2->getGlobalMatrix().mapRect(&bounds2);

  auto shapeLayer3 = ShapeLayer::Make();
  shapeLayer3->setStrokeStyle(ShapeStyle::Make(Color::Black()));
  auto path3 = Path();
  path3.addRect(Rect::MakeXYWH(60, 80, 40, 60));
  shapeLayer3->setPath(path3);
  // rootLayer->addChild(shapeLayer3);
  auto bounds3 = shapeLayer3->getBounds();
  shapeLayer3->getGlobalMatrix().mapRect(&bounds3);

  auto shapeLayer4 = ShapeLayer::Make();
  shapeLayer4->setStrokeStyle(ShapeStyle::Make(Color::Black()));
  auto path4 = Path();
  path4.addRect(Rect::MakeXYWH(800, 40, 80, 100));
  shapeLayer4->setPath(path4);
  // rootLayer->addChild(shapeLayer4);
  auto bounds4 = shapeLayer4->getBounds();
  shapeLayer4->getGlobalMatrix().mapRect(&bounds4);

  auto shapeLayer5 = ShapeLayer::Make();
  shapeLayer5->setStrokeStyle(ShapeStyle::Make(Color::Black()));
  auto path5 = Path();
  path5.addRect(Rect::MakeXYWH(840, 110, 120, 130));
  shapeLayer5->setPath(path5);
  // rootLayer->addChild(shapeLayer5);
  auto bounds5 = shapeLayer5->getBounds();
  shapeLayer5->getGlobalMatrix().mapRect(&bounds5);

  auto shapeLayer6 = ShapeLayer::Make();
  shapeLayer6->setStrokeStyle(ShapeStyle::Make(Color::Black()));
  auto path6 = Path();
  path6.addRect(Rect::MakeXYWH(80, 460, 120, 180));
  shapeLayer6->setPath(path6);
  // rootLayer->addChild(shapeLayer6);
  auto bounds6 = shapeLayer6->getBounds();
  shapeLayer6->getGlobalMatrix().mapRect(&bounds6);

  auto shapeLayer7 = ShapeLayer::Make();
  shapeLayer7->setStrokeStyle(ShapeStyle::Make(Color::Black()));
  auto path7 = Path();
  path7.addRect(Rect::MakeXYWH(20, 600, 240, 100));
  shapeLayer7->setPath(path7);
  // rootLayer->addChild(shapeLayer7);
  auto bounds7 = shapeLayer7->getBounds();
  shapeLayer7->getGlobalMatrix().mapRect(&bounds7);

  auto shapeLayer8 = ShapeLayer::Make();
  shapeLayer8->setStrokeStyle(ShapeStyle::Make(Color::Black()));
  auto path8 = Path();
  path8.addRect(Rect::MakeXYWH(300, 500, 100, 140));
  shapeLayer8->setPath(path8);
  // rootLayer->addChild(shapeLayer8);
  auto bounds8 = shapeLayer8->getBounds();
  shapeLayer8->getGlobalMatrix().mapRect(&bounds8);

  auto shapeLayer9 = ShapeLayer::Make();
  shapeLayer9->setStrokeStyle(ShapeStyle::Make(Color::Black()));
  auto path9 = Path();
  path9.addRect(Rect::MakeXYWH(220, 460, 140, 50));
  shapeLayer9->setPath(path9);
  // rootLayer->addChild(shapeLayer9);
  auto bounds9 = shapeLayer9->getBounds();
  shapeLayer9->getGlobalMatrix().mapRect(&bounds9);

  auto shapeLayer10 = ShapeLayer::Make();
  shapeLayer10->setStrokeStyle(ShapeStyle::Make(Color::Black()));
  auto path10 = Path();
  path10.addRect(Rect::MakeXYWH(820, 420, 140, 200));
  shapeLayer10->setPath(path10);
  // rootLayer->addChild(shapeLayer10);
  auto bounds10 = shapeLayer10->getBounds();
  shapeLayer10->getGlobalMatrix().mapRect(&bounds10);

  auto shapeLayer11 = ShapeLayer::Make();
  shapeLayer11->setStrokeStyle(ShapeStyle::Make(Color::Black()));
  auto path11 = Path();
  path11.addRect(Rect::MakeXYWH(850, 540, 80, 40));
  shapeLayer11->setPath(path11);
  // rootLayer->addChild(shapeLayer11);
  auto bounds11 = shapeLayer11->getBounds();
  shapeLayer11->getGlobalMatrix().mapRect(&bounds11);

  displayList->render(surface.get());
  displayList->showDirtyRegions(true);

  rootLayer->removeChildren();
  rootLayer->addChild(shapeLayer1);
  rootLayer->addChild(shapeLayer2);
  rootLayer->addChild(shapeLayer3);

  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerCacheTest/DirtyRegionTest1"));

  rootLayer->removeChildren();
  rootLayer->addChild(shapeLayer1);
  rootLayer->addChild(shapeLayer2);
  rootLayer->addChild(shapeLayer3);
  rootLayer->addChild(shapeLayer4);
  rootLayer->addChild(shapeLayer5);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerCacheTest/DirtyRegionTest2"));

  rootLayer->removeChildren();
  rootLayer->addChild(shapeLayer1);
  rootLayer->addChild(shapeLayer2);
  rootLayer->addChild(shapeLayer3);
  rootLayer->addChild(shapeLayer4);
  rootLayer->addChild(shapeLayer5);
  rootLayer->addChild(shapeLayer6);
  rootLayer->addChild(shapeLayer7);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerCacheTest/DirtyRegionTest3"));

  rootLayer->removeChildren();
  rootLayer->addChild(shapeLayer1);
  rootLayer->addChild(shapeLayer2);
  rootLayer->addChild(shapeLayer3);
  rootLayer->addChild(shapeLayer4);
  rootLayer->addChild(shapeLayer5);
  rootLayer->addChild(shapeLayer6);
  rootLayer->addChild(shapeLayer7);
  rootLayer->addChild(shapeLayer8);
  rootLayer->addChild(shapeLayer9);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerCacheTest/DirtyRegionTest4"));

  rootLayer->removeChildren();
  rootLayer->addChild(shapeLayer1);
  rootLayer->addChild(shapeLayer2);
  rootLayer->addChild(shapeLayer3);
  rootLayer->addChild(shapeLayer4);
  rootLayer->addChild(shapeLayer5);
  rootLayer->addChild(shapeLayer6);
  rootLayer->addChild(shapeLayer7);
  rootLayer->addChild(shapeLayer8);
  rootLayer->addChild(shapeLayer9);
  rootLayer->addChild(shapeLayer10);
  rootLayer->addChild(shapeLayer11);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerCacheTest/DirtyRegionTest5"));

  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setAllowZoomBlur(true);
  displayList->setMaxTileCount(512);
  displayList->render(surface.get());
  // Clear the previous dirty regions.
  displayList->showDirtyRegions(false);
  displayList->showDirtyRegions(true);
  rootLayer->removeChildren();
  rootLayer->addChild(shapeLayer1);
  rootLayer->addChild(shapeLayer2);
  rootLayer->addChild(shapeLayer3);
  rootLayer->addChild(shapeLayer4);
  rootLayer->addChild(shapeLayer5);
  rootLayer->addChild(shapeLayer6);
  rootLayer->addChild(shapeLayer7);
  rootLayer->addChild(shapeLayer8);
  rootLayer->addChild(shapeLayer9);
  rootLayer->addChild(shapeLayer10);
  rootLayer->addChild(shapeLayer11);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerCacheTest/DirtyRegionTest6"));

  // Clear the previous dirty regions.
  displayList->showDirtyRegions(false);
  displayList->showDirtyRegions(true);
  displayList->setContentOffset(-100, -300);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerCacheTest/DirtyRegionTest7"));

  // Clear the previous dirty regions.
  displayList->showDirtyRegions(false);
  displayList->showDirtyRegions(true);
  displayList->setZoomScale(1.3f);
  displayList->setMaxTilesRefinedPerFrame(0);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerCacheTest/DirtyRegionTest8"));

  // Clear the previous dirty regions.
  displayList->showDirtyRegions(false);
  displayList->showDirtyRegions(true);
  displayList->setMaxTilesRefinedPerFrame(INT_MAX);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerCacheTest/DirtyRegionTest9"));

  // Clear the previous dirty regions.
  displayList->showDirtyRegions(false);
  displayList->showDirtyRegions(true);
  displayList->setContentOffset(250, 150);
  displayList->setZoomScale(0.5f);
  displayList->setMaxTilesRefinedPerFrame(0);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerCacheTest/DirtyRegionTest10"));

  // Clear the previous dirty regions.
  displayList->showDirtyRegions(false);
  displayList->showDirtyRegions(true);
  displayList->setMaxTilesRefinedPerFrame(INT_MAX);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerCacheTest/DirtyRegionTest11"));
}

TGFX_TEST(LayerCacheTest, TileClearWhenAllLayersRemoved) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto surface = Surface::Make(context, 256, 256);

  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileSize(128);
  displayList->setBackgroundColor(Color::White());

  auto rootLayer = displayList->root();

  auto blueRect = ShapeLayer::Make();
  Path bluePath = {};
  bluePath.addRect(Rect::MakeXYWH(0, 0, 256, 256));
  blueRect->setPath(bluePath);
  blueRect->setFillStyle(ShapeStyle::Make(Color::Blue()));
  rootLayer->addChild(blueRect);
  displayList->render(surface.get());

  EXPECT_EQ(displayList->tileCaches.size(), 1u);
  auto result = displayList->tileCaches.find(1000);
  EXPECT_TRUE(result != displayList->tileCaches.end());
  EXPECT_EQ(result->second->tileMap.size(), 4u);

  blueRect->removeFromParent();
  displayList->render(surface.get());

  EXPECT_TRUE(displayList->tileCaches.empty());

  // Test 1: Add a layer that only covers part of tile(0,0)
  // Red rectangle at (20,20) with size 60x60, only covers (20,20)-(80,80) in tile(0,0)
  auto smallRect = ShapeLayer::Make();
  Path smallPath = {};
  smallPath.addRect(Rect::MakeXYWH(20, 20, 60, 60));
  smallRect->setPath(smallPath);
  smallRect->setFillStyle(ShapeStyle::Make(Color::Red()));
  rootLayer->addChild(smallRect);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerCacheTest/TileClear_PartialTile"));

  // Test 2: Add another layer in a different tile that doesn't dirty the partial tile,
  // but the entire partial tile should still be displayed correctly
  // Green rectangle at (150,150) with size 50x50, only in tile(1,1)
  auto greenRect = ShapeLayer::Make();
  Path greenPath = {};
  greenPath.addRect(Rect::MakeXYWH(150, 150, 50, 50));
  greenRect->setPath(greenPath);
  greenRect->setFillStyle(ShapeStyle::Make(Color::Green()));
  rootLayer->addChild(greenRect);
  displayList->render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "LayerCacheTest/TileClear_PartialTileWithNewLayer"));
}

/**
 * Test that overlapping layers with intersecting dirty regions don't cause duplicate tile recycling.
 * When two layers overlap and both are modified, their dirty regions may cover the same tiles.
 * The tile should only be recycled once, not multiple times.
 */
TGFX_TEST(LayerCacheTest, OverlappingDirtyRegions) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);

  auto surface = Surface::Make(context, 256, 256);
  auto displayList = std::make_unique<DisplayList>();
  displayList->setRenderMode(RenderMode::Tiled);
  displayList->setTileSize(128);
  displayList->setBackgroundColor(Color::White());

  auto rootLayer = displayList->root();

  auto redRect = ShapeLayer::Make();
  Path redPath = {};
  redPath.addRect(Rect::MakeXYWH(20, 20, 100, 100));
  redRect->setPath(redPath);
  redRect->setFillStyle(ShapeStyle::Make(Color::Red()));
  rootLayer->addChild(redRect);

  auto blueRect = ShapeLayer::Make();
  Path bluePath = {};
  bluePath.addRect(Rect::MakeXYWH(95, 95, 100, 100));
  blueRect->setPath(bluePath);
  blueRect->setFillStyle(ShapeStyle::Make(Color::Blue()));
  rootLayer->addChild(blueRect);

  displayList->render(surface.get());
  EXPECT_EQ(displayList->tileCaches.size(), 1u);

  redRect->removeFromParent();
  blueRect->removeFromParent();

  displayList->render(surface.get());

  EXPECT_EQ(displayList->tileCaches.size(), 0lu);

  auto emptyTilesAfter = displayList->emptyTiles.size();
  EXPECT_EQ(9lu, emptyTilesAfter);
}

}  // namespace tgfx

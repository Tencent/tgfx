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
#include "tgfx/layers/Gradient.h"
#include "tgfx/layers/ImagePattern.h"
#include "tgfx/layers/SolidColor.h"
#include "tgfx/layers/VectorLayer.h"
#include "tgfx/layers/vectors/Ellipse.h"
#include "tgfx/layers/vectors/FillStyle.h"
#include "tgfx/layers/vectors/MergePath.h"
#include "tgfx/layers/vectors/Polystar.h"
#include "tgfx/layers/vectors/Rectangle.h"
#include "tgfx/layers/vectors/Repeater.h"
#include "tgfx/layers/vectors/RoundCorner.h"
#include "tgfx/layers/vectors/ShapePath.h"
#include "tgfx/layers/vectors/StrokeStyle.h"
#include "tgfx/layers/vectors/TextSpan.h"
#include "tgfx/layers/vectors/TrimPath.h"
#include "tgfx/layers/vectors/VectorGroup.h"
#include "utils/TestUtils.h"

namespace tgfx {

static std::shared_ptr<FillStyle> MakeFillStyle(const Color& color) {
  auto fill = std::make_shared<FillStyle>();
  fill->setColorSource(SolidColor::Make(color));
  return fill;
}

static std::shared_ptr<StrokeStyle> MakeStrokeStyle(const Color& color, float width) {
  auto stroke = std::make_shared<StrokeStyle>();
  stroke->setColorSource(SolidColor::Make(color));
  stroke->setStrokeWidth(width);
  return stroke;
}

/**
 * Test basic shapes: Rectangle, Ellipse, ShapePath, Polystar with fill and stroke.
 * Each shape is wrapped in a VectorGroup to isolate its styles.
 */
TGFX_TEST(VectorLayerTest, BasicShapes) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 660, 463);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Row 1: Rectangle, Ellipse, Triangle

  // Group 1: Rectangle with red fill and roundness
  auto group1 = std::make_shared<VectorGroup>();
  auto rect = std::make_shared<Rectangle>();
  rect->setCenter({130, 130});
  rect->setSize({160, 160});
  rect->setRoundness(20);
  auto redFill = MakeFillStyle(Color::Red());
  group1->setElements({rect, redFill});

  // Group 2: Ellipse with blue stroke
  auto group2 = std::make_shared<VectorGroup>();
  auto ellipse = std::make_shared<Ellipse>();
  ellipse->setCenter({330, 130});
  ellipse->setSize({160, 120});
  auto blueStroke = MakeStrokeStyle(Color::Blue(), 8.0f);
  group2->setElements({ellipse, blueStroke});

  // Group 3: Triangle with green fill
  auto group3 = std::make_shared<VectorGroup>();
  Path trianglePath = {};
  trianglePath.moveTo(530, 50);
  trianglePath.lineTo(450, 210);
  trianglePath.lineTo(610, 210);
  trianglePath.close();
  auto shapePath = std::make_shared<ShapePath>();
  shapePath->setPath(trianglePath);
  auto greenFill = MakeFillStyle(Color::Green());
  group3->setElements({shapePath, greenFill});

  // Row 2: Star with roundness, Polygon with roundness

  // Group 4: Star with yellow fill and roundness
  auto group4 = std::make_shared<VectorGroup>();
  auto star = std::make_shared<Polystar>();
  star->setCenter({130, 330});
  star->setPolystarType(PolystarType::Star);
  star->setPointCount(5);
  star->setOuterRadius(80);
  star->setInnerRadius(40);
  star->setOuterRoundness(0.5f);
  star->setInnerRoundness(0.3f);
  auto yellowFill = MakeFillStyle(Color::FromRGBA(255, 200, 0, 255));
  group4->setElements({star, yellowFill});

  // Group 5: Six-pointed star without roundness for comparison
  auto group5 = std::make_shared<VectorGroup>();
  auto starSharp = std::make_shared<Polystar>();
  starSharp->setCenter({310, 330});
  starSharp->setPolystarType(PolystarType::Star);
  starSharp->setPointCount(6);
  starSharp->setOuterRadius(80);
  starSharp->setInnerRadius(40);
  auto orangeFill = MakeFillStyle(Color::FromRGBA(255, 128, 0, 255));
  group5->setElements({starSharp, orangeFill});

  // Group 6: Hexagon with purple stroke and roundness
  auto group6 = std::make_shared<VectorGroup>();
  auto polygon = std::make_shared<Polystar>();
  polygon->setCenter({530, 330});
  polygon->setPolystarType(PolystarType::Polygon);
  polygon->setPointCount(6);
  polygon->setOuterRadius(80);
  polygon->setOuterRoundness(0.5f);
  auto purpleStroke = MakeStrokeStyle(Color::FromRGBA(128, 0, 128, 255), 6.0f);
  group6->setElements({polygon, purpleStroke});

  vectorLayer->setContents({group1, group2, group3, group4, group5, group6});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/BasicShapes"));
}

/**
 * Test TrimPath: both Simultaneously and Individually modes.
 * TrimPath should affect the innermost shapes before styles are applied.
 */
TGFX_TEST(VectorLayerTest, TrimPath) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 574, 308);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Group 1: TrimPath Simultaneously (each shape trimmed independently with same params)
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({100, 154});

  auto rect1 = std::make_shared<Rectangle>();
  rect1->setSize({100, 200});

  auto ellipse1 = std::make_shared<Ellipse>();
  ellipse1->setCenter({110, 0});
  ellipse1->setSize({100, 200});

  auto trim1 = std::make_shared<TrimPath>();
  trim1->setStart(0.0f);
  trim1->setEnd(0.5f);
  trim1->setTrimType(TrimPathType::Simultaneously);

  auto stroke1 = MakeStrokeStyle(Color::Red(), 8.0f);
  group1->setElements({rect1, ellipse1, trim1, stroke1});

  // Group 2: TrimPath Individually (all shapes combined into one path, trimmed sequentially)
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({360, 154});

  auto rect2 = std::make_shared<Rectangle>();
  rect2->setSize({100, 200});

  auto ellipse2 = std::make_shared<Ellipse>();
  ellipse2->setCenter({110, 0});
  ellipse2->setSize({100, 200});

  auto trim2 = std::make_shared<TrimPath>();
  trim2->setStart(0.25f);
  trim2->setEnd(0.75f);
  trim2->setTrimType(TrimPathType::Individually);

  auto stroke2 = MakeStrokeStyle(Color::Blue(), 8.0f);
  group2->setElements({rect2, ellipse2, trim2, stroke2});

  vectorLayer->setContents({group1, group2});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/TrimPath"));
}

/**
 * Test TrimPath with reversed direction (start > end).
 * When start > end, the path wraps around, showing the complement of normal trim.
 */
TGFX_TEST(VectorLayerTest, TrimPathReversed) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 548, 308);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Group 1: Normal trim (start < end), shows 20%-70%
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({154, 154});

  auto rect1 = std::make_shared<Rectangle>();
  rect1->setSize({200, 200});

  auto trim1 = std::make_shared<TrimPath>();
  trim1->setStart(0.2f);
  trim1->setEnd(0.7f);

  auto stroke1 = MakeStrokeStyle(Color::Red(), 8.0f);
  group1->setElements({rect1, trim1, stroke1});

  // Group 2: Reversed trim (start > end), shows 70%-100% and 0%-20% (complement)
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({394, 154});

  auto rect2 = std::make_shared<Rectangle>();
  rect2->setSize({200, 200});

  auto trim2 = std::make_shared<TrimPath>();
  trim2->setStart(0.2f);
  trim2->setEnd(0.7f);
  trim2->setOffset(180);

  auto stroke2 = MakeStrokeStyle(Color::Blue(), 8.0f);
  group2->setElements({rect2, trim2, stroke2});

  vectorLayer->setContents({group1, group2});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/TrimPathReversed"));
}

/**
 * Test RoundCorner: should affect innermost shapes.
 */
TGFX_TEST(VectorLayerTest, RoundCorner) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 540, 300);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Rectangle without RoundCorner
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({150, 150});

  auto rect1 = std::make_shared<Rectangle>();
  rect1->setSize({200, 200});

  auto fill1 = MakeFillStyle(Color::Red());
  group1->setElements({rect1, fill1});

  // Rectangle with RoundCorner
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({390, 150});

  auto rect2 = std::make_shared<Rectangle>();
  rect2->setSize({200, 200});

  auto roundCorner = std::make_shared<RoundCorner>();
  roundCorner->setRadius(40.0f);

  auto fill2 = MakeFillStyle(Color::Blue());
  group2->setElements({rect2, roundCorner, fill2});

  vectorLayer->setContents({group1, group2});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/RoundCorner"));
}

/**
 * Test MergePath: should merge shapes and clear painters.
 */
TGFX_TEST(VectorLayerTest, MergePath) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 600, 300);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Group 1: Two overlapping rectangles without merge (overlap area is darker)
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({130, 130});

  auto rect1a = std::make_shared<Rectangle>();
  rect1a->setSize({160, 160});

  auto rect1b = std::make_shared<Rectangle>();
  rect1b->setCenter({60, 40});
  rect1b->setSize({160, 160});

  auto fill1 = MakeFillStyle(Color::FromRGBA(255, 0, 0, 128));
  group1->setElements({rect1a, rect1b, fill1});

  // Group 2: Two rectangles with MergePath XOR (overlap area is hollow)
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({410, 130});

  auto rect2a = std::make_shared<Rectangle>();
  rect2a->setSize({160, 160});

  auto rect2b = std::make_shared<Rectangle>();
  rect2b->setCenter({60, 40});
  rect2b->setSize({160, 160});

  auto merge = std::make_shared<MergePath>();
  merge->setPathOp(PathOp::XOR);

  auto fill2 = MakeFillStyle(Color::FromRGBA(0, 0, 255, 128));
  group2->setElements({rect2a, rect2b, merge, fill2});

  vectorLayer->setContents({group1, group2});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/MergePath"));
}

/**
 * Test MergePath clears existing painters.
 * Fill before MergePath should not render.
 */
TGFX_TEST(VectorLayerTest, MergePathClearsPainters) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 300, 300);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  auto rect = std::make_shared<Rectangle>();
  rect->setCenter({150, 150});
  rect->setSize({200, 200});

  // This fill should be cleared by MergePath
  auto redFill = MakeFillStyle(Color::Red());

  auto merge = std::make_shared<MergePath>();
  merge->setPathOp(PathOp::Append);

  // Only this fill should render
  auto blueFill = MakeFillStyle(Color::Blue());

  vectorLayer->setContents({rect, redFill, merge, blueFill});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/MergePathClearsPainters"));
}

/**
 * Test Repeater: BelowOriginal and AboveOriginal orders.
 * Using stroke to show the layer order difference.
 * BelowOriginal: copies are added to the end, so later copies (rightmost) are on top
 * AboveOriginal: copies are added to the front, so earlier copies (leftmost) are on top
 */
TGFX_TEST(VectorLayerTest, Repeater) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 834, 264);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Group 1: Repeater BelowOriginal
  // Later copies are drawn on top, so the rightmost (faintest) rectangle is on top
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({112, 132});

  auto rect1 = std::make_shared<Rectangle>();
  rect1->setSize({120, 160});

  auto fill1 = MakeFillStyle(Color::Red());
  auto stroke1 = MakeStrokeStyle(Color::Black(), 3.0f);

  auto repeater1 = std::make_shared<Repeater>();
  repeater1->setCopies(4);
  repeater1->setPosition({70, 0});
  repeater1->setOrder(RepeaterOrder::BelowOriginal);
  repeater1->setStartAlpha(1.0f);
  repeater1->setEndAlpha(0.25f);

  group1->setElements({rect1, fill1, stroke1, repeater1});

  // Group 2: Repeater AboveOriginal
  // Earlier copies are drawn on top, so the leftmost (most opaque) rectangle is on top
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({512, 132});

  auto rect2 = std::make_shared<Rectangle>();
  rect2->setSize({120, 160});

  auto fill2 = MakeFillStyle(Color::Blue());
  auto stroke2 = MakeStrokeStyle(Color::Black(), 3.0f);

  auto repeater2 = std::make_shared<Repeater>();
  repeater2->setCopies(4);
  repeater2->setPosition({70, 0});
  repeater2->setOrder(RepeaterOrder::AboveOriginal);
  repeater2->setStartAlpha(1.0f);
  repeater2->setEndAlpha(0.25f);

  group2->setElements({rect2, fill2, stroke2, repeater2});

  vectorLayer->setContents({group1, group2});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/Repeater"));
}

/**
 * Test Repeater with rotation and scale.
 */
TGFX_TEST(VectorLayerTest, RepeaterTransform) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 260, 260);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  auto group = std::make_shared<VectorGroup>();
  group->setPosition({130, 130});

  auto rect = std::make_shared<Rectangle>();
  rect->setSize({160, 40});

  auto repeater = std::make_shared<Repeater>();
  repeater->setCopies(8);
  repeater->setRotation(45.0f);
  repeater->setAnchorPoint({0, 0});
  repeater->setStartAlpha(1.0f);
  repeater->setEndAlpha(1.0f);

  auto fill = MakeFillStyle(Color::FromRGBA(0, 128, 255, 128));
  group->setElements({rect, repeater, fill});

  vectorLayer->setContents({group});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/RepeaterTransform"));
}

/**
 * Test TrimPath and RoundCorner applied after FillStyle/StrokeStyle.
 * They should still affect the shapes because they modify innermost shapes.
 */
TGFX_TEST(VectorLayerTest, ModifiersAfterStyles) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 540, 304);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Group 1: TrimPath after stroke - should still trim
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({150, 152});

  auto rect1 = std::make_shared<Rectangle>();
  rect1->setSize({200, 200});

  auto stroke1 = MakeStrokeStyle(Color::Red(), 8.0f);

  auto trim1 = std::make_shared<TrimPath>();
  trim1->setStart(0.0f);
  trim1->setEnd(0.5f);

  group1->setElements({rect1, stroke1, trim1});

  // Group 2: RoundCorner after fill - should still round
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({390, 152});

  auto rect2 = std::make_shared<Rectangle>();
  rect2->setSize({200, 200});

  auto fill2 = MakeFillStyle(Color::Blue());

  auto roundCorner = std::make_shared<RoundCorner>();
  roundCorner->setRadius(40.0f);

  group2->setElements({rect2, fill2, roundCorner});

  vectorLayer->setContents({group1, group2});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/ModifiersAfterStyles"));
}

/**
 * Test VectorGroup transform properties.
 */
TGFX_TEST(VectorLayerTest, VectorGroupTransform) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 776, 415);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Group with rotation
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({189, 269});
  group1->setRotation(30.0f);
  group1->setAnchorPoint({80, 80});

  auto rect1 = std::make_shared<Rectangle>();
  rect1->setSize({160, 160});

  auto fill1 = MakeFillStyle(Color::Red());
  group1->setElements({rect1, fill1});

  // Group with scale and alpha
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({409, 269});
  group2->setScale({1.2f, 1.2f});
  group2->setAlpha(0.6f);

  auto rect2 = std::make_shared<Rectangle>();
  rect2->setSize({160, 160});

  auto fill2 = MakeFillStyle(Color::Blue());
  group2->setElements({rect2, fill2});

  // Group with skew
  auto group3 = std::make_shared<VectorGroup>();
  group3->setPosition({609, 269});
  group3->setSkew(25.0f);
  group3->setSkewAxis(0.0f);

  auto rect3 = std::make_shared<Rectangle>();
  rect3->setSize({160, 160});

  auto fill3 = MakeFillStyle(Color::Green());
  group3->setElements({rect3, fill3});

  vectorLayer->setContents({group1, group2, group3});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/VectorGroupTransform"));
}

/**
 * Test nested VectorGroups.
 */
TGFX_TEST(VectorLayerTest, NestedGroups) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 437, 437);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Outer group
  auto outerGroup = std::make_shared<VectorGroup>();
  outerGroup->setPosition({206, 206});
  outerGroup->setRotation(45.0f);

  // Inner group 1
  auto innerGroup1 = std::make_shared<VectorGroup>();
  innerGroup1->setPosition({-100, 0});

  auto rect1 = std::make_shared<Rectangle>();
  rect1->setSize({120, 120});

  auto fill1 = MakeFillStyle(Color::Red());
  innerGroup1->setElements({rect1, fill1});

  // Inner group 2
  auto innerGroup2 = std::make_shared<VectorGroup>();
  innerGroup2->setPosition({100, 0});
  innerGroup2->setScale({1.3f, 1.3f});

  auto rect2 = std::make_shared<Rectangle>();
  rect2->setSize({120, 120});

  auto fill2 = MakeFillStyle(Color::FromRGBA(0, 0, 255, 120));
  innerGroup2->setElements({rect2, fill2});

  outerGroup->setElements({innerGroup1, innerGroup2});

  vectorLayer->setContents({outerGroup});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/NestedGroups"));
}

/**
 * Test multiple fills and strokes on the same shape.
 */
TGFX_TEST(VectorLayerTest, MultipleFillsAndStrokes) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 360, 360);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  auto rect = std::make_shared<Rectangle>();
  rect->setCenter({180, 180});
  rect->setSize({240, 240});

  // First fill (bottom)
  auto fill1 = MakeFillStyle(Color::Red());

  // First stroke
  auto stroke1 = MakeStrokeStyle(Color::Blue(), 20.0f);

  // Second stroke (on top)
  auto stroke2 = MakeStrokeStyle(Color(1.0f, 1.0f, 0.0f), 8.0f);

  vectorLayer->setContents({rect, fill1, stroke1, stroke2});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/MultipleFillsAndStrokes"));
}

/**
 * Test FillStyle with different fill rules.
 * Uses concentric rectangles with opposite winding directions.
 * Winding: both rectangles filled (same direction counts add up)
 * EvenOdd: inner rectangle creates a hole (crossing count is even)
 */
TGFX_TEST(VectorLayerTest, FillRule) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 460, 260);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Create concentric rectangles path - outer CW, inner CW (same direction)
  // With Winding: both filled (winding number is non-zero everywhere)
  // With EvenOdd: inner has hole (even crossing count)
  Path concentricPath = {};
  // Outer rectangle (clockwise)
  concentricPath.moveTo(0, 0);
  concentricPath.lineTo(160, 0);
  concentricPath.lineTo(160, 160);
  concentricPath.lineTo(0, 160);
  concentricPath.close();
  // Inner rectangle (also clockwise - same direction)
  concentricPath.moveTo(40, 40);
  concentricPath.lineTo(120, 40);
  concentricPath.lineTo(120, 120);
  concentricPath.lineTo(40, 120);
  concentricPath.close();

  // Group 1: Winding fill rule - both rectangles filled solid
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({50, 50});

  auto shape1 = std::make_shared<ShapePath>();
  shape1->setPath(concentricPath);

  auto fill1 = std::make_shared<FillStyle>();
  fill1->setColorSource(SolidColor::Make(Color::Red()));
  fill1->setFillRule(PathFillType::Winding);

  group1->setElements({shape1, fill1});

  // Group 2: EvenOdd fill rule - inner rectangle creates a hole
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({250, 50});

  auto shape2 = std::make_shared<ShapePath>();
  shape2->setPath(concentricPath);

  auto fill2 = std::make_shared<FillStyle>();
  fill2->setColorSource(SolidColor::Make(Color::Blue()));
  fill2->setFillRule(PathFillType::EvenOdd);

  group2->setElements({shape2, fill2});

  vectorLayer->setContents({group1, group2});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/FillRule"));
}

/**
 * Test TrimPath with offset covering various edge cases:
 * - Normal trim (no wrap-around)
 * - Wrap-around trim (offset causes crossing the start point)
 * - Chained TrimPath (two consecutive trims to verify path order)
 */
TGFX_TEST(VectorLayerTest, TrimPathOffset) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 520, 160);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Group 1: Normal trim (0% to 25%, offset 90 degrees = 0.25 to 0.5)
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({28, 80});

  auto ellipse1 = std::make_shared<Ellipse>();
  ellipse1->setSize({120, 120});

  auto trim1 = std::make_shared<TrimPath>();
  trim1->setStart(0.0f);
  trim1->setEnd(0.25f);
  trim1->setOffset(90.0f);

  auto stroke1 = MakeStrokeStyle(Color::Red(), 12.0f);

  group1->setElements({ellipse1, trim1, stroke1});

  // Group 2: Wrap-around trim (0% to 25%, offset 315 degrees = 0.875 to 1.125, wraps to 0.125)
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({168, 80});

  auto ellipse2 = std::make_shared<Ellipse>();
  ellipse2->setSize({120, 120});

  auto trim2 = std::make_shared<TrimPath>();
  trim2->setStart(0.0f);
  trim2->setEnd(0.25f);
  trim2->setOffset(315.0f);

  auto stroke2 = MakeStrokeStyle(Color::Blue(), 12.0f);

  group2->setElements({ellipse2, trim2, stroke2});

  // Group 3: Verify inverted trim segment order using an open polyline.
  // First trim with offset creates inverted mode (start > end after offset), producing two
  // disconnected segments. Second trim reduces from start, revealing which segment comes first.
  auto group3 = std::make_shared<VectorGroup>();
  group3->setPosition({308, 80});

  // Open polyline: 4 segments forming an open rectangle shape
  Path polyline = {};
  polyline.moveTo(-60, -60);
  polyline.lineTo(60, -60);
  polyline.lineTo(60, 60);
  polyline.lineTo(-60, 60);
  auto shapePath3 = std::make_shared<ShapePath>();
  shapePath3->setPath(polyline);

  // First trim: 75% length with 180 degree offset on open path triggers inverted mode,
  // producing [stopT, 1] + [0, startT] = two disconnected segments
  auto trim3a = std::make_shared<TrimPath>();
  trim3a->setStart(0.0f);
  trim3a->setEnd(0.75f);
  trim3a->setOffset(180.0f);

  // Second trim: take last 80%, this will cut from the actual start of the result path,
  // showing which segment is truly first in the inverted output
  auto trim3b = std::make_shared<TrimPath>();
  trim3b->setStart(0.2f);
  trim3b->setEnd(1.0f);
  trim3b->setTrimType(TrimPathType::Individually);

  auto stroke3 = MakeStrokeStyle(Color::Green(), 12.0f);

  group3->setElements({shapePath3, trim3a, trim3b, stroke3});

  // Group 4: Rectangle wrap-around (to show seamless connection at corner)
  auto group4 = std::make_shared<VectorGroup>();
  group4->setPosition({448, 80});

  auto rect4 = std::make_shared<Rectangle>();
  rect4->setSize({120, 120});

  auto trim4 = std::make_shared<TrimPath>();
  trim4->setStart(0.0f);
  trim4->setEnd(0.5f);
  trim4->setOffset(315.0f);  // Wrap around the starting corner

  auto stroke4 = MakeStrokeStyle(Color::FromRGBA(255, 128, 0, 255), 12.0f);

  group4->setElements({rect4, trim4, stroke4});

  vectorLayer->setContents({group1, group2, group3, group4});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/TrimPathOffset"));
}

/**
 * Test TrimPath reversed direction (end < start) with wrap-around.
 * This tests that reversed trim with wrap-around produces seamless connection.
 * When reversed and wrap-around occurs, the two segments should connect seamlessly.
 */
TGFX_TEST(VectorLayerTest, TrimPathReversedWrapAround) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 520, 160);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Group 1: Normal reversed trim on ellipse (no wrap-around)
  // start=0.6, end=0.2 means reversed: shows path from 80% to 40% in reverse direction
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({28, 80});

  auto ellipse1 = std::make_shared<Ellipse>();
  ellipse1->setSize({120, 120});

  auto trim1 = std::make_shared<TrimPath>();
  trim1->setStart(0.6f);
  trim1->setEnd(0.2f);

  auto stroke1 = MakeStrokeStyle(Color::Red(), 12.0f);

  group1->setElements({ellipse1, trim1, stroke1});

  // Group 2: Reversed trim with wrap-around on ellipse
  // start=0.3, end=0.7 with offset=-90 degrees (-0.25) becomes start=0.05, end=0.45
  // Then reversed: start=0.95, end=0.55, which wraps around
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({168, 80});

  auto ellipse2 = std::make_shared<Ellipse>();
  ellipse2->setSize({120, 120});

  auto trim2 = std::make_shared<TrimPath>();
  trim2->setStart(0.7f);
  trim2->setEnd(0.3f);
  trim2->setOffset(-90.0f);

  auto stroke2 = MakeStrokeStyle(Color::Blue(), 12.0f);

  group2->setElements({ellipse2, trim2, stroke2});

  // Group 3: Reversed trim on rectangle with wrap-around (tests seamless corner connection)
  // The wrap-around should produce a seamless connection at the corner
  auto group3 = std::make_shared<VectorGroup>();
  group3->setPosition({308, 80});

  auto rect3 = std::make_shared<Rectangle>();
  rect3->setSize({120, 120});

  auto trim3 = std::make_shared<TrimPath>();
  trim3->setStart(0.6f);
  trim3->setEnd(0.2f);
  trim3->setOffset(45.0f);  // Offset to make wrap-around cross a corner

  auto stroke3 = MakeStrokeStyle(Color::Green(), 12.0f);

  group3->setElements({rect3, trim3, stroke3});

  // Group 4: Reversed trim Individually mode with multiple shapes
  // Tests that reversed trim works correctly when trimming multiple shapes as one
  auto group4 = std::make_shared<VectorGroup>();
  group4->setPosition({448, 80});

  auto rect4a = std::make_shared<Rectangle>();
  rect4a->setCenter({-30, 0});
  rect4a->setSize({60, 120});

  auto rect4b = std::make_shared<Rectangle>();
  rect4b->setCenter({30, 0});
  rect4b->setSize({60, 120});

  auto trim4 = std::make_shared<TrimPath>();
  trim4->setStart(0.7f);
  trim4->setEnd(0.3f);
  trim4->setTrimType(TrimPathType::Individually);

  auto stroke4 = MakeStrokeStyle(Color::FromRGBA(255, 128, 0, 255), 8.0f);

  group4->setElements({rect4a, rect4b, trim4, stroke4});

  vectorLayer->setContents({group1, group2, group3, group4});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/TrimPathReversedWrapAround"));
}

/**
 * Test complex composition: Repeater + TrimPath + RoundCorner.
 */
TGFX_TEST(VectorLayerTest, ComplexComposition) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 266, 266);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  auto group = std::make_shared<VectorGroup>();
  group->setPosition({133, 133});

  auto rect = std::make_shared<Rectangle>();
  rect->setSize({160, 32});

  auto roundCorner = std::make_shared<RoundCorner>();
  roundCorner->setRadius(10.0f);

  auto repeater = std::make_shared<Repeater>();
  repeater->setCopies(12);
  repeater->setRotation(30.0f);
  repeater->setAnchorPoint({0, 0});

  auto trim = std::make_shared<TrimPath>();
  trim->setStart(0.48f);
  trim->setEnd(0.83f);

  auto stroke = MakeStrokeStyle(Color::FromRGBA(0, 100, 200, 255), 6.0f);
  stroke->setLineCap(LineCap::Round);

  group->setElements({rect, roundCorner, repeater, trim, stroke});

  vectorLayer->setContents({group});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/ComplexComposition"));
}

/**
 * Test RoundCorner with non-uniform scale.
 * RoundCorner operates in original shape coordinates, ignoring accumulated matrix.
 * Left: RoundCorner without MergePath - radius applied in original coords, then scale stretches
 * Right: Nested group with MergePath - inner group has scale, MergePath bakes it, then RoundCorner
 *        applies uniform radius on the already-stretched shape
 */
TGFX_TEST(VectorLayerTest, RoundCornerWithScale) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Group 1: RoundCorner without MergePath, with non-uniform scale
  // RoundCorner applies in original coords, then scale stretches the result
  // The radius appears stretched (wider horizontally)
  auto group1 = std::make_shared<VectorGroup>();
  group1->setScale({2.0f, 1.0f});

  auto rect1 = std::make_shared<Rectangle>();
  rect1->setSize({100, 100});

  auto roundCorner1 = std::make_shared<RoundCorner>();
  roundCorner1->setRadius(30.0f);

  auto fill1 = MakeFillStyle(Color::Red());
  group1->setElements({rect1, roundCorner1, fill1});

  // Group 2: Nested structure to demonstrate MergePath baking matrix before RoundCorner
  // Inner group has scale, MergePath bakes it into the shape, then RoundCorner applies
  // The radius is uniform because it's applied after the scale is already baked in
  auto outerGroup2 = std::make_shared<VectorGroup>();

  auto innerGroup2 = std::make_shared<VectorGroup>();
  innerGroup2->setScale({2.0f, 1.0f});

  auto rect2 = std::make_shared<Rectangle>();
  rect2->setSize({100, 100});
  innerGroup2->setElements({rect2});

  auto merge2 = std::make_shared<MergePath>();
  merge2->setPathOp(PathOp::Append);

  auto roundCorner2 = std::make_shared<RoundCorner>();
  roundCorner2->setRadius(30.0f);

  auto fill2 = MakeFillStyle(Color::Blue());
  outerGroup2->setElements({innerGroup2, merge2, roundCorner2, fill2});

  vectorLayer->setContents({group1, outerGroup2});

  // Calculate bounds and create surface
  displayList->root()->addChild(vectorLayer);
  auto bounds = vectorLayer->getBounds();
  group1->setPosition({-bounds.x() + 50, -bounds.y() + 50});
  outerGroup2->setPosition({-bounds.x() + 50 + 250, -bounds.y() + 50});
  bounds = vectorLayer->getBounds();
  auto surface = Surface::Make(context, static_cast<int>(bounds.width()) + 100,
                               static_cast<int>(bounds.height()) + 100);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/RoundCornerWithScale"));
}

/**
 * Test Stroke with non-uniform scale applied at different levels.
 * Stroke handles non-uniform scale by converting to fill first.
 * All three rectangles have similar final size for comparison.
 * Stroke is applied at outer group level to test accumulated matrix effect.
 */
TGFX_TEST(VectorLayerTest, StrokeNestedScale) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Group 1: Non-uniform scale at inner group level
  // Stroke sees accumulated matrix (2,1), converts to fill with uniform stroke width
  // Final size: 60*2 x 60*1 = 120x60
  auto outerGroup1 = std::make_shared<VectorGroup>();

  auto innerGroup1 = std::make_shared<VectorGroup>();
  innerGroup1->setScale({2.0f, 1.0f});

  auto rect1 = std::make_shared<Rectangle>();
  rect1->setSize({60, 60});
  innerGroup1->setElements({rect1});

  auto stroke1 = MakeStrokeStyle(Color::Red(), 8.0f);
  outerGroup1->setElements({innerGroup1, stroke1});

  // Group 2: Non-uniform scale at outer group only
  // Stroke sees accumulated matrix (2,1), converts to fill with uniform stroke width
  // Final size: 60*2 x 60*1 = 120x60
  // Should look identical to Group 1
  auto outerGroup2 = std::make_shared<VectorGroup>();
  outerGroup2->setScale({2.0f, 1.0f});

  auto innerGroup2 = std::make_shared<VectorGroup>();
  // No scale on inner group

  auto rect2 = std::make_shared<Rectangle>();
  rect2->setSize({60, 60});
  innerGroup2->setElements({rect2});

  auto stroke2 = MakeStrokeStyle(Color::Blue(), 8.0f);
  outerGroup2->setElements({innerGroup2, stroke2});

  // Group 3: Inner and outer scales cancel out to uniform
  // Inner: (0.5, 2), Outer: (2, 0.5), Combined: (1, 1)
  // Stroke sees uniform matrix, applies normal 8px stroke
  // Final size: 60*0.5*2 x 60*2*0.5 = 60x60
  auto outerGroup3 = std::make_shared<VectorGroup>();
  outerGroup3->setScale({2.0f, 0.5f});

  auto innerGroup3 = std::make_shared<VectorGroup>();
  innerGroup3->setScale({0.5f, 2.0f});

  auto rect3 = std::make_shared<Rectangle>();
  rect3->setSize({60, 60});
  innerGroup3->setElements({rect3});

  auto stroke3 = MakeStrokeStyle(Color::Green(), 8.0f);
  outerGroup3->setElements({innerGroup3, stroke3});

  vectorLayer->setContents({outerGroup1, outerGroup2, outerGroup3});

  // Calculate bounds and create surface
  displayList->root()->addChild(vectorLayer);
  auto bounds = vectorLayer->getBounds();
  outerGroup1->setPosition({-bounds.x() + 50, -bounds.y() + 50});
  outerGroup2->setPosition({-bounds.x() + 50 + 180, -bounds.y() + 50});
  outerGroup3->setPosition({-bounds.x() + 50 + 360, -bounds.y() + 50});
  bounds = vectorLayer->getBounds();
  auto surface = Surface::Make(context, static_cast<int>(bounds.width()) + 100,
                               static_cast<int>(bounds.height()) + 100);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/StrokeNestedScale"));
}

/**
 * Test TrimPath with non-uniform scale.
 * TrimPath operates in original shape coordinates, similar to RoundCorner.
 * Left: TrimPath without MergePath - trim based on original path length
 * Right: Nested group with MergePath - inner group has scale, MergePath bakes it,
 *        then TrimPath applies to already-stretched path
 * Using 25% trim to show the difference more clearly.
 */
TGFX_TEST(VectorLayerTest, TrimPathWithScale) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Group 1: TrimPath without MergePath, with non-uniform scale
  // Original 80x80 rect perimeter = 320px, 25% = 80px
  // Rectangle starts from left edge middle (startIndex=2), goes: left-bottom -> bottom -> right
  // 80px = left-bottom(40) + bottom(40), stops at bottom edge
  // Then scale(2,1) stretches horizontally: bottom becomes 80px visually
  auto group1 = std::make_shared<VectorGroup>();
  group1->setScale({2.0f, 1.0f});

  auto rect1 = std::make_shared<Rectangle>();
  rect1->setSize({80, 80});

  auto trim1 = std::make_shared<TrimPath>();
  trim1->setStart(0.0f);
  trim1->setEnd(0.25f);

  auto stroke1 = MakeStrokeStyle(Color::Red(), 6.0f);
  group1->setElements({rect1, trim1, stroke1});

  // Group 2: Nested structure - inner group has scale, MergePath bakes it
  // Stretched 160x80 rect perimeter = 480px, 25% = 120px
  // 120px = left-bottom(40) + bottom(80), stops further along bottom edge
  auto outerGroup2 = std::make_shared<VectorGroup>();

  auto innerGroup2 = std::make_shared<VectorGroup>();
  innerGroup2->setScale({2.0f, 1.0f});

  auto rect2 = std::make_shared<Rectangle>();
  rect2->setSize({80, 80});
  innerGroup2->setElements({rect2});

  auto merge2 = std::make_shared<MergePath>();
  merge2->setPathOp(PathOp::Append);

  auto trim2 = std::make_shared<TrimPath>();
  trim2->setStart(0.0f);
  trim2->setEnd(0.25f);

  auto stroke2 = MakeStrokeStyle(Color::Blue(), 6.0f);
  outerGroup2->setElements({innerGroup2, merge2, trim2, stroke2});

  vectorLayer->setContents({group1, outerGroup2});

  // Calculate bounds and create surface
  displayList->root()->addChild(vectorLayer);
  auto bounds = vectorLayer->getBounds();
  group1->setPosition({-bounds.x() + 50, -bounds.y() + 50});
  outerGroup2->setPosition({-bounds.x() + 50 + 220, -bounds.y() + 50});
  bounds = vectorLayer->getBounds();
  auto surface = Surface::Make(context, static_cast<int>(bounds.width()) + 100,
                               static_cast<int>(bounds.height()) + 100);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/TrimPathWithScale"));
}

/**
 * Test Repeater with non-uniform scale.
 * Repeater copies shapes and applies incremental transforms.
 * The scale accumulates with each copy.
 */
TGFX_TEST(VectorLayerTest, RepeaterWithScale) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Group 1: Repeater with non-uniform scale per copy
  // Each copy gets progressively more stretched
  auto group1 = std::make_shared<VectorGroup>();

  auto rect1 = std::make_shared<Rectangle>();
  rect1->setSize({40, 40});

  auto repeater1 = std::make_shared<Repeater>();
  repeater1->setCopies(4);
  repeater1->setPosition({100, 0});
  repeater1->setScale({1.3f, 1.0f});  // Non-uniform scale per copy
  repeater1->setAnchorPoint({20, 20});

  auto stroke1 = MakeStrokeStyle(Color::Red(), 3.0f);
  auto fill1 = MakeFillStyle(Color::FromRGBA(255, 0, 0, 80));
  group1->setElements({rect1, fill1, stroke1, repeater1});

  // Group 2: Repeater inside a non-uniformly scaled group
  // The group scale affects all copies uniformly
  auto group2 = std::make_shared<VectorGroup>();
  group2->setScale({1.5f, 1.0f});

  auto rect2 = std::make_shared<Rectangle>();
  rect2->setSize({40, 40});

  auto repeater2 = std::make_shared<Repeater>();
  repeater2->setCopies(5);
  repeater2->setPosition({50, 0});
  repeater2->setAnchorPoint({20, 20});

  auto stroke2 = MakeStrokeStyle(Color::Blue(), 3.0f);
  auto fill2 = MakeFillStyle(Color::FromRGBA(0, 0, 255, 80));
  group2->setElements({rect2, fill2, stroke2, repeater2});

  vectorLayer->setContents({group1, group2});

  // Calculate bounds and create surface
  displayList->root()->addChild(vectorLayer);
  auto bounds = vectorLayer->getBounds();
  group1->setPosition({-bounds.x() + 50, -bounds.y() + 50});
  group2->setPosition({-bounds.x() + 50, -bounds.y() + 50 + 100});
  bounds = vectorLayer->getBounds();
  auto surface = Surface::Make(context, static_cast<int>(bounds.width()) + 100,
                               static_cast<int>(bounds.height()) + 100);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/RepeaterWithScale"));
}

/**
 * Test MergePath with different PathOps.
 * Verifies Union, Intersect, Difference, and XOR operations.
 */
TGFX_TEST(VectorLayerTest, MergePathOps) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 760, 200);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  auto createOverlappingShapes = [](float offsetX) {
    auto group = std::make_shared<VectorGroup>();
    group->setPosition({offsetX, 100});

    auto rect = std::make_shared<Rectangle>();
    rect->setSize({80, 80});

    auto ellipse = std::make_shared<Ellipse>();
    ellipse->setCenter({40, 0});
    ellipse->setSize({80, 80});

    return std::make_tuple(group, rect, ellipse);
  };

  // Group 1: Union (combines both shapes)
  auto [group1, rect1, ellipse1] = createOverlappingShapes(90);
  auto merge1 = std::make_shared<MergePath>();
  merge1->setPathOp(PathOp::Union);
  auto fill1 = MakeFillStyle(Color::Red());
  group1->setElements({rect1, ellipse1, merge1, fill1});

  // Group 2: Intersect (only overlapping area)
  auto [group2, rect2, ellipse2] = createOverlappingShapes(260);
  auto merge2 = std::make_shared<MergePath>();
  merge2->setPathOp(PathOp::Intersect);
  auto fill2 = MakeFillStyle(Color::Green());
  group2->setElements({rect2, ellipse2, merge2, fill2});

  // Group 3: Difference (first minus second)
  auto [group3, rect3, ellipse3] = createOverlappingShapes(430);
  auto merge3 = std::make_shared<MergePath>();
  merge3->setPathOp(PathOp::Difference);
  auto fill3 = MakeFillStyle(Color::Blue());
  group3->setElements({rect3, ellipse3, merge3, fill3});

  // Group 4: XOR (non-overlapping areas)
  auto [group4, rect4, ellipse4] = createOverlappingShapes(600);
  auto merge4 = std::make_shared<MergePath>();
  merge4->setPathOp(PathOp::XOR);
  auto fill4 = MakeFillStyle(Color::FromRGBA(255, 128, 0, 255));
  group4->setElements({rect4, ellipse4, merge4, fill4});

  vectorLayer->setContents({group1, group2, group3, group4});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/MergePathOps"));
}

/**
 * Test Stroke dash pattern with TrimPath.
 * Verifies the interaction between dash effect and trim.
 */
TGFX_TEST(VectorLayerTest, StrokeDashWithTrim) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 540, 200);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Group 1: Dash only (reference)
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({130, 100});

  auto rect1 = std::make_shared<Rectangle>();
  rect1->setSize({160, 100});

  auto stroke1 = std::make_shared<StrokeStyle>();
  stroke1->setColorSource(SolidColor::Make(Color::Red()));
  stroke1->setStrokeWidth(6.0f);
  stroke1->setDashes({20.0f, 10.0f});

  group1->setElements({rect1, stroke1});

  // Group 2: Trim then Dash (trim affects the path, then dash is applied)
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({340, 100});

  auto rect2 = std::make_shared<Rectangle>();
  rect2->setSize({160, 100});

  auto trim2 = std::make_shared<TrimPath>();
  trim2->setStart(0.0f);
  trim2->setEnd(0.6f);

  auto stroke2 = std::make_shared<StrokeStyle>();
  stroke2->setColorSource(SolidColor::Make(Color::Blue()));
  stroke2->setStrokeWidth(6.0f);
  stroke2->setDashes({20.0f, 10.0f});

  group2->setElements({rect2, trim2, stroke2});

  vectorLayer->setContents({group1, group2});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/StrokeDashWithTrim"));
}

/**
 * Test Repeater with fractional copies and alpha gradient.
 * Fractional copies should partially render the last copy with reduced alpha.
 */
TGFX_TEST(VectorLayerTest, RepeaterEdgeCases) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 620, 200);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Group 1: Fractional copies (2.5 copies)
  // Should show 2 full copies and 1 half-opacity copy
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({90, 100});

  auto rect1 = std::make_shared<Rectangle>();
  rect1->setSize({60, 80});

  auto fill1 = MakeFillStyle(Color::Red());

  auto repeater1 = std::make_shared<Repeater>();
  repeater1->setCopies(2.5f);
  repeater1->setPosition({70, 0});

  group1->setElements({rect1, fill1, repeater1});

  // Group 2: Repeater with offset and alpha gradient
  // 4 copies with alpha from 1.0 to 0.3
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({340, 100});

  auto rect2 = std::make_shared<Rectangle>();
  rect2->setSize({60, 80});

  auto fill2 = MakeFillStyle(Color::Blue());

  auto repeater2 = std::make_shared<Repeater>();
  repeater2->setCopies(4.0f);
  repeater2->setPosition({70, 0});
  repeater2->setStartAlpha(1.0f);
  repeater2->setEndAlpha(0.3f);

  group2->setElements({rect2, fill2, repeater2});

  vectorLayer->setContents({group1, group2});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/RepeaterEdgeCases"));
}

/**
 * Test multiple modifiers in sequence.
 * Verifies the order: RoundCorner -> TrimPath -> Stroke
 */
TGFX_TEST(VectorLayerTest, ChainedModifiers) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 740, 200);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Group 1: Rectangle only (reference)
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({100, 100});

  auto rect1 = std::make_shared<Rectangle>();
  rect1->setSize({120, 100});

  auto stroke1 = MakeStrokeStyle(Color::Red(), 6.0f);
  group1->setElements({rect1, stroke1});

  // Group 2: RoundCorner then Stroke
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({270, 100});

  auto rect2 = std::make_shared<Rectangle>();
  rect2->setSize({120, 100});

  auto round2 = std::make_shared<RoundCorner>();
  round2->setRadius(20.0f);

  auto stroke2 = MakeStrokeStyle(Color::Green(), 6.0f);
  group2->setElements({rect2, round2, stroke2});

  // Group 3: RoundCorner then TrimPath then Stroke
  auto group3 = std::make_shared<VectorGroup>();
  group3->setPosition({440, 100});

  auto rect3 = std::make_shared<Rectangle>();
  rect3->setSize({120, 100});

  auto round3 = std::make_shared<RoundCorner>();
  round3->setRadius(20.0f);

  auto trim3 = std::make_shared<TrimPath>();
  trim3->setStart(0.0f);
  trim3->setEnd(0.6f);

  auto stroke3 = MakeStrokeStyle(Color::Blue(), 6.0f);
  group3->setElements({rect3, round3, trim3, stroke3});

  // Group 4: TrimPath then RoundCorner then Stroke
  // Note: RoundCorner after TrimPath may have different effect
  auto group4 = std::make_shared<VectorGroup>();
  group4->setPosition({610, 100});

  auto rect4 = std::make_shared<Rectangle>();
  rect4->setSize({120, 100});

  auto trim4 = std::make_shared<TrimPath>();
  trim4->setStart(0.0f);
  trim4->setEnd(0.6f);

  auto round4 = std::make_shared<RoundCorner>();
  round4->setRadius(20.0f);

  auto stroke4 = MakeStrokeStyle(Color::FromRGBA(255, 128, 0, 255), 6.0f);
  group4->setElements({rect4, trim4, round4, stroke4});

  vectorLayer->setContents({group1, group2, group3, group4});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/ChainedModifiers"));
}

/**
 * Test Polystar rotation property.
 * Verifies that star/polygon rotation works correctly.
 */
TGFX_TEST(VectorLayerTest, PolystarRotation) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 460, 200);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Group 1: Star without rotation
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({100, 100});

  auto star1 = std::make_shared<Polystar>();
  star1->setPolystarType(PolystarType::Star);
  star1->setPointCount(5);
  star1->setOuterRadius(60);
  star1->setInnerRadius(30);
  star1->setRotation(0.0f);

  auto fill1 = MakeFillStyle(Color::Red());
  group1->setElements({star1, fill1});

  // Group 2: Star with 36 degree rotation (one point up)
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({240, 100});

  auto star2 = std::make_shared<Polystar>();
  star2->setPolystarType(PolystarType::Star);
  star2->setPointCount(5);
  star2->setOuterRadius(60);
  star2->setInnerRadius(30);
  star2->setRotation(-90.0f);  // Rotate to point up

  auto fill2 = MakeFillStyle(Color::Green());
  group2->setElements({star2, fill2});

  // Group 3: Polygon with rotation
  auto group3 = std::make_shared<VectorGroup>();
  group3->setPosition({380, 100});

  auto polygon3 = std::make_shared<Polystar>();
  polygon3->setPolystarType(PolystarType::Polygon);
  polygon3->setPointCount(6);
  polygon3->setOuterRadius(60);
  polygon3->setRotation(30.0f);  // Rotate hexagon

  auto stroke3 = MakeStrokeStyle(Color::Blue(), 4.0f);
  group3->setElements({polygon3, stroke3});

  vectorLayer->setContents({group1, group2, group3});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/PolystarRotation"));
}

/**
 * Test Stroke line join and cap styles.
 */
TGFX_TEST(VectorLayerTest, StrokeJoinCap) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 660, 300);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Create an open path for testing caps
  Path openPath = {};
  openPath.moveTo(-50, 30);
  openPath.lineTo(0, -30);
  openPath.lineTo(50, 30);

  // Row 1: Different line caps
  // Group 1: Butt cap
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({100, 80});

  auto shape1 = std::make_shared<ShapePath>();
  shape1->setPath(openPath);

  auto stroke1 = std::make_shared<StrokeStyle>();
  stroke1->setColorSource(SolidColor::Make(Color::Red()));
  stroke1->setStrokeWidth(16.0f);
  stroke1->setLineCap(LineCap::Butt);
  stroke1->setLineJoin(LineJoin::Miter);

  group1->setElements({shape1, stroke1});

  // Group 2: Round cap
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({250, 80});

  auto shape2 = std::make_shared<ShapePath>();
  shape2->setPath(openPath);

  auto stroke2 = std::make_shared<StrokeStyle>();
  stroke2->setColorSource(SolidColor::Make(Color::Green()));
  stroke2->setStrokeWidth(16.0f);
  stroke2->setLineCap(LineCap::Round);
  stroke2->setLineJoin(LineJoin::Round);

  group2->setElements({shape2, stroke2});

  // Group 3: Square cap
  auto group3 = std::make_shared<VectorGroup>();
  group3->setPosition({400, 80});

  auto shape3 = std::make_shared<ShapePath>();
  shape3->setPath(openPath);

  auto stroke3 = std::make_shared<StrokeStyle>();
  stroke3->setColorSource(SolidColor::Make(Color::Blue()));
  stroke3->setStrokeWidth(16.0f);
  stroke3->setLineCap(LineCap::Square);
  stroke3->setLineJoin(LineJoin::Bevel);

  group3->setElements({shape3, stroke3});

  // Row 2: Different line joins on closed path
  // Group 4: Miter join
  auto group4 = std::make_shared<VectorGroup>();
  group4->setPosition({100, 210});

  auto rect4 = std::make_shared<Rectangle>();
  rect4->setSize({80, 80});

  auto stroke4 = std::make_shared<StrokeStyle>();
  stroke4->setColorSource(SolidColor::Make(Color::Red()));
  stroke4->setStrokeWidth(12.0f);
  stroke4->setLineJoin(LineJoin::Miter);
  stroke4->setMiterLimit(10.0f);

  group4->setElements({rect4, stroke4});

  // Group 5: Round join
  auto group5 = std::make_shared<VectorGroup>();
  group5->setPosition({250, 210});

  auto rect5 = std::make_shared<Rectangle>();
  rect5->setSize({80, 80});

  auto stroke5 = std::make_shared<StrokeStyle>();
  stroke5->setColorSource(SolidColor::Make(Color::Green()));
  stroke5->setStrokeWidth(12.0f);
  stroke5->setLineJoin(LineJoin::Round);

  group5->setElements({rect5, stroke5});

  // Group 6: Bevel join
  auto group6 = std::make_shared<VectorGroup>();
  group6->setPosition({400, 210});

  auto rect6 = std::make_shared<Rectangle>();
  rect6->setSize({80, 80});

  auto stroke6 = std::make_shared<StrokeStyle>();
  stroke6->setColorSource(SolidColor::Make(Color::Blue()));
  stroke6->setStrokeWidth(12.0f);
  stroke6->setLineJoin(LineJoin::Bevel);

  group6->setElements({rect6, stroke6});

  // Group 7: Sharp angle with miter limit
  auto group7 = std::make_shared<VectorGroup>();
  group7->setPosition({550, 80});

  Path sharpPath = {};
  sharpPath.moveTo(-40, 30);
  sharpPath.lineTo(0, -40);
  sharpPath.lineTo(40, 30);

  auto shape7 = std::make_shared<ShapePath>();
  shape7->setPath(sharpPath);

  auto stroke7 = std::make_shared<StrokeStyle>();
  stroke7->setColorSource(SolidColor::Make(Color::FromRGBA(255, 128, 0, 255)));
  stroke7->setStrokeWidth(12.0f);
  stroke7->setLineJoin(LineJoin::Miter);
  stroke7->setMiterLimit(2.0f);  // Low miter limit forces bevel on sharp angles

  group7->setElements({shape7, stroke7});

  vectorLayer->setContents({group1, group2, group3, group4, group5, group6, group7});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/StrokeJoinCap"));
}

// ==================== Text Rendering Tests ====================

static std::shared_ptr<Typeface> GetTestTypeface() {
  return MakeTypeface("resources/font/NotoSansSC-Regular.otf");
}

static std::shared_ptr<Typeface> GetEmojiTypeface() {
  return MakeTypeface("resources/font/NotoColorEmoji.ttf");
}

/**
 * Test basic TextSpan rendering with different positions.
 */
TGFX_TEST(VectorLayerTest, TextSpanBasic) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 291, 240);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  auto typeface = GetTestTypeface();
  if (typeface == nullptr) {
    return;
  }
  Font font(typeface, 36.0f);

  // TextSpan 1: Basic text at origin
  auto textSpan1 = std::make_shared<TextSpan>();
  auto blob1 = TextBlob::MakeFrom("Hello TGFX", font);
  textSpan1->setTextBlob(blob1);
  textSpan1->setPosition({50, 80});

  // TextSpan 2: Text at different position
  auto textSpan2 = std::make_shared<TextSpan>();
  auto blob2 = TextBlob::MakeFrom("Vector Text", font);
  textSpan2->setTextBlob(blob2);
  textSpan2->setPosition({50, 140});

  // TextSpan 3: Smaller font
  Font smallFont(typeface, 24.0f);
  auto textSpan3 = std::make_shared<TextSpan>();
  auto blob3 = TextBlob::MakeFrom("Small Text", smallFont);
  textSpan3->setTextBlob(blob3);
  textSpan3->setPosition({50, 190});

  auto fill = MakeFillStyle(Color::Black());
  vectorLayer->setContents({textSpan1, textSpan2, textSpan3, fill});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/TextSpanBasic"));
}

/**
 * Test TextSpan with VectorGroup transformations.
 */
TGFX_TEST(VectorLayerTest, TextSpanWithGroup) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 494, 226);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  auto typeface = GetTestTypeface();
  if (typeface == nullptr) {
    return;
  }
  Font font(typeface, 32.0f);

  // Group 1: Rotated text
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({50, 76});
  group1->setRotation(15.0f);

  auto textSpan1 = std::make_shared<TextSpan>();
  auto blob1 = TextBlob::MakeFrom("Rotated", font);
  textSpan1->setTextBlob(blob1);

  auto fill1 = MakeFillStyle(Color::Red());
  group1->setElements({textSpan1, fill1});

  // Group 2: Scaled text
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({250, 76});
  group2->setScale({2.0f, 1.0f});

  auto textSpan2 = std::make_shared<TextSpan>();
  auto blob2 = TextBlob::MakeFrom("Scaled", font);
  textSpan2->setTextBlob(blob2);

  auto fill2 = MakeFillStyle(Color::Blue());
  group2->setElements({textSpan2, fill2});

  // Group 3: Skewed text with alpha
  auto group3 = std::make_shared<VectorGroup>();
  group3->setPosition({50, 176});
  group3->setSkew(20.0f);
  group3->setAlpha(0.6f);

  auto textSpan3 = std::make_shared<TextSpan>();
  auto blob3 = TextBlob::MakeFrom("Skewed", font);
  textSpan3->setTextBlob(blob3);

  auto fill3 = MakeFillStyle(Color::Green());
  group3->setElements({textSpan3, fill3});

  vectorLayer->setContents({group1, group2, group3});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/TextSpanWithGroup"));
}

/**
 * Test TextSpan with fill and stroke styles.
 */
TGFX_TEST(VectorLayerTest, TextSpanStyles) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 340, 210);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  auto typeface = GetTestTypeface();
  if (typeface == nullptr) {
    return;
  }
  Font font(typeface, 48.0f);
  font.setFauxBold(true);

  // Group 1: Fill only
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({46, 60});

  auto textSpan1 = std::make_shared<TextSpan>();
  auto blob1 = TextBlob::MakeFrom("Fill", font);
  textSpan1->setTextBlob(blob1);

  auto fill1 = MakeFillStyle(Color::Red());
  group1->setElements({textSpan1, fill1});

  // Group 2: Stroke only
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({141, 60});

  auto textSpan2 = std::make_shared<TextSpan>();
  auto blob2 = TextBlob::MakeFrom("Stroke", font);
  textSpan2->setTextBlob(blob2);

  auto stroke2 = MakeStrokeStyle(Color::Blue(), 2.0f);
  group2->setElements({textSpan2, stroke2});

  // Group 3: Fill and stroke
  auto group3 = std::make_shared<VectorGroup>();
  group3->setPosition({46, 120});

  auto textSpan3 = std::make_shared<TextSpan>();
  auto blob3 = TextBlob::MakeFrom("Fill+Stroke", font);
  textSpan3->setTextBlob(blob3);

  auto fill3 = MakeFillStyle(Color::FromRGBA(255, 200, 0, 255));
  auto stroke3 = MakeStrokeStyle(Color::FromRGBA(200, 100, 0, 255), 2.0f);
  group3->setElements({textSpan3, fill3, stroke3});

  // Group 4: Dash stroke
  auto group4 = std::make_shared<VectorGroup>();
  group4->setPosition({46, 180});

  auto textSpan4 = std::make_shared<TextSpan>();
  auto blob4 = TextBlob::MakeFrom("Dash", font);
  textSpan4->setTextBlob(blob4);

  auto stroke4 = MakeStrokeStyle(Color::Green(), 2.0f);
  stroke4->setDashes({8.0f, 4.0f});
  group4->setElements({textSpan4, stroke4});

  vectorLayer->setContents({group1, group2, group3, group4});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/TextSpanStyles"));
}

/**
 * Test TextSpan with path modifiers (RoundCorner, MergePath).
 * Text is converted to shape when path modifiers are applied.
 */
TGFX_TEST(VectorLayerTest, TextSpanWithPathModifiers) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 399, 222);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  auto typeface = GetTestTypeface();
  if (typeface == nullptr) {
    return;
  }
  Font font(typeface, 48.0f);
  font.setFauxBold(true);

  // Group 1: Text with RoundCorner
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({50, 91});

  auto textSpan1 = std::make_shared<TextSpan>();
  auto blob1 = TextBlob::MakeFrom("Round", font);
  textSpan1->setTextBlob(blob1);

  auto roundCorner = std::make_shared<RoundCorner>();
  roundCorner->setRadius(5.0f);

  auto fill1 = MakeFillStyle(Color::Blue());
  group1->setElements({textSpan1, roundCorner, fill1});

  // Group 2: Text with MergePath (text with emoji, emoji should be discarded after merge)
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({50, 171});

  auto textSpan2a = std::make_shared<TextSpan>();
  auto blob2a = TextBlob::MakeFrom("AB🐼", font);
  textSpan2a->setTextBlob(blob2a);

  auto textSpan2b = std::make_shared<TextSpan>();
  auto blob2b = TextBlob::MakeFrom("CD🎉", font);
  textSpan2b->setTextBlob(blob2b);
  textSpan2b->setPosition({100, 0});

  auto mergePath = std::make_shared<MergePath>();
  mergePath->setPathOp(PathOp::Union);

  auto fill2 = MakeFillStyle(Color::Red());
  group2->setElements({textSpan2a, textSpan2b, mergePath, fill2});

  // Group 3: Text with TrimPath
  auto group3 = std::make_shared<VectorGroup>();
  group3->setPosition({250, 171});

  auto textSpan3 = std::make_shared<TextSpan>();
  auto blob3 = TextBlob::MakeFrom("Trim", font);
  textSpan3->setTextBlob(blob3);

  auto trimPath = std::make_shared<TrimPath>();
  trimPath->setStart(0.2f);
  trimPath->setEnd(0.8f);

  auto stroke3 = MakeStrokeStyle(Color::Black(), 2.0f);
  group3->setElements({textSpan3, trimPath, stroke3});

  vectorLayer->setContents({group1, group2, group3});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/TextSpanWithPathModifiers"));
}

/**
 * Test TextSpan edge cases: empty blob, disabled span, nested groups.
 */
TGFX_TEST(VectorLayerTest, TextSpanEdgeCases) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 368, 204);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  auto typeface = GetTestTypeface();
  if (typeface == nullptr) {
    return;
  }
  Font font(typeface, 32.0f);

  // Group 1: Empty TextBlob (should render nothing)
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({47, 26});

  auto emptySpan = std::make_shared<TextSpan>();
  // textBlob is nullptr by default

  auto fill1 = MakeFillStyle(Color::Red());
  group1->setElements({emptySpan, fill1});

  // Group 2: Disabled TextSpan (should not render)
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({47, 76});

  auto disabledSpan = std::make_shared<TextSpan>();
  auto blob2 = TextBlob::MakeFrom("Disabled", font);
  disabledSpan->setTextBlob(blob2);
  disabledSpan->setEnabled(false);

  // This text should render (enabled by default)
  auto enabledSpan = std::make_shared<TextSpan>();
  auto blob2b = TextBlob::MakeFrom("Enabled", font);
  enabledSpan->setTextBlob(blob2b);
  enabledSpan->setPosition({150, 0});

  auto fill2 = MakeFillStyle(Color::Blue());
  group2->setElements({disabledSpan, enabledSpan, fill2});

  // Group 3: Nested groups with text (transform accumulation)
  auto outerGroup = std::make_shared<VectorGroup>();
  outerGroup->setPosition({47, 136});
  outerGroup->setScale({1.0f, 1.0f});

  auto innerGroup = std::make_shared<VectorGroup>();
  innerGroup->setRotation(10.0f);

  auto nestedSpan = std::make_shared<TextSpan>();
  auto blob3 = TextBlob::MakeFrom("Nested", font);
  nestedSpan->setTextBlob(blob3);

  auto fill3 = MakeFillStyle(Color::Green());
  innerGroup->setElements({nestedSpan, fill3});
  outerGroup->setElements({innerGroup});

  vectorLayer->setContents({group1, group2, outerGroup});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/TextSpanEdgeCases"));
}

/**
 * Test TextSpan with Repeater modifier.
 */
TGFX_TEST(VectorLayerTest, TextSpanWithRepeater) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 386, 218);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  auto typeface = GetTestTypeface();
  if (typeface == nullptr) {
    return;
  }
  Font font(typeface, 24.0f);

  // Text with Repeater
  auto group = std::make_shared<VectorGroup>();
  group->setPosition({50, 68});

  auto textSpan = std::make_shared<TextSpan>();
  auto blob = TextBlob::MakeFrom("ABC", font);
  textSpan->setTextBlob(blob);

  auto repeater = std::make_shared<Repeater>();
  repeater->setCopies(5);
  repeater->setPosition({60, 25});
  repeater->setStartAlpha(1.0f);
  repeater->setEndAlpha(0.3f);

  auto fill = MakeFillStyle(Color::Blue());
  group->setElements({textSpan, fill, repeater});

  vectorLayer->setContents({group});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/TextSpanWithRepeater"));
}

/**
 * Test TextSpan with emoji characters.
 * Emoji are rendered as images, not paths, so they should display correctly with fill.
 * When path modifiers (TrimPath, MergePath, RoundCorner) are applied, emoji will be lost
 * since they don't have path outlines.
 */
TGFX_TEST(VectorLayerTest, TextSpanEmoji) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 372, 318);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  auto textTypeface = GetTestTypeface();
  auto emojiTypeface = GetEmojiTypeface();
  if (textTypeface == nullptr || emojiTypeface == nullptr) {
    return;
  }

  Font textFont(textTypeface, 32.0f);
  Font emojiFont(emojiTypeface, 32.0f);

  // Group 1: Mixed text and emoji with fill
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({50, 80});

  auto textSpan1 = std::make_shared<TextSpan>();
  auto blob1 = TextBlob::MakeFrom("Hello ", textFont);
  textSpan1->setTextBlob(blob1);

  auto emojiSpan1 = std::make_shared<TextSpan>();
  auto emojiBlob1 = TextBlob::MakeFrom("🎨🌈", emojiFont);
  emojiSpan1->setTextBlob(emojiBlob1);
  emojiSpan1->setPosition({85, 0});

  auto textSpan1b = std::make_shared<TextSpan>();
  auto blob1b = TextBlob::MakeFrom(" World", textFont);
  textSpan1b->setTextBlob(blob1b);
  textSpan1b->setPosition({165, 0});

  auto fill1 = MakeFillStyle(Color::Black());
  group1->setElements({textSpan1, emojiSpan1, textSpan1b, fill1});

  // Group 2: Emoji with stroke (emoji won't show stroke, but text will)
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({50, 150});

  auto textSpan2 = std::make_shared<TextSpan>();
  auto blob2 = TextBlob::MakeFrom("Stroke: ", textFont);
  textSpan2->setTextBlob(blob2);

  auto emojiSpan2 = std::make_shared<TextSpan>();
  auto emojiBlob2 = TextBlob::MakeFrom("🚀🎭", emojiFont);
  emojiSpan2->setTextBlob(emojiBlob2);
  emojiSpan2->setPosition({114, 0});

  auto stroke2 = MakeStrokeStyle(Color::Blue(), 1.0f);
  auto fill2 = MakeFillStyle(Color::Red());
  group2->setElements({textSpan2, emojiSpan2, fill2, stroke2});

  // Group 3: Emoji with TrimPath (emoji will be lost, only text path remains)
  auto group3 = std::make_shared<VectorGroup>();
  group3->setPosition({50, 200});

  auto textSpan3 = std::make_shared<TextSpan>();
  auto blob3 = TextBlob::MakeFrom("Trim: ", textFont);
  textSpan3->setTextBlob(blob3);

  auto emojiSpan3 = std::make_shared<TextSpan>();
  auto emojiBlob3 = TextBlob::MakeFrom("🎪🎉", emojiFont);
  emojiSpan3->setTextBlob(emojiBlob3);
  emojiSpan3->setPosition({86, 0});

  auto trim3 = std::make_shared<TrimPath>();
  trim3->setStart(0.0f);
  trim3->setEnd(0.6f);

  auto stroke3 = MakeStrokeStyle(Color::Green(), 2.0f);
  group3->setElements({textSpan3, emojiSpan3, trim3, stroke3});

  // Group 4: Pure emoji row
  auto group4 = std::make_shared<VectorGroup>();
  group4->setPosition({50, 260});

  auto emojiSpan4 = std::make_shared<TextSpan>();
  auto emojiBlob4 = TextBlob::MakeFrom("😀🤩🥳🎊🎁", emojiFont);
  emojiSpan4->setTextBlob(emojiBlob4);

  auto fill4 = MakeFillStyle(Color::Black());
  group4->setElements({emojiSpan4, fill4});

  vectorLayer->setContents({group1, group2, group3, group4});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/TextSpanEmoji"));
}

/**
 * Test comprehensive rich text demonstrating various text styles in a coherent paragraph:
 * gradient title, bold/italic, subscript/superscript, strikethrough, underline, inline image.
 */
TGFX_TEST(VectorLayerTest, RichText) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto surface = Surface::Make(context, 842, 318);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  auto typeface = GetTestTypeface();
  auto emojiTypeface = GetEmojiTypeface();
  if (typeface == nullptr) {
    return;
  }

  Font titleFont(typeface, 64);
  titleFont.setFauxBold(true);
  Font normalFont(typeface, 44);
  Font subscriptFont(typeface, 28);
  Font boldFont(typeface, 44);
  boldFont.setFauxBold(true);
  Font italicFont(typeface, 44);
  italicFont.setFauxItalic(true);

  auto inlineImage = MakeImage("resources/assets/tgfx.png");
  ASSERT_TRUE(inlineImage != nullptr);

  // === Row 1: [image] "TGFX Rich Text Demo" ===
  auto imageRect = std::make_shared<Rectangle>();
  imageRect->setCenter({82, 82});
  imageRect->setSize({64, 64});

  auto imageFill = std::make_shared<FillStyle>();
  SamplingOptions nearestSampling(FilterMode::Nearest, MipmapMode::None);
  auto imagePattern =
      ImagePattern::Make(inlineImage, TileMode::Clamp, TileMode::Clamp, nearestSampling);
  Matrix imageMatrix = Matrix::MakeScale(0.125f);
  imageMatrix.postTranslate(50, 50);
  imagePattern->setMatrix(imageMatrix);
  imageFill->setColorSource(imagePattern);

  auto imageGroup = std::make_shared<VectorGroup>();
  imageGroup->setElements({imageRect, imageFill});

  auto row1 = std::make_shared<VectorGroup>();
  row1->setPosition({127, 107});

  auto tgfxSpan = std::make_shared<TextSpan>();
  tgfxSpan->setTextBlob(TextBlob::MakeFrom("TGFX", titleFont));

  auto titleGradient = Gradient::MakeLinear(
      {0, 0}, {155, 0}, {Color::FromRGBA(255, 0, 0, 255), Color::FromRGBA(0, 0, 255, 255)});
  auto tgfxFill = std::make_shared<FillStyle>();
  tgfxFill->setColorSource(titleGradient);
  auto tgfxGroup = std::make_shared<VectorGroup>();
  tgfxGroup->setElements({tgfxSpan, tgfxFill});

  auto demoSpan = std::make_shared<TextSpan>();
  demoSpan->setTextBlob(TextBlob::MakeFrom(" Rich Text Demo", titleFont));
  demoSpan->setPosition({155, 0});
  auto blackFill = MakeFillStyle(Color::Black());
  auto demoGroup = std::make_shared<VectorGroup>();
  demoGroup->setElements({demoSpan, blackFill});

  row1->setElements({tgfxGroup, demoGroup});

  // === Row 2: "Supports bold italic 描边 and E=mc²" ===
  auto row2 = std::make_shared<VectorGroup>();
  row2->setPosition({50, 183});

  // "Supports " - black
  auto supportsSpan = std::make_shared<TextSpan>();
  supportsSpan->setTextBlob(TextBlob::MakeFrom("Supports ", normalFont));

  // "bold" - blue
  auto boldSpan = std::make_shared<TextSpan>();
  boldSpan->setTextBlob(TextBlob::MakeFrom("bold", boldFont));
  boldSpan->setPosition({199, 0});

  auto blueFill = MakeFillStyle(Color::Blue());
  auto boldGroup = std::make_shared<VectorGroup>();
  boldGroup->setElements({boldSpan, blueFill});

  // "italic" - red
  auto italicSpan = std::make_shared<TextSpan>();
  italicSpan->setTextBlob(TextBlob::MakeFrom("italic", italicFont));
  italicSpan->setPosition({303, 0});

  auto redFill = MakeFillStyle(Color::Red());
  auto italicGroup = std::make_shared<VectorGroup>();
  italicGroup->setElements({italicSpan, redFill});

  // "描边" - black fill + green stroke
  auto strokeSpan = std::make_shared<TextSpan>();
  strokeSpan->setTextBlob(TextBlob::MakeFrom("描边", normalFont));
  strokeSpan->setPosition({414, 0});

  auto greenStroke = MakeStrokeStyle(Color::Green(), 2.0f);
  auto strokeGroup = std::make_shared<VectorGroup>();
  strokeGroup->setElements({strokeSpan, blackFill, greenStroke});

  // " and E=mc" - black
  auto andSpan = std::make_shared<TextSpan>();
  andSpan->setTextBlob(TextBlob::MakeFrom(" and E=mc", normalFont));
  andSpan->setPosition({502, 0});

  // "2" superscript - black
  auto superscriptSpan = std::make_shared<TextSpan>();
  superscriptSpan->setTextBlob(TextBlob::MakeFrom("2", subscriptFont));
  superscriptSpan->setPosition({714, -16});

  row2->setElements(
      {supportsSpan, andSpan, superscriptSpan, blackFill, boldGroup, italicGroup, strokeGroup});

  // === Row 3: "Visit tgfx.org for more information ℹ️" ===
  auto row3 = std::make_shared<VectorGroup>();
  row3->setPosition({50, 250});

  // "Visit " - black
  auto visitSpan = std::make_shared<TextSpan>();
  visitSpan->setTextBlob(TextBlob::MakeFrom("Visit ", normalFont));

  // "tgfx.org" - blue link with underline
  auto linkSpan = std::make_shared<TextSpan>();
  linkSpan->setTextBlob(TextBlob::MakeFrom("tgfx.org", normalFont));
  linkSpan->setPosition({97, 0});

  auto underline = std::make_shared<Rectangle>();
  underline->setCenter({177, 16});
  underline->setSize({159, 3});

  auto linkGroup = std::make_shared<VectorGroup>();
  linkGroup->setElements({linkSpan, underline, blueFill});

  // " for more information " - black
  auto infoSpan = std::make_shared<TextSpan>();
  infoSpan->setTextBlob(TextBlob::MakeFrom(" for more information ", normalFont));
  infoSpan->setPosition({256, 0});

  // emoji
  std::shared_ptr<TextSpan> emojiSpan = nullptr;
  if (emojiTypeface != nullptr) {
    Font emojiFont(emojiTypeface, 32);
    emojiSpan = std::make_shared<TextSpan>();
    emojiSpan->setTextBlob(TextBlob::MakeFrom("\u2139", emojiFont));
    emojiSpan->setPosition({702, -3});
  }

  if (emojiSpan) {
    row3->setElements({visitSpan, infoSpan, emojiSpan, blackFill, linkGroup});
  } else {
    row3->setElements({visitSpan, infoSpan, blackFill, linkGroup});
  }

  vectorLayer->setContents({imageGroup, row1, row2, row3});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/RichText"));
}

}  // namespace tgfx

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
#include "tgfx/layers/VectorLayer.h"
#include "tgfx/layers/vectors/Ellipse.h"
#include "tgfx/layers/vectors/FillStyle.h"
#include "tgfx/layers/vectors/Gradient.h"
#include "tgfx/layers/vectors/ImagePattern.h"
#include "tgfx/layers/vectors/MergePath.h"
#include "tgfx/layers/vectors/Polystar.h"
#include "tgfx/layers/vectors/Rectangle.h"
#include "tgfx/layers/vectors/Repeater.h"
#include "tgfx/layers/vectors/RoundCorner.h"
#include "tgfx/layers/vectors/ShapePath.h"
#include "tgfx/layers/vectors/SolidColor.h"
#include "tgfx/layers/vectors/StrokeStyle.h"
#include "tgfx/layers/vectors/TextModifier.h"
#include "tgfx/layers/vectors/TextPath.h"
#include "tgfx/layers/vectors/TextSelector.h"
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

// ==================== ColorSource Tests ====================

/**
 * Test SolidColor: creation, color changes, and setColor.
 */
TGFX_TEST(VectorLayerTest, SolidColor) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 480, 200);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Test default color (black)
  auto solidDefault = SolidColor::Make();
  EXPECT_EQ(solidDefault->color(), Color::Black());

  // Test specific color
  auto solidRed = SolidColor::Make(Color::Red());
  EXPECT_EQ(solidRed->color(), Color::Red());

  // Test setColor
  solidRed->setColor(Color::Green());
  EXPECT_EQ(solidRed->color(), Color::Green());
  solidRed->setColor(Color::Red());

  // Group 1: Default SolidColor (black)
  auto group1 = std::make_shared<VectorGroup>();
  auto rect1 = std::make_shared<Rectangle>();
  rect1->setCenter({100, 100});
  rect1->setSize({100, 100});
  auto fill1 = std::make_shared<FillStyle>();
  fill1->setColorSource(solidDefault);
  group1->setElements({rect1, fill1});

  // Group 2: SolidColor with specific color
  auto group2 = std::make_shared<VectorGroup>();
  auto rect2 = std::make_shared<Rectangle>();
  rect2->setCenter({240, 100});
  rect2->setSize({100, 100});
  auto fill2 = std::make_shared<FillStyle>();
  fill2->setColorSource(solidRed);
  group2->setElements({rect2, fill2});

  // Group 3: SolidColor with alpha
  auto group3 = std::make_shared<VectorGroup>();
  auto rect3 = std::make_shared<Rectangle>();
  rect3->setCenter({380, 100});
  rect3->setSize({100, 100});
  auto fill3 = std::make_shared<FillStyle>();
  fill3->setColorSource(SolidColor::Make(Color::FromRGBA(0, 0, 255, 128)));
  group3->setElements({rect3, fill3});

  vectorLayer->setContents({group1, group2, group3});
  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/SolidColor"));
}

/**
 * Test all gradient types: Linear, Radial, Conic, Diamond with property setters.
 */
TGFX_TEST(VectorLayerTest, Gradient) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 670, 220);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  std::vector<Color> colors = {Color::Red(), Color::FromRGBA(255, 255, 0, 255), Color::Green(),
                               Color::Blue()};

  // Linear gradient
  auto group1 = std::make_shared<VectorGroup>();
  auto rect1 = std::make_shared<Rectangle>();
  rect1->setCenter({110, 110});
  rect1->setSize({120, 120});
  auto fill1 = std::make_shared<FillStyle>();
  auto linear = Gradient::MakeLinear({50, 110}, {170, 110}, colors);
  EXPECT_EQ(linear->type(), GradientType::Linear);
  EXPECT_EQ(linear->startPoint(), Point::Make(50, 110));
  EXPECT_EQ(linear->endPoint(), Point::Make(170, 110));
  linear->setStartPoint({50, 110});
  linear->setEndPoint({170, 110});
  fill1->setColorSource(linear);
  group1->setElements({rect1, fill1});

  // Radial gradient
  auto group2 = std::make_shared<VectorGroup>();
  auto rect2 = std::make_shared<Rectangle>();
  rect2->setCenter({260, 110});
  rect2->setSize({120, 120});
  auto fill2 = std::make_shared<FillStyle>();
  auto radial = Gradient::MakeRadial({260, 110}, 85, colors);
  EXPECT_EQ(radial->type(), GradientType::Radial);
  EXPECT_EQ(radial->center(), Point::Make(260, 110));
  EXPECT_EQ(radial->radius(), 85.0f);
  radial->setCenter({260, 110});
  radial->setRadius(85.0f);
  fill2->setColorSource(radial);
  group2->setElements({rect2, fill2});

  // Conic gradient
  auto group3 = std::make_shared<VectorGroup>();
  auto rect3 = std::make_shared<Rectangle>();
  rect3->setCenter({410, 110});
  rect3->setSize({120, 120});
  auto fill3 = std::make_shared<FillStyle>();
  auto conic = Gradient::MakeConic({410, 110}, 0, 360, colors);
  EXPECT_EQ(conic->type(), GradientType::Conic);
  EXPECT_EQ(conic->center(), Point::Make(410, 110));
  EXPECT_EQ(conic->startAngle(), 0.0f);
  EXPECT_EQ(conic->endAngle(), 360.0f);
  conic->setCenter({410, 110});
  conic->setStartAngle(0.0f);
  conic->setEndAngle(360.0f);
  fill3->setColorSource(conic);
  group3->setElements({rect3, fill3});

  // Diamond gradient
  auto group4 = std::make_shared<VectorGroup>();
  auto rect4 = std::make_shared<Rectangle>();
  rect4->setCenter({560, 110});
  rect4->setSize({120, 120});
  auto fill4 = std::make_shared<FillStyle>();
  auto diamond = Gradient::MakeDiamond({560, 110}, 85, colors);
  EXPECT_EQ(diamond->type(), GradientType::Diamond);
  EXPECT_EQ(diamond->center(), Point::Make(560, 110));
  EXPECT_EQ(diamond->halfDiagonal(), 85.0f);
  diamond->setCenter({560, 110});
  diamond->setHalfDiagonal(85.0f);
  fill4->setColorSource(diamond);
  group4->setElements({rect4, fill4});

  // Test colors and positions
  EXPECT_EQ(linear->colors().size(), 4u);
  linear->setColors(colors);
  linear->setPositions({0.0f, 0.33f, 0.66f, 1.0f});
  EXPECT_EQ(linear->positions().size(), 4u);

  vectorLayer->setContents({group1, group2, group3, group4});
  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/Gradient"));
}

/**
 * Test Gradient edge cases: empty colors, single color, matrix transformation.
 */
TGFX_TEST(VectorLayerTest, GradientEdgeCases) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 481, 202);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Group 1: Empty colors (invisible fill, with stroke to show rect bounds)
  auto group1 = std::make_shared<VectorGroup>();
  auto rect1 = std::make_shared<Rectangle>();
  rect1->setCenter({101, 101});
  rect1->setSize({100, 100});
  auto fill1 = std::make_shared<FillStyle>();
  auto emptyGradient = Gradient::MakeLinear({51, 101}, {151, 101}, {});
  EXPECT_TRUE(emptyGradient->colors().empty());
  fill1->setColorSource(emptyGradient);
  auto stroke1 = MakeStrokeStyle(Color::FromRGBA(128, 128, 128, 255), 2.0f);
  group1->setElements({rect1, fill1, stroke1});

  // Group 2: Single color
  auto group2 = std::make_shared<VectorGroup>();
  auto rect2 = std::make_shared<Rectangle>();
  rect2->setCenter({241, 101});
  rect2->setSize({100, 100});
  auto fill2 = std::make_shared<FillStyle>();
  auto singleGradient = Gradient::MakeLinear({191, 101}, {291, 101}, {Color::Red()});
  EXPECT_EQ(singleGradient->colors().size(), 1u);
  fill2->setColorSource(singleGradient);
  group2->setElements({rect2, fill2});

  // Group 3: With rotation matrix (gradient rotated 45 degrees around center)
  auto group3 = std::make_shared<VectorGroup>();
  auto rect3 = std::make_shared<Rectangle>();
  rect3->setCenter({381, 101});
  rect3->setSize({100, 100});
  auto fill3 = std::make_shared<FillStyle>();
  // Create a gradient from top-left to bottom-right of the rect
  auto matrixGradient = Gradient::MakeLinear({331, 51}, {431, 151}, {Color::Red(), Color::Blue()});
  fill3->setColorSource(matrixGradient);
  group3->setElements({rect3, fill3});

  vectorLayer->setContents({group1, group2, group3});
  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/GradientEdgeCases"));
}

/**
 * Test ImagePattern: tile modes, null image, and matrix transformation.
 */
TGFX_TEST(VectorLayerTest, ImagePattern) {
  // Test null image returns nullptr
  auto nullPattern = ImagePattern::Make(nullptr);
  EXPECT_EQ(nullPattern, nullptr);

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 480, 200);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto image = MakeImage("resources/assets/bridge.jpg");
  ASSERT_TRUE(image != nullptr);
  auto image2 = MakeImage("resources/assets/tgfx.png");
  ASSERT_TRUE(image2 != nullptr);

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Group 1: Clamp tile mode - rect larger than image to show edge clamping
  auto group1 = std::make_shared<VectorGroup>();
  auto rect1 = std::make_shared<Rectangle>();
  rect1->setCenter({100, 100});
  rect1->setSize({100, 100});  // Rect is 100x100, larger than 50x50 image
  auto fill1 = std::make_shared<FillStyle>();
  auto pattern1 = ImagePattern::Make(image, TileMode::Clamp, TileMode::Clamp);
  ASSERT_TRUE(pattern1 != nullptr);
  EXPECT_EQ(pattern1->image(), image);
  EXPECT_EQ(pattern1->tileModeX(), TileMode::Clamp);
  EXPECT_EQ(pattern1->tileModeY(), TileMode::Clamp);
  // Scale image to 50x50 and position at rect center (75,75)
  // Rect bounds: (50,50) to (150,150), image at (75,75) to (125,125)
  // This leaves margins on all sides for clamping effect
  auto scale1 = 50.0f / static_cast<float>(image->width());
  Matrix matrix1 = Matrix::MakeScale(scale1);
  matrix1.postTranslate(75, 75);
  pattern1->setMatrix(matrix1);
  fill1->setColorSource(pattern1);
  group1->setElements({rect1, fill1});

  // Group 2: Repeat tile mode - small scale to show tiling
  auto group2 = std::make_shared<VectorGroup>();
  auto rect2 = std::make_shared<Rectangle>();
  rect2->setCenter({240, 100});
  rect2->setSize({100, 100});
  auto fill2 = std::make_shared<FillStyle>();
  auto pattern2 = ImagePattern::Make(image2, TileMode::Repeat, TileMode::Repeat);
  // Scale image to ~25x25 pixels to show tiling, position at rect's top-left (190, 50)
  Matrix matrix2 = Matrix::MakeScale(0.05f);
  matrix2.postTranslate(190, 50);
  pattern2->setMatrix(matrix2);
  fill2->setColorSource(pattern2);
  group2->setElements({rect2, fill2});

  // Group 3: Mirror tile mode with rotation
  auto group3 = std::make_shared<VectorGroup>();
  auto rect3 = std::make_shared<Rectangle>();
  rect3->setCenter({380, 100});
  rect3->setSize({100, 100});
  auto fill3 = std::make_shared<FillStyle>();
  auto pattern3 = ImagePattern::Make(image2, TileMode::Mirror, TileMode::Mirror);
  // Scale image to ~25x25 pixels, rotate, and position at rect's top-left (330, 50)
  Matrix matrix3 = Matrix::MakeScale(0.05f);
  matrix3.postRotate(30.0f, 12.8f, 12.8f);  // Rotate around center of scaled image
  matrix3.postTranslate(330, 50);
  pattern3->setMatrix(matrix3);
  EXPECT_EQ(pattern3->matrix(), matrix3);
  fill3->setColorSource(pattern3);
  group3->setElements({rect3, fill3});

  vectorLayer->setContents({group1, group2, group3});
  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/ImagePattern"));
}

/**
 * Test ColorSource with stroke style and shared ColorSource.
 */
TGFX_TEST(VectorLayerTest, ColorSourceAdvanced) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 484, 208);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Group 1: SolidColor stroke
  auto group1 = std::make_shared<VectorGroup>();
  auto rect1 = std::make_shared<Rectangle>();
  rect1->setCenter({104, 104});
  rect1->setSize({100, 100});
  auto stroke1 = std::make_shared<StrokeStyle>();
  stroke1->setColorSource(SolidColor::Make(Color::Red()));
  stroke1->setStrokeWidth(8.0f);
  group1->setElements({rect1, stroke1});

  // Group 2: Gradient stroke
  auto group2 = std::make_shared<VectorGroup>();
  auto rect2 = std::make_shared<Rectangle>();
  rect2->setCenter({244, 104});
  rect2->setSize({100, 100});
  auto stroke2 = std::make_shared<StrokeStyle>();
  stroke2->setColorSource(
      Gradient::MakeLinear({194, 54}, {294, 154}, {Color::Blue(), Color::Green()}));
  stroke2->setStrokeWidth(8.0f);
  group2->setElements({rect2, stroke2});

  // Group 3: Shared ColorSource (two shapes share the same gradient)
  auto sharedGradient = Gradient::MakeRadial(
      {384, 104}, 70, {Color::FromRGBA(255, 255, 0, 255), Color::FromRGBA(255, 0, 255, 255)});
  auto group3 = std::make_shared<VectorGroup>();
  auto rect3 = std::make_shared<Rectangle>();
  rect3->setCenter({384, 104});
  rect3->setSize({100, 100});
  auto fill3 = std::make_shared<FillStyle>();
  fill3->setColorSource(sharedGradient);
  group3->setElements({rect3, fill3});

  vectorLayer->setContents({group1, group2, group3});
  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  // Modify shared gradient and re-render
  sharedGradient->setColors({Color::Green(), Color::Blue()});
  canvas->clear(Color::White());
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/ColorSourceAdvanced"));
}

/**
 * Test ImagePattern fill on bold text.
 */
TGFX_TEST(VectorLayerTest, ImagePatternText) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 334, 177);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto typeface = GetTestTypeface();
  if (typeface == nullptr) {
    return;
  }

  auto image = MakeImage("resources/assets/bridge.jpg");
  ASSERT_TRUE(image != nullptr);

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  // Create bold text filled with image pattern
  Font font(typeface, 96.0f);
  font.setFauxBold(true);
  auto textSpan = std::make_shared<TextSpan>();
  textSpan->setTextBlob(TextBlob::MakeFrom("TGFX", font));
  textSpan->setPosition({50, 126});

  auto fill = std::make_shared<FillStyle>();
  auto pattern = ImagePattern::Make(image, TileMode::Clamp, TileMode::Clamp);
  Matrix matrix = Matrix::MakeScale(0.5f);
  matrix.postTranslate(-180, -80);
  pattern->setMatrix(matrix);
  fill->setColorSource(pattern);

  vectorLayer->setContents({textSpan, fill});
  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/ImagePatternText"));
}

/**
 * Test TextPath with various configurations:
 * Column 1 (left): Basic TextPath options
 * - Alignment: Start, Center, End
 * - perpendicularToPath: true and false
 * - reversed: true
 * - firstMargin/lastMargin with forceAlignment
 *
 * Column 2 (right): TextPath matrix override behavior
 * - Two consecutive TextPaths (second overrides first)
 * - Group transform before TextPath (TextPath overrides group transform)
 * - Group transform after TextPath (group transform applies after TextPath)
 */
TGFX_TEST(VectorLayerTest, TextPath) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1105, 961);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  auto typeface = GetTestTypeface();
  if (typeface == nullptr) {
    return;
  }
  Font font(typeface, 24);

  // Create a curved path
  Path curvePath = {};
  curvePath.moveTo(40, 80);
  curvePath.cubicTo(140, -40, 340, 200, 440, 80);

  // ==================== Column 1: Basic TextPath options ====================

  // Group 1: Start alignment, perpendicular to path
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({58, 63});

  auto textSpan1 = std::make_shared<TextSpan>();
  textSpan1->setTextBlob(TextBlob::MakeFrom("Start Aligned", font));

  auto textPath1 = std::make_shared<TextPath>();
  textPath1->setPath(curvePath);
  textPath1->setAlign(TextPathAlign::Start);
  textPath1->setPerpendicularToPath(true);

  auto fill1 = MakeFillStyle(Color::Blue());
  group1->setElements({textSpan1, textPath1, fill1});

  // Group 2: Center alignment, perpendicular to path
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({58, 163});

  auto textSpan2 = std::make_shared<TextSpan>();
  textSpan2->setTextBlob(TextBlob::MakeFrom("Center Aligned", font));

  auto textPath2 = std::make_shared<TextPath>();
  textPath2->setPath(curvePath);
  textPath2->setAlign(TextPathAlign::Center);
  textPath2->setPerpendicularToPath(true);

  auto fill2 = MakeFillStyle(Color::Red());
  group2->setElements({textSpan2, textPath2, fill2});

  // Group 3: End alignment, perpendicular to path
  auto group3 = std::make_shared<VectorGroup>();
  group3->setPosition({58, 263});

  auto textSpan3 = std::make_shared<TextSpan>();
  textSpan3->setTextBlob(TextBlob::MakeFrom("End Aligned", font));

  auto textPath3 = std::make_shared<TextPath>();
  textPath3->setPath(curvePath);
  textPath3->setAlign(TextPathAlign::End);
  textPath3->setPerpendicularToPath(true);

  auto fill3 = MakeFillStyle(Color::Green());
  group3->setElements({textSpan3, textPath3, fill3});

  // Group 4: Not perpendicular to path (text stays upright)
  auto group4 = std::make_shared<VectorGroup>();
  group4->setPosition({58, 363});

  auto textSpan4 = std::make_shared<TextSpan>();
  textSpan4->setTextBlob(TextBlob::MakeFrom("Not Perpendicular", font));

  auto textPath4 = std::make_shared<TextPath>();
  textPath4->setPath(curvePath);
  textPath4->setAlign(TextPathAlign::Center);
  textPath4->setPerpendicularToPath(false);

  auto fill4 = MakeFillStyle(Color{1.0f, 0.5f, 0.0f, 1.0f});  // Orange
  group4->setElements({textSpan4, textPath4, fill4});

  // Group 5: Reversed path direction
  auto group5 = std::make_shared<VectorGroup>();
  group5->setPosition({58, 463});

  auto textSpan5 = std::make_shared<TextSpan>();
  textSpan5->setTextBlob(TextBlob::MakeFrom("Reversed Path", font));

  auto textPath5 = std::make_shared<TextPath>();
  textPath5->setPath(curvePath);
  textPath5->setAlign(TextPathAlign::Start);
  textPath5->setPerpendicularToPath(true);
  textPath5->setReversed(true);

  auto fill5 = MakeFillStyle(Color{0.5f, 0.0f, 0.5f, 1.0f});  // Purple
  group5->setElements({textSpan5, textPath5, fill5});

  // Group 6: With margins and forceAlignment
  auto group6 = std::make_shared<VectorGroup>();
  group6->setPosition({58, 563});

  auto textSpan6 = std::make_shared<TextSpan>();
  textSpan6->setTextBlob(TextBlob::MakeFrom("Force Alignment", font));

  auto textPath6 = std::make_shared<TextPath>();
  textPath6->setPath(curvePath);
  textPath6->setFirstMargin(50);
  textPath6->setLastMargin(-50);  // Negative value shrinks from path end
  textPath6->setAlign(TextPathAlign::Start);
  textPath6->setPerpendicularToPath(true);
  textPath6->setForceAlignment(true);

  auto fill6 = MakeFillStyle(Color{0.0f, 0.5f, 0.5f, 1.0f});  // Teal
  group6->setElements({textSpan6, textPath6, fill6});

  // ==================== Column 2: TextPath advanced tests ====================

  // Create a larger curved path for testing TextPath override
  Path largerCurvePath = {};
  largerCurvePath.moveTo(40, 80);
  largerCurvePath.cubicTo(140, -120, 340, 280, 440, 80);  // More extreme curve

  // Group 7: Two consecutive TextPaths - second should override first
  auto group7 = std::make_shared<VectorGroup>();
  group7->setPosition({548, 63});

  auto textSpan7 = std::make_shared<TextSpan>();
  textSpan7->setTextBlob(TextBlob::MakeFrom("Second Override", font));

  auto textPathFirst = std::make_shared<TextPath>();
  textPathFirst->setPath(curvePath);
  textPathFirst->setAlign(TextPathAlign::Start);
  textPathFirst->setPerpendicularToPath(true);

  auto textPathSecond = std::make_shared<TextPath>();
  textPathSecond->setPath(largerCurvePath);
  textPathSecond->setAlign(TextPathAlign::Start);
  textPathSecond->setPerpendicularToPath(true);

  auto fill7 = MakeFillStyle(Color::Blue());
  group7->setElements({textSpan7, textPathFirst, textPathSecond, fill7});

  // Group 8: Inner group transform overridden by TextPath
  auto group8 = std::make_shared<VectorGroup>();
  group8->setPosition({548, 163});

  auto innerGroup8 = std::make_shared<VectorGroup>();
  innerGroup8->setPosition({20, 60});
  innerGroup8->setScale({1.5f, 0.8f});

  auto textSpan8 = std::make_shared<TextSpan>();
  textSpan8->setTextBlob(TextBlob::MakeFrom("Group Override", font));

  innerGroup8->setElements({textSpan8});

  auto textPath8 = std::make_shared<TextPath>();
  textPath8->setPath(curvePath);
  textPath8->setAlign(TextPathAlign::Start);
  textPath8->setPerpendicularToPath(true);

  auto fill8 = MakeFillStyle(Color::Red());
  group8->setElements({innerGroup8, textPath8, fill8});

  // Group 9: Path extension - text extends beyond path boundaries
  // Uses short path with long text to test path extension at both ends
  Path shortPath = {};
  shortPath.moveTo(180, 80);
  shortPath.cubicTo(220, 20, 280, 140, 320, 80);

  auto group9 = std::make_shared<VectorGroup>();
  group9->setPosition({548, 263});

  auto textSpan9 = std::make_shared<TextSpan>();
  textSpan9->setTextBlob(TextBlob::MakeFrom("Path Extension Test", font));

  auto textPath9 = std::make_shared<TextPath>();
  textPath9->setPath(shortPath);
  textPath9->setAlign(TextPathAlign::Center);
  textPath9->setPerpendicularToPath(true);

  auto fill9 = MakeFillStyle(Color::Green());
  group9->setElements({textSpan9, textPath9, fill9});

  // Group 10: Closed path with text wrapping around
  Path closedPath = {};
  closedPath.moveTo(240, 40);
  closedPath.cubicTo(340, 40, 340, 120, 240, 120);
  closedPath.cubicTo(140, 120, 140, 40, 240, 40);
  closedPath.close();

  auto group10 = std::make_shared<VectorGroup>();
  group10->setPosition({548, 363});

  auto textSpan10 = std::make_shared<TextSpan>();
  textSpan10->setTextBlob(TextBlob::MakeFrom("Closed Path Text Wrap", font));

  auto textPath10 = std::make_shared<TextPath>();
  textPath10->setPath(closedPath);
  textPath10->setAlign(TextPathAlign::Start);
  textPath10->setFirstMargin(-80.0f);  // Negative margin to wrap around the closed path
  textPath10->setPerpendicularToPath(true);

  auto fill10 = MakeFillStyle(Color{0.5f, 0.0f, 0.5f, 1.0f});  // Purple
  group10->setElements({textSpan10, textPath10, fill10});

  // Group 11: Multiple TextSpans with nested transforms
  auto group11 = std::make_shared<VectorGroup>();
  group11->setPosition({448, 513});

  auto middleGroup11 = std::make_shared<VectorGroup>();
  middleGroup11->setScale({1.3f, 1.3f});

  auto innerGroup11 = std::make_shared<VectorGroup>();
  innerGroup11->setPosition({0, 8});
  innerGroup11->setSkew(-20.0f);

  auto textSpan11a = std::make_shared<TextSpan>();
  textSpan11a->setTextBlob(TextBlob::MakeFrom("Multi ", font));

  auto textSpan11b = std::make_shared<TextSpan>();
  textSpan11b->setTextBlob(TextBlob::MakeFrom("Spans", font));

  auto textPath11 = std::make_shared<TextPath>();
  textPath11->setPath(curvePath);
  textPath11->setAlign(TextPathAlign::Center);
  textPath11->setPerpendicularToPath(true);

  auto fill11 = MakeFillStyle(Color{0.0f, 0.5f, 0.5f, 1.0f});  // Teal
  innerGroup11->setElements({textSpan11a, textSpan11b, textPath11, fill11});
  middleGroup11->setElements({innerGroup11});

  auto rotationGroup11 = std::make_shared<VectorGroup>();
  rotationGroup11->setAnchorPoint({350, 104});
  rotationGroup11->setPosition({350, 104});
  rotationGroup11->setRotation(15.0f);

  rotationGroup11->setElements({middleGroup11});
  group11->setElements({rotationGroup11});

  // ==================== Row 7: Edge cases for margin and alignment ====================

  // Group 12: ForceAlignment with Center align - should ignore alignment and start from firstMargin
  auto group12 = std::make_shared<VectorGroup>();
  group12->setPosition({58, 663});

  auto textSpan12 = std::make_shared<TextSpan>();
  textSpan12->setTextBlob(TextBlob::MakeFrom("Force+Center", font));

  auto textPath12 = std::make_shared<TextPath>();
  textPath12->setPath(curvePath);
  textPath12->setFirstMargin(30);
  textPath12->setLastMargin(-30);
  textPath12->setAlign(TextPathAlign::Center);  // Should be ignored when forceAlignment is true
  textPath12->setPerpendicularToPath(true);
  textPath12->setForceAlignment(true);

  auto fill12 = MakeFillStyle(Color{0.8f, 0.2f, 0.2f, 1.0f});  // Dark red
  group12->setElements({textSpan12, textPath12, fill12});

  // Group 13: Negative spacing - firstMargin exceeds pathLength + lastMargin
  auto group13 = std::make_shared<VectorGroup>();
  group13->setPosition({58, 763});

  auto textSpan13 = std::make_shared<TextSpan>();
  textSpan13->setTextBlob(TextBlob::MakeFrom("Negative Spacing", font));

  auto textPath13 = std::make_shared<TextPath>();
  textPath13->setPath(curvePath);
  textPath13->setFirstMargin(400);  // Exceeds path end
  textPath13->setLastMargin(-350);  // Path length ~380, so 400 > 380 + (-350) = 30
  textPath13->setAlign(TextPathAlign::Start);
  textPath13->setPerpendicularToPath(true);
  textPath13->setForceAlignment(true);

  auto fill13 = MakeFillStyle(Color{0.2f, 0.2f, 0.8f, 1.0f});  // Dark blue
  group13->setElements({textSpan13, textPath13, fill13});

  // Group 14: Center align with both margins
  auto group14 = std::make_shared<VectorGroup>();
  group14->setPosition({548, 663});

  auto textSpan14 = std::make_shared<TextSpan>();
  textSpan14->setTextBlob(TextBlob::MakeFrom("Center+Margins", font));

  auto textPath14 = std::make_shared<TextPath>();
  textPath14->setPath(curvePath);
  textPath14->setFirstMargin(40);
  textPath14->setLastMargin(-60);
  textPath14->setAlign(TextPathAlign::Center);
  textPath14->setPerpendicularToPath(true);

  auto fill14 = MakeFillStyle(Color{0.2f, 0.6f, 0.2f, 1.0f});  // Dark green
  group14->setElements({textSpan14, textPath14, fill14});

  // Group 15: End align with lastMargin
  auto group15 = std::make_shared<VectorGroup>();
  group15->setPosition({548, 763});

  auto textSpan15 = std::make_shared<TextSpan>();
  textSpan15->setTextBlob(TextBlob::MakeFrom("End+LastMargin", font));

  auto textPath15 = std::make_shared<TextPath>();
  textPath15->setPath(curvePath);
  textPath15->setLastMargin(-80);
  textPath15->setAlign(TextPathAlign::End);
  textPath15->setPerpendicularToPath(true);

  auto fill15 = MakeFillStyle(Color{0.6f, 0.4f, 0.0f, 1.0f});  // Brown
  group15->setElements({textSpan15, textPath15, fill15});

  vectorLayer->setContents({group1, group2, group3, group4, group5, group6, group7, group8, group9,
                            group10, group11, group12, group13, group14, group15});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  // Draw helper paths after rendering
  Paint pathPaint = {};
  pathPaint.setStyle(PaintStyle::Stroke);
  pathPaint.setStrokeWidth(1);
  pathPaint.setAlpha(0.3f);

  // Column 1 helper paths
  std::vector<std::pair<float, Color>> pathPositions1 = {
      {63, Color::Blue()},
      {163, Color::Red()},
      {263, Color::Green()},
      {363, Color{1.0f, 0.5f, 0.0f, 1.0f}},  // Orange
      {463, Color{0.5f, 0.0f, 0.5f, 1.0f}},  // Purple
      {563, Color{0.0f, 0.5f, 0.5f, 1.0f}},  // Teal
      {663, Color{0.8f, 0.2f, 0.2f, 1.0f}},  // Dark red (Force+Center)
      {763, Color{0.2f, 0.2f, 0.8f, 1.0f}},  // Dark blue (Negative Spacing)
  };

  for (const auto& [y, color] : pathPositions1) {
    canvas->save();
    canvas->translate(58, y);
    pathPaint.setColor(color);
    canvas->drawPath(curvePath, pathPaint);
    canvas->restore();
  }

  // Column 2 helper paths
  // Row 1 (group7): Second Override
  canvas->save();
  canvas->translate(548, 63);
  pathPaint.setColor(Color::Blue());
  canvas->drawPath(curvePath, pathPaint);
  canvas->restore();

  canvas->save();
  canvas->translate(548, 63);
  pathPaint.setColor(Color{0.8f, 0.8f, 0.8f, 1.0f});  // Gray for overridden path
  canvas->drawPath(largerCurvePath, pathPaint);
  canvas->restore();

  // Row 2 (group8): Group Transform
  canvas->save();
  canvas->translate(548, 163);
  pathPaint.setColor(Color::Red());
  canvas->drawPath(curvePath, pathPaint);
  canvas->restore();

  // Row 3 (group9): Path Extension
  canvas->save();
  canvas->translate(548, 263);
  pathPaint.setColor(Color::Green());
  canvas->drawPath(shortPath, pathPaint);
  canvas->restore();

  // Row 4 (group10): Closed Path
  canvas->save();
  canvas->translate(548, 363);
  pathPaint.setColor(Color{0.5f, 0.0f, 0.5f, 1.0f});
  canvas->drawPath(closedPath, pathPaint);
  canvas->restore();

  // Row 5 (group11): Deep Nested
  canvas->save();
  canvas->translate(548, 533);
  pathPaint.setColor(Color{0.0f, 0.5f, 0.5f, 1.0f});
  canvas->drawPath(curvePath, pathPaint);
  canvas->restore();

  // Row 7 Column 2 helper paths
  // Group 14: Center+Margins
  canvas->save();
  canvas->translate(548, 663);
  pathPaint.setColor(Color{0.2f, 0.6f, 0.2f, 1.0f});
  canvas->drawPath(curvePath, pathPaint);
  canvas->restore();

  // Group 15: End+LastMargin
  canvas->save();
  canvas->translate(548, 763);
  pathPaint.setColor(Color{0.6f, 0.4f, 0.0f, 1.0f});
  canvas->drawPath(curvePath, pathPaint);
  canvas->restore();

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/TextPath"));
}

/**
 * Test combinations of TextPath (text modifier) and TrimPath (path modifier):
 * - Row 1: TextPath then TrimPath - text is laid out along path first, then trimmed as shapes
 * - Row 2: TrimPath then TextPath - TrimPath converts text to shapes and trims them,
 *          TextPath finds no text to layout (already converted)
 */
TGFX_TEST(VectorLayerTest, TextPathWithTrimPath) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 518, 460);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  auto typeface = GetTestTypeface();
  if (typeface == nullptr) {
    return;
  }
  Font font(typeface, 32);
  font.setFauxBold(true);

  Path curvePath = {};
  curvePath.moveTo(40, 60);
  curvePath.cubicTo(140, -60, 340, 180, 440, 60);

  // Group 1: TextPath then TrimPath
  // Text is first laid out along the path (glyphs positioned on curve),
  // then TrimPath trims each glyph shape (Simultaneously mode)
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({28, 110});

  auto textSpan1 = std::make_shared<TextSpan>();
  textSpan1->setTextBlob(TextBlob::MakeFrom("TextPath+TrimPath", font));

  auto textPath1 = std::make_shared<TextPath>();
  textPath1->setPath(curvePath);
  textPath1->setAlign(TextPathAlign::Start);
  textPath1->setPerpendicularToPath(true);

  auto trim1 = std::make_shared<TrimPath>();
  trim1->setStart(0.0f);
  trim1->setEnd(0.95f);
  trim1->setTrimType(TrimPathType::Simultaneously);

  auto fill1 = MakeFillStyle(Color::Blue());
  group1->setElements({textSpan1, textPath1, trim1, fill1});

  // Group 2: TrimPath then TextPath
  // TrimPath runs first, converts text to shapes (at original position) and trims from 5% to 100%.
  // TextPath then runs but finds no text to layout (already converted to shapes by TrimPath).
  // Result: text is trimmed at original position, not laid out along path.
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({28, 230});

  auto textSpan2 = std::make_shared<TextSpan>();
  textSpan2->setTextBlob(TextBlob::MakeFrom("TrimPath+TextPath", font));
  textSpan2->setPosition({150, 40});

  auto trim2 = std::make_shared<TrimPath>();
  trim2->setStart(0.05f);
  trim2->setEnd(1.0f);
  trim2->setTrimType(TrimPathType::Simultaneously);

  auto textPath2 = std::make_shared<TextPath>();
  textPath2->setPath(curvePath);
  textPath2->setAlign(TextPathAlign::Start);
  textPath2->setPerpendicularToPath(true);

  auto fill2 = MakeFillStyle(Color::Red());
  group2->setElements({textSpan2, trim2, textPath2, fill2});

  vectorLayer->setContents({group1, group2});

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  // Draw helper paths
  Paint pathPaint = {};
  pathPaint.setStyle(PaintStyle::Stroke);
  pathPaint.setStrokeWidth(1.0f);

  canvas->save();
  canvas->translate(28, 110);
  pathPaint.setColor(Color{0.7f, 0.7f, 1.0f, 1.0f});
  canvas->drawPath(curvePath, pathPaint);
  canvas->restore();

  canvas->save();
  canvas->translate(28, 230);
  pathPaint.setColor(Color{1.0f, 0.7f, 0.7f, 1.0f});
  canvas->drawPath(curvePath, pathPaint);
  canvas->restore();

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/TextPathWithTrimPath"));
}

// ==================== TextModifier and TextSelector Tests ====================

/**
 * Test TextModifier and RangeSelector functionality in a grid layout:
 * Column 1: Transform properties (Position, Scale, Rotation, Alpha, Skew, AnchorPoint)
 * Column 2: Style overrides (FillColor, StrokeColor, StrokeWidth, Fill+Stroke, MultiSpan, MultiMod)
 * Column 3: Shape comparison (Square, RampUp, RampDown, Triangle, Round, Smooth)
 * Column 4: RangeSelector properties (EaseIn, EaseOut, EaseBoth, Unit, NegOffset, Reversed)
 * Column 5: Edge cases (Random, Empty, StartEnd, SubFirst)
 */
TGFX_TEST(VectorLayerTest, TextModifier) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 1100, 520);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  auto typeface = GetTestTypeface();
  if (typeface == nullptr) {
    return;
  }
  Font font(typeface, 28);
  Font boldFont(typeface, 28);
  boldFont.setFauxBold(true);

  std::vector<std::shared_ptr<VectorGroup>> groups = {};

  // ==================== Column 1: Transform properties ====================
  float col1X = 50;
  float rowHeight = 75;

  // Row 1: Position
  auto group1 = std::make_shared<VectorGroup>();
  group1->setPosition({col1X, 86});
  auto textSpan1 = std::make_shared<TextSpan>();
  textSpan1->setTextBlob(TextBlob::MakeFrom("Position", font));
  auto selector1 = std::make_shared<RangeSelector>();
  selector1->setShape(SelectorShape::RampUp);
  auto modifier1 = std::make_shared<TextModifier>();
  modifier1->setSelectors({selector1});
  modifier1->setPosition({0, -20});
  group1->setElements({textSpan1, modifier1, MakeFillStyle(Color::Blue())});
  groups.push_back(group1);

  // Row 2: Scale (non-uniform)
  auto group2 = std::make_shared<VectorGroup>();
  group2->setPosition({col1X, 86 + rowHeight});
  auto textSpan2 = std::make_shared<TextSpan>();
  textSpan2->setTextBlob(TextBlob::MakeFrom("Scale", font));
  auto selector2 = std::make_shared<RangeSelector>();
  selector2->setShape(SelectorShape::Triangle);
  auto modifier2 = std::make_shared<TextModifier>();
  modifier2->setSelectors({selector2});
  modifier2->setScale({2.0f, 0.5f});
  group2->setElements({textSpan2, modifier2, MakeFillStyle(Color::Red())});
  groups.push_back(group2);

  // Row 3: Rotation
  auto group3 = std::make_shared<VectorGroup>();
  group3->setPosition({col1X, 86 + rowHeight * 2});
  auto textSpan3 = std::make_shared<TextSpan>();
  textSpan3->setTextBlob(TextBlob::MakeFrom("Rotation", font));
  auto selector3 = std::make_shared<RangeSelector>();
  selector3->setShape(SelectorShape::Square);
  auto modifier3 = std::make_shared<TextModifier>();
  modifier3->setSelectors({selector3});
  modifier3->setRotation(45);
  group3->setElements({textSpan3, modifier3, MakeFillStyle(Color::Green())});
  groups.push_back(group3);

  // Row 4: Alpha (intermediate value 0.5)
  auto group4 = std::make_shared<VectorGroup>();
  group4->setPosition({col1X, 86 + rowHeight * 3});
  auto textSpan4 = std::make_shared<TextSpan>();
  textSpan4->setTextBlob(TextBlob::MakeFrom("Alpha", font));
  auto selector4 = std::make_shared<RangeSelector>();
  selector4->setShape(SelectorShape::RampDown);
  auto modifier4 = std::make_shared<TextModifier>();
  modifier4->setSelectors({selector4});
  modifier4->setAlpha(0.5f);
  group4->setElements({textSpan4, modifier4, MakeFillStyle(Color::Black())});
  groups.push_back(group4);

  // Row 5: Skew
  auto group5 = std::make_shared<VectorGroup>();
  group5->setPosition({col1X, 86 + rowHeight * 4});
  auto textSpan5 = std::make_shared<TextSpan>();
  textSpan5->setTextBlob(TextBlob::MakeFrom("Skew", font));
  auto selector5 = std::make_shared<RangeSelector>();
  selector5->setShape(SelectorShape::Triangle);
  auto modifier5 = std::make_shared<TextModifier>();
  modifier5->setSelectors({selector5});
  modifier5->setSkew(30);
  modifier5->setSkewAxis(45);
  group5->setElements({textSpan5, modifier5, MakeFillStyle(Color::FromRGBA(128, 0, 128, 255))});
  groups.push_back(group5);

  // Row 6: AnchorPoint
  auto group6 = std::make_shared<VectorGroup>();
  group6->setPosition({col1X, 86 + rowHeight * 5});
  auto textSpan6 = std::make_shared<TextSpan>();
  textSpan6->setTextBlob(TextBlob::MakeFrom("AnchorPoint", font));
  auto selector6 = std::make_shared<RangeSelector>();
  selector6->setShape(SelectorShape::RampUp);
  auto modifier6 = std::make_shared<TextModifier>();
  modifier6->setSelectors({selector6});
  modifier6->setAnchorPoint({0, 15});
  modifier6->setRotation(30);
  group6->setElements({textSpan6, modifier6, MakeFillStyle(Color::FromRGBA(255, 128, 0, 255))});
  groups.push_back(group6);

  // ==================== Column 2: Style overrides ====================
  float col2X = 244;

  // Row 1: FillColor
  auto group7 = std::make_shared<VectorGroup>();
  group7->setPosition({col2X, 86});
  auto textSpan7 = std::make_shared<TextSpan>();
  textSpan7->setTextBlob(TextBlob::MakeFrom("FillColor", boldFont));
  auto selector7 = std::make_shared<RangeSelector>();
  selector7->setShape(SelectorShape::RampDown);
  auto modifier7 = std::make_shared<TextModifier>();
  modifier7->setSelectors({selector7});
  modifier7->setFillColor(Color::Red());
  group7->setElements({textSpan7, modifier7, MakeFillStyle(Color::Blue())});
  groups.push_back(group7);

  // Row 2: StrokeColor
  auto group8 = std::make_shared<VectorGroup>();
  group8->setPosition({col2X, 86 + rowHeight});
  auto textSpan8 = std::make_shared<TextSpan>();
  textSpan8->setTextBlob(TextBlob::MakeFrom("StrokeColor", boldFont));
  auto selector8 = std::make_shared<RangeSelector>();
  selector8->setShape(SelectorShape::Triangle);
  auto modifier8 = std::make_shared<TextModifier>();
  modifier8->setSelectors({selector8});
  modifier8->setStrokeColor(Color::Red());
  group8->setElements({textSpan8, modifier8, MakeStrokeStyle(Color::Blue(), 2)});
  groups.push_back(group8);

  // Row 3: StrokeWidth
  auto group9 = std::make_shared<VectorGroup>();
  group9->setPosition({col2X, 86 + rowHeight * 2});
  auto textSpan9 = std::make_shared<TextSpan>();
  textSpan9->setTextBlob(TextBlob::MakeFrom("StrokeWidth", boldFont));
  auto selector9 = std::make_shared<RangeSelector>();
  selector9->setShape(SelectorShape::RampUp);
  auto modifier9 = std::make_shared<TextModifier>();
  modifier9->setSelectors({selector9});
  modifier9->setStrokeWidth(6);
  group9->setElements({textSpan9, modifier9, MakeStrokeStyle(Color::Green(), 1)});
  groups.push_back(group9);

  // Row 4: Fill+Stroke
  auto group10 = std::make_shared<VectorGroup>();
  group10->setPosition({col2X, 86 + rowHeight * 3});
  auto textSpan10 = std::make_shared<TextSpan>();
  textSpan10->setTextBlob(TextBlob::MakeFrom("Fill+Stroke", boldFont));
  auto selector10 = std::make_shared<RangeSelector>();
  selector10->setShape(SelectorShape::RampUp);
  auto modifier10 = std::make_shared<TextModifier>();
  modifier10->setSelectors({selector10});
  modifier10->setFillColor(Color::Red());
  modifier10->setStrokeColor(Color::Green());
  group10->setElements({textSpan10, modifier10, MakeFillStyle(Color::Blue()),
                        MakeStrokeStyle(Color(0.5f, 0.5f, 0.5f), 2)});
  groups.push_back(group10);

  // Row 5: MultiSpan
  auto group11 = std::make_shared<VectorGroup>();
  group11->setPosition({col2X, 86 + rowHeight * 4});
  auto textSpanA = std::make_shared<TextSpan>();
  textSpanA->setTextBlob(TextBlob::MakeFrom("AB", font));
  auto textSpanB = std::make_shared<TextSpan>();
  textSpanB->setTextBlob(TextBlob::MakeFrom("CD", font));
  textSpanB->setPosition({55, 0});
  auto textSpanC = std::make_shared<TextSpan>();
  textSpanC->setTextBlob(TextBlob::MakeFrom("EF", font));
  textSpanC->setPosition({110, 0});
  auto selector11 = std::make_shared<RangeSelector>();
  selector11->setShape(SelectorShape::RampUp);
  auto modifier11 = std::make_shared<TextModifier>();
  modifier11->setSelectors({selector11});
  modifier11->setPosition({0, -20});
  modifier11->setFillColor(Color::Red());
  group11->setElements({textSpanA, textSpanB, textSpanC, modifier11, MakeFillStyle(Color::Blue())});
  groups.push_back(group11);

  // Row 6: Multiple Modifiers stacking
  auto group12 = std::make_shared<VectorGroup>();
  group12->setPosition({col2X, 86 + rowHeight * 5});
  auto textSpan12 = std::make_shared<TextSpan>();
  textSpan12->setTextBlob(TextBlob::MakeFrom("MultiMod", font));
  auto selectorA = std::make_shared<RangeSelector>();
  selectorA->setStart(0.0f);
  selectorA->setEnd(0.5f);
  auto modifierA = std::make_shared<TextModifier>();
  modifierA->setSelectors({selectorA});
  modifierA->setPosition({0, -10});
  auto selectorB = std::make_shared<RangeSelector>();
  selectorB->setStart(0.5f);
  selectorB->setEnd(1.0f);
  auto modifierB = std::make_shared<TextModifier>();
  modifierB->setSelectors({selectorB});
  modifierB->setRotation(15);
  group12->setElements(
      {textSpan12, modifierA, modifierB, MakeFillStyle(Color::FromRGBA(128, 0, 128, 255))});
  groups.push_back(group12);

  // ==================== Column 3: Shape comparison ====================
  float col3X = 470;
  std::vector<std::pair<SelectorShape, std::string>> shapes = {
      {SelectorShape::Square, "Square"},     {SelectorShape::RampUp, "RampUp"},
      {SelectorShape::RampDown, "RampDown"}, {SelectorShape::Triangle, "Triangle"},
      {SelectorShape::Round, "Round"},       {SelectorShape::Smooth, "Smooth"}};

  for (size_t i = 0; i < shapes.size(); i++) {
    auto group = std::make_shared<VectorGroup>();
    group->setPosition({col3X, 86 + rowHeight * static_cast<float>(i)});
    auto textSpan = std::make_shared<TextSpan>();
    textSpan->setTextBlob(TextBlob::MakeFrom(shapes[i].second, font));
    auto selector = std::make_shared<RangeSelector>();
    selector->setShape(shapes[i].first);
    auto modifier = std::make_shared<TextModifier>();
    modifier->setSelectors({selector});
    modifier->setPosition({0, -15});
    group->setElements({textSpan, modifier, MakeFillStyle(Color::Blue())});
    groups.push_back(group);
  }

  // ==================== Column 4: RangeSelector properties ====================
  float col4X = 680;

  // Row 1: EaseIn (Triangle + EaseIn)
  auto groupEL = std::make_shared<VectorGroup>();
  groupEL->setPosition({col4X, 86});
  auto textSpanEL = std::make_shared<TextSpan>();
  textSpanEL->setTextBlob(TextBlob::MakeFrom("EaseIn", font));
  auto selectorEL = std::make_shared<RangeSelector>();
  selectorEL->setShape(SelectorShape::Triangle);
  selectorEL->setEaseIn(0.8f);
  auto modifierEL = std::make_shared<TextModifier>();
  modifierEL->setSelectors({selectorEL});
  modifierEL->setPosition({0, -15});
  groupEL->setElements({textSpanEL, modifierEL, MakeFillStyle(Color::Blue())});
  groups.push_back(groupEL);

  // Row 2: EaseOut (Triangle + EaseOut)
  auto groupEH = std::make_shared<VectorGroup>();
  groupEH->setPosition({col4X, 86 + rowHeight});
  auto textSpanEH = std::make_shared<TextSpan>();
  textSpanEH->setTextBlob(TextBlob::MakeFrom("EaseOut", font));
  auto selectorEH = std::make_shared<RangeSelector>();
  selectorEH->setShape(SelectorShape::Triangle);
  selectorEH->setEaseOut(0.8f);
  auto modifierEH = std::make_shared<TextModifier>();
  modifierEH->setSelectors({selectorEH});
  modifierEH->setPosition({0, -15});
  groupEH->setElements({textSpanEH, modifierEH, MakeFillStyle(Color::Red())});
  groups.push_back(groupEH);

  // Row 3: EaseBoth (Triangle + EaseOut + EaseIn)
  auto groupEB = std::make_shared<VectorGroup>();
  groupEB->setPosition({col4X, 86 + rowHeight * 2});
  auto textSpanEB = std::make_shared<TextSpan>();
  textSpanEB->setTextBlob(TextBlob::MakeFrom("EaseBoth", font));
  auto selectorEB = std::make_shared<RangeSelector>();
  selectorEB->setShape(SelectorShape::Triangle);
  selectorEB->setEaseOut(0.6f);
  selectorEB->setEaseIn(0.6f);
  auto modifierEB = std::make_shared<TextModifier>();
  modifierEB->setSelectors({selectorEB});
  modifierEB->setPosition({0, -15});
  groupEB->setElements({textSpanEB, modifierEB, MakeFillStyle(Color::Green())});
  groups.push_back(groupEB);

  // Row 4: Unit (Index)
  auto groupUnit = std::make_shared<VectorGroup>();
  groupUnit->setPosition({col4X, 86 + rowHeight * 3});
  auto textSpanUnit = std::make_shared<TextSpan>();
  textSpanUnit->setTextBlob(TextBlob::MakeFrom("ABCDEFGH", font));
  auto selectorUnit = std::make_shared<RangeSelector>();
  selectorUnit->setUnit(SelectorUnit::Index);
  selectorUnit->setStart(2);
  selectorUnit->setEnd(6);
  auto modifierUnit = std::make_shared<TextModifier>();
  modifierUnit->setSelectors({selectorUnit});
  modifierUnit->setFillColor(Color::Red());
  groupUnit->setElements({textSpanUnit, modifierUnit, MakeFillStyle(Color::Blue())});
  groups.push_back(groupUnit);

  // Row 5: Negative Offset
  auto groupOff = std::make_shared<VectorGroup>();
  groupOff->setPosition({col4X, 86 + rowHeight * 4});
  auto textSpanOff = std::make_shared<TextSpan>();
  textSpanOff->setTextBlob(TextBlob::MakeFrom("NegOffset", font));
  auto selectorOff = std::make_shared<RangeSelector>();
  selectorOff->setStart(0.5f);
  selectorOff->setEnd(1.0f);
  selectorOff->setOffset(-0.3f);
  auto modifierOff = std::make_shared<TextModifier>();
  modifierOff->setSelectors({selectorOff});
  modifierOff->setFillColor(Color::Green());
  groupOff->setElements({textSpanOff, modifierOff, MakeFillStyle(Color::Blue())});
  groups.push_back(groupOff);

  // Row 6: Reversed (Start > End)
  auto groupRev = std::make_shared<VectorGroup>();
  groupRev->setPosition({col4X, 86 + rowHeight * 5});
  auto textSpanRev = std::make_shared<TextSpan>();
  textSpanRev->setTextBlob(TextBlob::MakeFrom("Reversed", font));
  auto selectorRev = std::make_shared<RangeSelector>();
  selectorRev->setStart(0.7f);
  selectorRev->setEnd(0.3f);
  auto modifierRev = std::make_shared<TextModifier>();
  modifierRev->setSelectors({selectorRev});
  modifierRev->setFillColor(Color::FromRGBA(255, 128, 0, 255));
  groupRev->setElements({textSpanRev, modifierRev, MakeFillStyle(Color::Blue())});
  groups.push_back(groupRev);

  // ==================== Column 5: Edge cases ====================
  float col5X = 910;

  // Row 1: Random
  auto groupRnd = std::make_shared<VectorGroup>();
  groupRnd->setPosition({col5X, 86});
  auto textSpanRnd = std::make_shared<TextSpan>();
  textSpanRnd->setTextBlob(TextBlob::MakeFrom("Random", font));
  auto selectorRnd = std::make_shared<RangeSelector>();
  selectorRnd->setShape(SelectorShape::RampUp);
  selectorRnd->setRandomizeOrder(true);
  selectorRnd->setRandomSeed(12345);
  auto modifierRnd = std::make_shared<TextModifier>();
  modifierRnd->setSelectors({selectorRnd});
  modifierRnd->setPosition({0, -12});
  groupRnd->setElements(
      {textSpanRnd, modifierRnd, MakeFillStyle(Color::FromRGBA(128, 0, 128, 255))});
  groups.push_back(groupRnd);

  // Row 2: Empty selector
  auto groupEmpty = std::make_shared<VectorGroup>();
  groupEmpty->setPosition({col5X, 86 + rowHeight});
  auto textSpanEmpty = std::make_shared<TextSpan>();
  textSpanEmpty->setTextBlob(TextBlob::MakeFrom("Empty", font));
  auto modifierEmpty = std::make_shared<TextModifier>();
  modifierEmpty->setPosition({0, -10});
  groupEmpty->setElements({textSpanEmpty, modifierEmpty, MakeFillStyle(Color::Black())});
  groups.push_back(groupEmpty);

  // Row 3: Start == End boundary
  auto groupSE = std::make_shared<VectorGroup>();
  groupSE->setPosition({col5X, 86 + rowHeight * 2});
  auto textSpanSE = std::make_shared<TextSpan>();
  textSpanSE->setTextBlob(TextBlob::MakeFrom("StartEnd", font));
  auto selectorSE = std::make_shared<RangeSelector>();
  selectorSE->setStart(0.5f);
  selectorSE->setEnd(0.5f);
  auto modifierSE = std::make_shared<TextModifier>();
  modifierSE->setSelectors({selectorSE});
  modifierSE->setFillColor(Color::Red());
  groupSE->setElements({textSpanSE, modifierSE, MakeFillStyle(Color::Black())});
  groups.push_back(groupSE);

  // Row 4: First selector uses Subtract mode
  auto groupSub = std::make_shared<VectorGroup>();
  groupSub->setPosition({col5X, 86 + rowHeight * 3});
  auto textSpanSub = std::make_shared<TextSpan>();
  textSpanSub->setTextBlob(TextBlob::MakeFrom("SubFirst", font));
  auto selectorSub = std::make_shared<RangeSelector>();
  selectorSub->setMode(SelectorMode::Subtract);
  selectorSub->setShape(SelectorShape::Triangle);
  auto modifierSub = std::make_shared<TextModifier>();
  modifierSub->setSelectors({selectorSub});
  modifierSub->setPosition({0, -15});
  groupSub->setElements(
      {textSpanSub, modifierSub, MakeFillStyle(Color::FromRGBA(255, 128, 0, 255))});
  groups.push_back(groupSub);

  std::vector<std::shared_ptr<VectorElement>> contents = {};
  for (const auto& group : groups) {
    contents.push_back(group);
  }
  vectorLayer->setContents(contents);

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/TextModifier"));
}

/**
 * Test TextSelector base class properties:
 * Column 1: SelectorMode (Add, Subtract, Intersect, Min, Max, Difference)
 * Column 2: Amount variations (1.0, 0.5, 0.0, -0.5, 1.5) + Edge cases (ThreeSels)
 */
TGFX_TEST(VectorLayerTest, TextSelector) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 435, 460);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  auto displayList = std::make_unique<DisplayList>();
  auto vectorLayer = VectorLayer::Make();

  auto typeface = GetTestTypeface();
  if (typeface == nullptr) {
    return;
  }
  Font font(typeface, 22);

  std::vector<std::shared_ptr<VectorGroup>> groups = {};
  float rowHeight = 65;
  float col1X = 50;
  float col2X = 270;

  // Helper to create a baseline reference line
  auto makeBaseline = [](float width) {
    auto group = std::make_shared<VectorGroup>();
    Path linePath = {};
    linePath.moveTo(0, 0);
    linePath.lineTo(width, 0);
    auto shapePath = std::make_shared<ShapePath>();
    shapePath->setPath(linePath);
    auto stroke = std::make_shared<StrokeStyle>();
    stroke->setColorSource(SolidColor::Make(Color{0.8f, 0.8f, 0.8f}));
    stroke->setStrokeWidth(1);
    group->setElements({shapePath, stroke});
    return group;
  };

  // ==================== Column 1: Modes with two overlapping selectors ====================
  std::vector<std::pair<SelectorMode, std::string>> modes = {
      {SelectorMode::Add, "Add-Mode"},        {SelectorMode::Subtract, "Subtract"},
      {SelectorMode::Intersect, "Intersect"}, {SelectorMode::Min, "Min-Mode"},
      {SelectorMode::Max, "Max-Mode"},        {SelectorMode::Difference, "Difference"}};

  for (size_t i = 0; i < modes.size(); i++) {
    float y = 84 + rowHeight * static_cast<float>(i);

    // Baseline reference line
    auto baseline = makeBaseline(105);
    baseline->setPosition({col1X, y});
    groups.push_back(baseline);

    auto group = std::make_shared<VectorGroup>();
    group->setPosition({col1X, y});
    auto textSpan = std::make_shared<TextSpan>();
    textSpan->setTextBlob(TextBlob::MakeFrom(modes[i].second, font));

    // Each string has 8-10 chars
    // Selector1: first 60% with Square shape, amount=0.6
    auto selector1 = std::make_shared<RangeSelector>();
    selector1->setShape(SelectorShape::Square);
    selector1->setStart(0.0f);
    selector1->setEnd(0.6f);
    selector1->setWeight(0.6f);

    // Selector2: last 60% with Square shape, amount=0.4, overlapping 20% in middle
    auto selector2 = std::make_shared<RangeSelector>();
    selector2->setShape(SelectorShape::Square);
    selector2->setStart(0.4f);
    selector2->setEnd(1.0f);
    selector2->setWeight(0.4f);
    selector2->setMode(modes[i].first);

    auto modifier = std::make_shared<TextModifier>();
    modifier->setSelectors({selector1, selector2});
    modifier->setPosition({0, -20});

    group->setElements({textSpan, modifier, MakeFillStyle(Color::Blue())});
    groups.push_back(group);
  }

  // ==================== Column 2: Amount variations ====================
  std::vector<std::pair<float, std::string>> amounts = {{1.0f, "Amount 1.0"},
                                                        {0.5f, "Amount 0.5"},
                                                        {0.0f, "Amount 0.0"},
                                                        {-0.5f, "Amount -0.5"},
                                                        {1.5f, "Amount 1.5"}};

  for (size_t i = 0; i < amounts.size(); i++) {
    float y = 84 + rowHeight * static_cast<float>(i);

    // Baseline reference line
    auto baseline = makeBaseline(130);
    baseline->setPosition({col2X, y});
    groups.push_back(baseline);

    auto group = std::make_shared<VectorGroup>();
    group->setPosition({col2X, y});
    auto textSpan = std::make_shared<TextSpan>();
    textSpan->setTextBlob(TextBlob::MakeFrom(amounts[i].second, font));

    auto selector = std::make_shared<RangeSelector>();
    selector->setShape(SelectorShape::Triangle);
    selector->setWeight(amounts[i].first);

    auto modifier = std::make_shared<TextModifier>();
    modifier->setSelectors({selector});
    modifier->setPosition({0, -20});

    group->setElements({textSpan, modifier, MakeFillStyle(Color::Red())});
    groups.push_back(group);
  }

  // ==================== Column 2 Row 6: Edge cases ====================
  // Row 6: Three selectors combination
  {
    float y = 84 + rowHeight * 5;
    auto baseline = makeBaseline(130);
    baseline->setPosition({col2X, y});
    groups.push_back(baseline);

    auto group = std::make_shared<VectorGroup>();
    group->setPosition({col2X, y});
    auto textSpan = std::make_shared<TextSpan>();
    textSpan->setTextBlob(TextBlob::MakeFrom("ThreeSels", font));

    // Selector1: [0, 0.4], amount=0.5
    auto selector1 = std::make_shared<RangeSelector>();
    selector1->setShape(SelectorShape::Square);
    selector1->setStart(0.0f);
    selector1->setEnd(0.4f);
    selector1->setWeight(0.5f);

    // Selector2: [0.3, 0.7], amount=0.5, Add
    auto selector2 = std::make_shared<RangeSelector>();
    selector2->setShape(SelectorShape::Square);
    selector2->setStart(0.3f);
    selector2->setEnd(0.7f);
    selector2->setWeight(0.5f);
    selector2->setMode(SelectorMode::Add);

    // Selector3: [0.6, 1.0], amount=0.5, Add
    auto selector3 = std::make_shared<RangeSelector>();
    selector3->setShape(SelectorShape::Square);
    selector3->setStart(0.6f);
    selector3->setEnd(1.0f);
    selector3->setWeight(0.5f);
    selector3->setMode(SelectorMode::Add);

    auto modifier = std::make_shared<TextModifier>();
    modifier->setSelectors({selector1, selector2, selector3});
    modifier->setPosition({0, -20});

    group->setElements({textSpan, modifier, MakeFillStyle(Color::Green())});
    groups.push_back(group);
  }

  std::vector<std::shared_ptr<VectorElement>> contents = {};
  for (const auto& group : groups) {
    contents.push_back(group);
  }
  vectorLayer->setContents(contents);

  displayList->root()->addChild(vectorLayer);
  displayList->render(surface.get());

  EXPECT_TRUE(Baseline::Compare(surface, "VectorLayerTest/TextSelector"));
}

}  // namespace tgfx

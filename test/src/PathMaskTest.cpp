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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/layers/DisplayList.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidLayer.h"
#include "utils/TestUtils.h"

namespace tgfx {

static std::shared_ptr<ShapeLayer> MakeContent(const Rect& rect, const Color& color) {
  auto content = ShapeLayer::Make();
  Path path;
  path.addRect(rect);
  content->setPath(path);
  content->setFillStyle(ShapeStyle::Make(color));
  return content;
}

static std::shared_ptr<PathMask> MakeCircleMask(float x, float y, float radius) {
  auto mask = PathMask::Make();
  Path path;
  path.addOval(Rect::MakeXYWH(x - radius, y - radius, radius * 2, radius * 2));
  mask->setPath(path);
  return mask;
}

TGFX_TEST(PathMaskTest, Combine) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  DisplayList list;
  auto contentColor = Color::FromRGBA(0, 180, 0);

  // Union: two overlapping circles.
  auto content = MakeContent(Rect::MakeXYWH(10, 10, 80, 80), contentColor);
  auto maskA = MakeCircleMask(40, 40, 30);
  auto maskB = MakeCircleMask(70, 70, 30);
  content->setPathMasks({maskA, maskB});
  list.root()->addChild(content);

  // Subtract: the second circle cuts a bite out of the first one.
  content = MakeContent(Rect::MakeXYWH(110, 10, 80, 80), contentColor);
  maskA = MakeCircleMask(140, 40, 30);
  maskB = MakeCircleMask(170, 70, 30);
  maskB->setOp(PathOp::Difference);
  content->setPathMasks({maskA, maskB});
  list.root()->addChild(content);

  // Intersect: only the overlapping lens stays visible.
  content = MakeContent(Rect::MakeXYWH(210, 10, 80, 80), contentColor);
  maskA = MakeCircleMask(240, 40, 30);
  maskB = MakeCircleMask(270, 70, 30);
  maskB->setOp(PathOp::Intersect);
  content->setPathMasks({maskA, maskB});
  list.root()->addChild(content);

  // XOR: the overlapping area is cut out.
  content = MakeContent(Rect::MakeXYWH(310, 10, 80, 80), contentColor);
  maskA = MakeCircleMask(340, 40, 30);
  maskB = MakeCircleMask(370, 70, 30);
  maskB->setOp(PathOp::XOR);
  content->setPathMasks({maskA, maskB});
  list.root()->addChild(content);

  // Inverted: the content stays visible outside the circle.
  content = MakeContent(Rect::MakeXYWH(410, 10, 80, 80), contentColor);
  maskA = MakeCircleMask(450, 50, 30);
  maskA->setInverted(true);
  content->setPathMasks({maskA});
  list.root()->addChild(content);

  // First mask with Difference op flips its inverted flag: the visible region is everything
  // outside circle A, unioned with circle B.
  content = MakeContent(Rect::MakeXYWH(510, 10, 80, 80), contentColor);
  maskA = MakeCircleMask(540, 40, 30);
  maskA->setOp(PathOp::Difference);
  maskB = MakeCircleMask(570, 70, 30);
  content->setPathMasks({maskA, maskB});
  list.root()->addChild(content);

  // Positive expansion enlarges the circle by 10 pixels.
  content = MakeContent(Rect::MakeXYWH(610, 10, 80, 80), contentColor);
  maskA = MakeCircleMask(650, 50, 30);
  maskA->setExpansion(10);
  content->setPathMasks({maskA});
  list.root()->addChild(content);

  // Negative expansion shrinks the circle by 10 pixels.
  content = MakeContent(Rect::MakeXYWH(10, 110, 80, 80), contentColor);
  maskA = MakeCircleMask(50, 150, 30);
  maskA->setExpansion(-10);
  content->setPathMasks({maskA});
  list.root()->addChild(content);

  // Masks with empty paths are skipped, so the content stays fully visible.
  content = MakeContent(Rect::MakeXYWH(110, 110, 80, 80), contentColor);
  content->setPathMasks({PathMask::Make()});
  list.root()->addChild(content);

  // Disjoint circles combined by Intersect produce an empty region, hiding the whole layer.
  content = MakeContent(Rect::MakeXYWH(210, 110, 80, 80), contentColor);
  maskA = MakeCircleMask(230, 130, 15);
  maskB = MakeCircleMask(280, 180, 15);
  maskB->setOp(PathOp::Intersect);
  content->setPathMasks({maskA, maskB});
  list.root()->addChild(content);

  auto surface = Surface::Make(context, 700, 200);
  list.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "PathMaskTest/Combine"));
}

TGFX_TEST(PathMaskTest, Feather) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  DisplayList list;
  auto contentColor = Color::FromRGBA(0, 180, 0);

  auto background = SolidLayer::Make();
  background->setWidth(400);
  background->setHeight(100);
  background->setColor(Color::White());
  list.root()->addChild(background);

  // Feathered circle with soft edges.
  auto content = MakeContent(Rect::MakeXYWH(10, 10, 80, 80), contentColor);
  auto maskA = MakeCircleMask(50, 50, 30);
  maskA->setFeather(Point::Make(20, 20));
  content->setPathMasks({maskA});
  list.root()->addChild(content);

  // Half-opacity mask, showing the white background through the content.
  content = MakeContent(Rect::MakeXYWH(110, 10, 80, 80), contentColor);
  maskA = MakeCircleMask(150, 50, 30);
  maskA->setOpacity(0.5f);
  content->setPathMasks({maskA});
  list.root()->addChild(content);

  // Two feathered masks are composited in SrcOver order, boolean ops are ignored.
  content = MakeContent(Rect::MakeXYWH(210, 10, 80, 80), contentColor);
  maskA = MakeCircleMask(240, 40, 30);
  maskA->setFeather(Point::Make(10, 10));
  auto maskB = MakeCircleMask(270, 70, 30);
  maskB->setFeather(Point::Make(10, 10));
  maskB->setOp(PathOp::Difference);
  content->setPathMasks({maskA, maskB});
  list.root()->addChild(content);

  // Inverted feathered mask: soft edge, visible outside the circle.
  content = MakeContent(Rect::MakeXYWH(310, 10, 80, 80), contentColor);
  maskA = MakeCircleMask(350, 50, 30);
  maskA->setFeather(Point::Make(10, 10));
  maskA->setInverted(true);
  content->setPathMasks({maskA});
  list.root()->addChild(content);

  auto surface = Surface::Make(context, 400, 100);
  list.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "PathMaskTest/Feather"));
}

TGFX_TEST(PathMaskTest, WithLayerMask) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  DisplayList list;
  auto contentColor = Color::FromRGBA(0, 180, 0);

  // A clip-path path mask combined with an alpha layer mask: circle intersect right half.
  auto content = MakeContent(Rect::MakeXYWH(10, 10, 80, 80), contentColor);
  content->setPathMasks({MakeCircleMask(50, 50, 30)});
  auto layerMask = ShapeLayer::Make();
  Path halfPath;
  halfPath.addRect(Rect::MakeXYWH(50, 0, 50, 100));
  layerMask->setPath(halfPath);
  layerMask->setFillStyle(ShapeStyle::Make(Color::White()));
  content->setMask(layerMask);
  list.root()->addChild(content);
  list.root()->addChild(layerMask);

  // A feathered path mask composed with a luminance layer mask at the shader level:
  // soft circle intersect right half.
  content = MakeContent(Rect::MakeXYWH(110, 10, 80, 80), contentColor);
  auto maskA = MakeCircleMask(150, 50, 30);
  maskA->setFeather(Point::Make(10, 10));
  content->setPathMasks({maskA});
  layerMask = ShapeLayer::Make();
  halfPath = {};
  halfPath.addRect(Rect::MakeXYWH(150, 0, 50, 100));
  layerMask->setPath(halfPath);
  layerMask->setFillStyle(ShapeStyle::Make(Color::White()));
  content->setMask(layerMask);
  content->setMaskType(LayerMaskType::Luminance);
  list.root()->addChild(content);
  list.root()->addChild(layerMask);

  // A feathered path mask composed with an inverted luminance layer mask:
  // soft circle with the right half removed.
  content = MakeContent(Rect::MakeXYWH(210, 10, 80, 80), contentColor);
  maskA = MakeCircleMask(250, 50, 30);
  maskA->setFeather(Point::Make(10, 10));
  content->setPathMasks({maskA});
  layerMask = ShapeLayer::Make();
  halfPath = {};
  halfPath.addRect(Rect::MakeXYWH(250, 0, 50, 100));
  layerMask->setPath(halfPath);
  layerMask->setFillStyle(ShapeStyle::Make(Color::White()));
  content->setMask(layerMask);
  content->setMaskType(LayerMaskType::LuminanceInverted);
  list.root()->addChild(content);
  list.root()->addChild(layerMask);

  auto surface = Surface::Make(context, 300, 100);
  list.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "PathMaskTest/WithLayerMask"));
}

TGFX_TEST(PathMaskTest, Animation) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  DisplayList list;
  auto contentColor = Color::FromRGBA(0, 180, 0);

  auto content = MakeContent(Rect::MakeXYWH(10, 10, 80, 80), contentColor);
  auto mask = MakeCircleMask(35, 50, 20);
  content->setPathMasks({mask});
  list.root()->addChild(content);

  auto surface = Surface::Make(context, 100, 100);
  list.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "PathMaskTest/Animation_Start"));

  // Changing the mask path invalidates the layer content and takes effect on the next frame.
  Path newPath;
  newPath.addOval(Rect::MakeXYWH(45, 30, 40, 40));
  mask->setPath(newPath);
  list.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "PathMaskTest/Animation_End"));
}

TGFX_TEST(PathMaskTest, Invalidation) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  DisplayList list;
  auto contentColor = Color::FromRGBA(0, 180, 0);

  auto content = MakeContent(Rect::MakeXYWH(10, 10, 80, 80), contentColor);
  content->setPathMasks({MakeCircleMask(50, 50, 30)});
  list.root()->addChild(content);

  auto surface = Surface::Make(context, 100, 100);
  list.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "PathMaskTest/Invalidation_Masked"));

  // Clearing the path masks invalidates the layer, so the content renders in full again.
  content->setPathMasks({});
  list.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "PathMaskTest/Invalidation_Cleared"));
}

TGFX_TEST(PathMaskTest, Transform) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  DisplayList list;
  auto contentColor = Color::FromRGBA(0, 180, 0);

  // The group matrix scales and translates both the content and its path masks, so the mask
  // circle must stay centered on the content.
  auto group = Layer::Make();
  auto content = MakeContent(Rect::MakeXYWH(10, 10, 40, 40), contentColor);
  content->setPathMasks({MakeCircleMask(30, 30, 15)});
  group->addChild(content);
  group->setMatrix(Matrix::MakeAll(2, 0, 20, 0, 2, 10));
  list.root()->addChild(group);

  // A feathered mask under a scaled group: the blur radius must scale with the content.
  group = Layer::Make();
  content = MakeContent(Rect::MakeXYWH(10, 10, 40, 40), contentColor);
  auto featherMask = MakeCircleMask(30, 30, 15);
  featherMask->setFeather(Point::Make(5, 5));
  content->setPathMasks({featherMask});
  group->addChild(content);
  group->setMatrix(Matrix::MakeAll(2, 0, 120, 0, 2, 10));
  list.root()->addChild(group);

  auto surface = Surface::Make(context, 220, 110);
  list.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "PathMaskTest/Transform"));
}

TGFX_TEST(PathMaskTest, ScrollRect) {
  ContextScope scope;
  auto context = scope.getContext();
  EXPECT_TRUE(context != nullptr);
  DisplayList list;
  auto contentColor = Color::FromRGBA(0, 180, 0);

  // The scroll rect crops the content to the left half, and the circle path mask crops it
  // further: only the left half of the circle remains visible.
  auto content = MakeContent(Rect::MakeXYWH(10, 10, 80, 80), contentColor);
  content->setScrollRect(Rect::MakeXYWH(10, 10, 40, 80));
  content->setPathMasks({MakeCircleMask(50, 50, 30)});
  list.root()->addChild(content);

  auto surface = Surface::Make(context, 100, 100);
  list.render(surface.get());
  EXPECT_TRUE(Baseline::Compare(surface, "PathMaskTest/ScrollRect"));
}

}  // namespace tgfx

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

#include "core/ClipStack.h"
#include "core/utils/BlockAllocator.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RectEffect.h"
#include "gtest/gtest.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Surface.h"
#include "utils/TestUtils.h"

namespace tgfx {

static Path MakePath(std::initializer_list<Point> points, bool close = true) {
  Path path;
  auto it = points.begin();
  if (it != points.end()) {
    path.moveTo(*it++);
    while (it != points.end()) {
      path.lineTo(*it++);
    }
    if (close) {
      path.close();
    }
  }
  return path;
}

TGFX_TEST(ClipTest, RectEffect) {
  BlockAllocator allocator;

  // Case 1: Identity matrix produces identity variant.
  {
    auto fp = RectEffect::Make(&allocator, Rect::MakeWH(100, 80), Matrix::I(), true);
    ASSERT_NE(fp, nullptr);
    EXPECT_FALSE(fp->needTransform);
    EXPECT_EQ(fp->localRect, Rect::MakeWH(100, 80));
    EXPECT_TRUE(fp->deviceToLocal.isIdentity());
  }

  // Case 2: Axis-aligned matrix bakes into localRect, still identity variant.
  {
    auto fp = RectEffect::Make(&allocator, Rect::MakeWH(100, 80),
                               Matrix::MakeTrans(10, 20) * Matrix::MakeScale(2, 3), true);
    ASSERT_NE(fp, nullptr);
    EXPECT_FALSE(fp->needTransform);
    EXPECT_EQ(fp->localRect, Rect::MakeLTRB(10, 20, 210, 260));
    EXPECT_TRUE(fp->deviceToLocal.isIdentity());
  }

  // Case 3: Non-axis-aligned matrix produces transform variant.
  {
    Matrix m = {};
    m.setRotate(30);
    auto fp = RectEffect::Make(&allocator, Rect::MakeWH(100, 80), m, true);
    ASSERT_NE(fp, nullptr);
    EXPECT_TRUE(fp->needTransform);
    EXPECT_FALSE(fp->deviceToLocal.isIdentity());
  }

  // Case 4: Perspective matrix returns nullptr.
  {
    Matrix m = Matrix::I();
    m.setAll(1, 0, 0, 0, 1, 0, 0.001f, 0, 1);
    auto fp = RectEffect::Make(&allocator, Rect::MakeWH(100, 80), m, true);
    EXPECT_EQ(fp, nullptr);
  }

  // Case 5: Zero-scale matrix returns nullptr.
  {
    auto fp = RectEffect::Make(&allocator, Rect::MakeWH(100, 80), Matrix::MakeScale(0, 1), true);
    EXPECT_EQ(fp, nullptr);
  }
}

TGFX_TEST(ClipTest, RRectEffect) {
  BlockAllocator allocator;

  // Case 1: Simple RRect + identity produces identity variant.
  {
    const auto rRect = RRect::MakeRectXY(Rect::MakeWH(100, 80), 10, 12);
    auto fp = RRectEffect::Make(&allocator, rRect, Matrix::I(), true);
    ASSERT_NE(fp, nullptr);
    EXPECT_FALSE(fp->needTransform);
    EXPECT_EQ(fp->localRect, rRect.rect());
    EXPECT_EQ(fp->radii[0], (Point{10, 12}));
  }

  // Case 2: Scale matrix bakes axis scales into localRect and radii.
  {
    const auto rRect = RRect::MakeRectXY(Rect::MakeWH(100, 80), 10, 12);
    auto fp = RRectEffect::Make(&allocator, rRect, Matrix::MakeScale(2, 3), true);
    ASSERT_NE(fp, nullptr);
    EXPECT_TRUE(fp->needTransform);
    EXPECT_FLOAT_EQ(fp->localRect.width(), 200.f);
    EXPECT_FLOAT_EQ(fp->localRect.height(), 240.f);
    EXPECT_FLOAT_EQ(fp->radii[0].x, 20.f);
    EXPECT_FLOAT_EQ(fp->radii[0].y, 36.f);
  }

  // Case 3: Perspective matrix returns nullptr.
  {
    Matrix m = Matrix::I();
    m.setAll(1, 0, 0, 0, 1, 0, 0.001f, 0, 1);
    auto fp =
        RRectEffect::Make(&allocator, RRect::MakeRectXY(Rect::MakeWH(100, 80), 10, 12), m, true);
    EXPECT_EQ(fp, nullptr);
  }

  // Case 4: Zero-scale matrix returns nullptr.
  {
    auto fp = RRectEffect::Make(&allocator, RRect::MakeRectXY(Rect::MakeWH(100, 80), 10, 12),
                                Matrix::MakeScale(0, 1), true);
    EXPECT_EQ(fp, nullptr);
  }
}

TGFX_TEST(ClipTest, State) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 100, 100);
  auto canvas = surface->getCanvas();

  // Case 1: WideOpen is the initial state.
  TGFX_PRIVATE_ACCESS({
    EXPECT_EQ(canvas->clipStack->state(), ClipState::WideOpen);
    EXPECT_TRUE(canvas->clipStack->elements().empty());
  });

  // Case 2: Single rect clip transitions to Rect state.
  canvas->clipRect(Rect::MakeXYWH(10, 20, 80, 60), true);
  TGFX_PRIVATE_ACCESS({
    EXPECT_EQ(canvas->clipStack->state(), ClipState::Rect);
    EXPECT_EQ(canvas->clipStack->bounds(), Rect::MakeXYWH(10, 20, 80, 60));
  });

  // Case 3: Non-rect clip transitions to Complex state.
  canvas->save();
  Path path = {};
  path.addOval(Rect::MakeXYWH(20, 30, 40, 40));
  canvas->clipPath(path, true);
  TGFX_PRIVATE_ACCESS({ EXPECT_EQ(canvas->clipStack->state(), ClipState::Complex); });
  canvas->restore();

  // Case 4: Disjoint clip transitions to Empty state.
  canvas->save();
  canvas->clipRect(Rect::MakeXYWH(300, 300, 10, 10), true);
  TGFX_PRIVATE_ACCESS({ EXPECT_EQ(canvas->clipStack->state(), ClipState::Empty); });
  canvas->restore();

  // Case 5: Transforming ClipStack changes Rect state to Complex.
  TGFX_PRIVATE_ACCESS({
    EXPECT_EQ(canvas->clipStack->state(), ClipState::Rect);
    canvas->clipStack->transform(Matrix::MakeRotate(45.0f));
    EXPECT_EQ(canvas->clipStack->state(), ClipState::Complex);
  });
}

TGFX_TEST(ClipTest, ElementType) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 100, 100);
  auto canvas = surface->getCanvas();

  // Case 1: clipRect -> Type::Rect.
  {
    canvas->save();
    canvas->clipRect(Rect::MakeXYWH(20, 30, 40, 40), true);
    TGFX_PRIVATE_ACCESS({
      const auto& elems = canvas->clipStack->elements();
      ASSERT_EQ(elems.size(), 1u);
      EXPECT_EQ(elems[0].shape().type(), GeometryShape::Type::Rect);
      EXPECT_EQ(elems[0].shape().rect(), Rect::MakeXYWH(20, 30, 40, 40));
      EXPECT_TRUE(elems[0].antiAlias());
    });
    canvas->restore();
  }

  // Case 2: clipRRect(Simple) -> Type::RRect.
  {
    canvas->save();
    canvas->clipRRect(RRect::MakeRectXY(Rect::MakeXYWH(20, 30, 40, 40), 8, 12), true);
    TGFX_PRIVATE_ACCESS({
      const auto& elems = canvas->clipStack->elements();
      ASSERT_EQ(elems.size(), 1u);
      EXPECT_EQ(elems[0].shape().type(), GeometryShape::Type::RRect);
      EXPECT_EQ(elems[0].shape().rRect().type(), RRect::Type::Simple);
    });
    canvas->restore();
  }

  // Case 3: clipRRect with zero radii -> degenerates to Type::Rect.
  {
    canvas->save();
    canvas->clipRRect(RRect::MakeRectXY(Rect::MakeWH(80, 60), 0, 0), true);
    TGFX_PRIVATE_ACCESS({
      const auto& elems = canvas->clipStack->elements();
      ASSERT_EQ(elems.size(), 1u);
      EXPECT_EQ(elems[0].shape().type(), GeometryShape::Type::Rect);
      EXPECT_EQ(elems[0].shape().rect(), Rect::MakeWH(80, 60));
    });
    canvas->restore();
  }

  // Case 4: clipPath(rect-shaped) -> Type::Rect.
  {
    canvas->save();
    Path path = {};
    path.addRect(Rect::MakeXYWH(20, 30, 40, 40));
    canvas->clipPath(path, true);
    TGFX_PRIVATE_ACCESS({
      const auto& elems = canvas->clipStack->elements();
      ASSERT_EQ(elems.size(), 1u);
      EXPECT_EQ(elems[0].shape().type(), GeometryShape::Type::Rect);
      EXPECT_EQ(elems[0].shape().rect(), Rect::MakeXYWH(20, 30, 40, 40));
    });
    canvas->restore();
  }

  // Case 5: clipPath(oval-shaped) -> Type::RRect, Oval.
  {
    canvas->save();
    Path path = {};
    path.addOval(Rect::MakeXYWH(20, 30, 60, 60));
    canvas->clipPath(path, false);
    TGFX_PRIVATE_ACCESS({
      const auto& elems = canvas->clipStack->elements();
      ASSERT_EQ(elems.size(), 1u);
      EXPECT_EQ(elems[0].shape().type(), GeometryShape::Type::RRect);
      EXPECT_EQ(elems[0].shape().rRect().type(), RRect::Type::Oval);
      EXPECT_FALSE(elems[0].antiAlias());
    });
    canvas->restore();
  }

  // Case 6: clipPath(rrect-shaped) -> Type::RRect, Simple.
  {
    canvas->save();
    Path path = {};
    path.addRoundRect(Rect::MakeXYWH(20, 30, 40, 40), 8, 12);
    canvas->clipPath(path, true);
    TGFX_PRIVATE_ACCESS({
      const auto& elems = canvas->clipStack->elements();
      ASSERT_EQ(elems.size(), 1u);
      EXPECT_EQ(elems[0].shape().type(), GeometryShape::Type::RRect);
      EXPECT_EQ(elems[0].shape().rRect().type(), RRect::Type::Simple);
    });
    canvas->restore();
  }

  // Case 7: clipPath(arbitrary polygon) -> Type::Path.
  {
    canvas->save();
    auto path = MakePath({{50, 10}, {90, 90}, {10, 90}});
    canvas->clipPath(path, true);
    TGFX_PRIVATE_ACCESS({
      const auto& elems = canvas->clipStack->elements();
      ASSERT_EQ(elems.size(), 1u);
      EXPECT_EQ(elems[0].shape().type(), GeometryShape::Type::Path);
    });
    canvas->restore();
  }
}

TGFX_TEST(ClipTest, Merge) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 100, 100);
  auto canvas = surface->getCanvas();

  // Case 1: Redundant clip is eliminated.
  {
    canvas->save();
    canvas->clipRect(Rect::MakeXYWH(10, 20, 80, 60), true);
    canvas->clipRect(Rect::MakeXYWH(0, 0, 200, 200), true);
    TGFX_PRIVATE_ACCESS({
      EXPECT_EQ(canvas->clipStack->elements().size(), 1u);
      EXPECT_EQ(canvas->clipStack->bounds(), Rect::MakeXYWH(10, 20, 80, 60));
    });
    canvas->restore();
  }

  // Case 2: Pixel-aligned rect clips merge even when their AA flags differ, because pixel-aligned
  // edges have no visible AA effect. The merged element adopts the existing element's AA flag.
  {
    canvas->save();
    canvas->clipRect(Rect::MakeXYWH(10, 20, 80, 60), false);
    canvas->clipRect(Rect::MakeXYWH(50, 50, 100, 100), true);
    TGFX_PRIVATE_ACCESS({
      EXPECT_EQ(canvas->clipStack->elements().size(), 1u);
      EXPECT_EQ(canvas->clipStack->bounds(), Rect::MakeXYWH(50, 50, 40, 30));
      EXPECT_FALSE(canvas->clipStack->elements().front().antiAlias());
    });
    canvas->restore();
  }

  // Case 3: Non-pixel-aligned AA mismatch keeps two rect elements.
  {
    canvas->save();
    canvas->clipRect(Rect::MakeXYWH(10.5f, 20.5f, 50.0f, 50.0f), true);
    canvas->clipRect(Rect::MakeXYWH(20.5f, 30.5f, 50.0f, 50.0f), false);
    TGFX_PRIVATE_ACCESS({ EXPECT_EQ(canvas->clipStack->elements().size(), 2u); });
    canvas->restore();
  }

  // Case 4: Duplicate RRect-shaped clipPath elimination.
  {
    canvas->save();
    Path path = {};
    path.addRoundRect(Rect::MakeLTRB(10, 10, 90, 90), 10, 10);
    canvas->clipPath(path);
    TGFX_PRIVATE_ACCESS({ EXPECT_EQ(canvas->clipStack->elements().size(), 1u); });
    canvas->clipPath(path);
    TGFX_PRIVATE_ACCESS({ EXPECT_EQ(canvas->clipStack->elements().size(), 1u); });
    canvas->clipPath(path, false);
    TGFX_PRIVATE_ACCESS({ EXPECT_EQ(canvas->clipStack->elements().size(), 2u); });
    canvas->restore();
  }

  // Case 5: Partially-overlapping RRects merge conservatively.
  {
    canvas->save();
    canvas->clipRRect(RRect::MakeRectXY(Rect::MakeLTRB(0, 0, 80, 80), 10, 10));
    canvas->clipRRect(RRect::MakeRectXY(Rect::MakeLTRB(30, 30, 120, 120), 10, 10));
    TGFX_PRIVATE_ACCESS({
      const auto& elems = canvas->clipStack->elements();
      size_t valid = 0;
      const ClipElement* survivor = nullptr;
      for (const auto& e : elems) {
        if (e.isValid()) {
          ++valid;
          survivor = &e;
        }
      }
      ASSERT_EQ(valid, 1u);
      ASSERT_EQ(survivor->shape().type(), GeometryShape::Type::RRect);
      EXPECT_EQ(survivor->shape().rRect().rect(), Rect::MakeLTRB(30, 30, 80, 80));
      EXPECT_EQ(survivor->shape().rRect().radii()[0], (Point{10, 10}));
      EXPECT_EQ(survivor->shape().rRect().radii()[1], (Point{0, 0}));
      EXPECT_EQ(survivor->shape().rRect().radii()[2], (Point{10, 10}));
      EXPECT_EQ(survivor->shape().rRect().radii()[3], (Point{0, 0}));
    });
    canvas->restore();
  }

  // Case 6: Crossing radii keep both RRect clips.
  {
    canvas->save();
    canvas->clipRRect(
        RRect::MakeRectRadii(Rect::MakeWH(100, 100), {{{20, 5}, {0, 0}, {0, 0}, {0, 0}}}));
    canvas->clipRRect(
        RRect::MakeRectRadii(Rect::MakeWH(100, 100), {{{5, 20}, {0, 0}, {0, 0}, {0, 0}}}));
    TGFX_PRIVATE_ACCESS({
      const auto& elems = canvas->clipStack->elements();
      size_t valid = 0;
      for (const auto& e : elems) {
        if (e.isValid()) ++valid;
      }
      EXPECT_EQ(valid, 2u);
    });
    canvas->restore();
  }

  // Case 7: Adjacent radii overflow after intersection rejects the merge.
  {
    canvas->save();
    canvas->clipRRect(
        RRect::MakeRectRadii(Rect::MakeWH(100, 100), {{{60, 20}, {0, 0}, {0, 0}, {0, 0}}}));
    canvas->clipRRect(
        RRect::MakeRectRadii(Rect::MakeWH(100, 100), {{{0, 0}, {60, 20}, {0, 0}, {0, 0}}}));
    TGFX_PRIVATE_ACCESS({
      const auto& elems = canvas->clipStack->elements();
      size_t valid = 0;
      for (const auto& e : elems) {
        if (e.isValid()) ++valid;
      }
      EXPECT_EQ(valid, 2u);
    });
    canvas->restore();
  }

  // Case 8: Different non-axis-aligned matrices skip geometric merge.
  {
    canvas->save();
    Matrix m1 = {};
    m1.setRotate(30);
    canvas->concat(m1);
    canvas->clipRRect(RRect::MakeRectXY(Rect::MakeWH(100, 100), 10, 10));
    canvas->resetMatrix();
    Matrix m2 = {};
    m2.setRotate(60);
    canvas->concat(m2);
    canvas->clipRRect(RRect::MakeRectXY(Rect::MakeWH(100, 100), 10, 10));
    TGFX_PRIVATE_ACCESS({
      const auto& elems = canvas->clipStack->elements();
      size_t valid = 0;
      for (const auto& e : elems) {
        if (e.isValid()) ++valid;
      }
      EXPECT_EQ(valid, 2u);
    });
    canvas->restore();
  }

  // Case 9: AA mismatch on RRect skips merge (rounded corners are AA-sensitive).
  {
    canvas->save();
    canvas->clipRRect(RRect::MakeRectXY(Rect::MakeXYWH(10, 10, 80, 80), 10, 10), true);
    canvas->clipRRect(RRect::MakeRectXY(Rect::MakeXYWH(10, 10, 80, 80), 10, 10), false);
    TGFX_PRIVATE_ACCESS({
      const auto& elems = canvas->clipStack->elements();
      size_t valid = 0;
      for (const auto& e : elems) {
        if (e.isValid()) ++valid;
      }
      EXPECT_EQ(valid, 2u);
    });
    canvas->restore();
  }

  // Case 10: Full containment merges two RRects into one.
  {
    canvas->save();
    canvas->clipRRect(RRect::MakeRectXY(Rect::MakeLTRB(0, 0, 100, 100), 10, 10));
    canvas->clipRRect(RRect::MakeRectXY(Rect::MakeLTRB(10, 10, 90, 90), 10, 10));
    TGFX_PRIVATE_ACCESS({
      const auto& elems = canvas->clipStack->elements();
      size_t valid = 0;
      for (const auto& e : elems) {
        if (e.isValid()) {
          ++valid;
          EXPECT_FALSE(e.outerBounds().isEmpty());
          EXPECT_FALSE(e.innerBounds().isEmpty());
        }
      }
      EXPECT_EQ(valid, 1u);
    });
    canvas->restore();
  }

  // Case 11: RRect + Rect merge succeeds but inner bounds are disjoint.
  {
    canvas->save();
    canvas->clipRRect(RRect::MakeRectRadii(Rect::MakeLTRB(0, 0, 300, 200),
                                           {{{10, 90}, {10, 90}, {20, 90}, {10, 90}}}));
    canvas->clipRect(Rect::MakeLTRB(280, 0, 300, 100));
    TGFX_PRIVATE_ACCESS({
      const auto& elems = canvas->clipStack->elements();
      size_t valid = 0;
      for (const auto& e : elems) {
        if (e.isValid()) {
          ++valid;
          EXPECT_TRUE(e.innerBounds().isEmpty());
          EXPECT_FALSE(e.outerBounds().isEmpty());
        }
      }
      EXPECT_EQ(valid, 1u);
    });
    canvas->restore();
  }

  // Case 12: Nested inverse-fill clips absorb the redundant element. The outer shape's
  // keep-region (everything outside outer) is a subset of the inner shape's keep-region
  // (everything outside inner), so the inner element is redundant. ResolveClipGeometry
  // returns BOnly for the outer toAdd, invalidating inner; appendElement then reuses the
  // freed slot for outer, so elems[1] ends up holding outer.
  {
    canvas->save();
    canvas->clipRect(Rect::MakeWH(100, 100));
    Path inner = {};
    inner.addRect(Rect::MakeLTRB(40, 40, 60, 60));
    inner.toggleInverseFillType();
    canvas->clipPath(inner);
    Path outer = {};
    outer.addRect(Rect::MakeLTRB(20, 20, 80, 80));
    outer.toggleInverseFillType();
    canvas->clipPath(outer);
    TGFX_PRIVATE_ACCESS({
      EXPECT_EQ(canvas->clipStack->state(), ClipState::Complex);
      const auto& elems = canvas->clipStack->elements();
      ASSERT_EQ(elems.size(), 2u);
      // [0] is the initial clipRect, untouched.
      EXPECT_TRUE(elems[0].isValid());
      EXPECT_TRUE(elems[0].shape().isRect());
      EXPECT_EQ(elems[0].outerBounds(), Rect::MakeWH(100, 100));
      // [1] was inner originally; outer's BOnly verdict invalidated inner, and appendElement
      // reused the freed slot for outer.
      EXPECT_TRUE(elems[1].isValid());
      EXPECT_TRUE(elems[1].shape().path().isInverseFillType());
      EXPECT_EQ(elems[1].shape().path().getBounds(), Rect::MakeLTRB(20, 20, 80, 80));
    });
    canvas->restore();
  }

  // Case 13: Non-inverse clip falling entirely inside an inverse-fill hole collapses the
  // combined clip to Empty. The new clipRect's keep-region (its interior) lies entirely
  // inside the inverse-fill clip's removed shape, so the two keep-regions are disjoint
  // and ResolveClipGeometry returns Empty.
  {
    canvas->save();
    canvas->clipRect(Rect::MakeWH(100, 100));
    Path hole = {};
    hole.addRect(Rect::MakeLTRB(20, 20, 80, 80));
    hole.toggleInverseFillType();
    canvas->clipPath(hole);
    TGFX_PRIVATE_ACCESS({ EXPECT_EQ(canvas->clipStack->state(), ClipState::Complex); });
    canvas->clipRect(Rect::MakeLTRB(30, 30, 70, 70));
    TGFX_PRIVATE_ACCESS({ EXPECT_EQ(canvas->clipStack->state(), ClipState::Empty); });
    canvas->restore();
  }

  // Case 14: Inverse-fill clip partially overlapping a non-inverse rect keeps both clips.
  // The new clipRect's bounds partially overlap the inverse-fill's removed shape; neither
  // side's keep-region contains the other, so neither clip is redundant. tryCombine then
  // absorbs the new rect into the existing clipRect element ([0] shrinks to (10,10,40,40)),
  // while the inverse-fill element survives unchanged.
  {
    canvas->save();
    canvas->clipRect(Rect::MakeWH(100, 100));
    Path hole = {};
    hole.addRect(Rect::MakeLTRB(20, 20, 80, 80));
    hole.toggleInverseFillType();
    canvas->clipPath(hole);
    canvas->clipRect(Rect::MakeLTRB(10, 10, 40, 40));
    TGFX_PRIVATE_ACCESS({
      EXPECT_EQ(canvas->clipStack->state(), ClipState::Complex);
      const auto& elems = canvas->clipStack->elements();
      ASSERT_EQ(elems.size(), 2u);
      // [0] is the initial clipRect after tryCombine absorbed the new clipRect into it.
      EXPECT_TRUE(elems[0].isValid());
      EXPECT_TRUE(elems[0].shape().isRect());
      EXPECT_EQ(elems[0].outerBounds(), Rect::MakeLTRB(10, 10, 40, 40));
      // [1] is the inverse-fill hole. Both elements survive because the rect's bounds
      // straddle the hole's boundary, and the hole's shape does not cover the rect.
      EXPECT_TRUE(elems[1].isValid());
      EXPECT_TRUE(elems[1].shape().path().isInverseFillType());
      EXPECT_EQ(elems[1].shape().path().getBounds(), Rect::MakeLTRB(20, 20, 80, 80));
    });
    canvas->restore();
  }
}

TGFX_TEST(ClipTest, Simplify) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 100, 100);
  auto canvas = surface->getCanvas();

  // Case 1: Axis-aligned matrix is baked into local geometry.
  {
    canvas->save();
    canvas->translate(10, 20);
    canvas->scale(2.0f, 3.0f);
    canvas->clipRect(Rect::MakeXYWH(0, 0, 30, 20));
    TGFX_PRIVATE_ACCESS({
      const auto& elem = canvas->clipStack->elements().back();
      EXPECT_EQ(elem.shape().type(), GeometryShape::Type::Rect);
      EXPECT_TRUE(elem.matrix().isIdentity());
      EXPECT_EQ(elem.shape().rect(), Rect::MakeXYWH(10, 20, 60, 60));
    });
    canvas->restore();
  }

  // Case 2: Non-axis-aligned matrix is preserved.
  {
    canvas->save();
    canvas->rotate(45);
    canvas->clipRect(Rect::MakeWH(50, 50));
    TGFX_PRIVATE_ACCESS({
      const auto& elem = canvas->clipStack->elements().back();
      EXPECT_EQ(elem.shape().type(), GeometryShape::Type::Rect);
      EXPECT_FALSE(elem.matrix().isIdentity());
      EXPECT_EQ(elem.shape().rect(), Rect::MakeWH(50, 50));
    });
    canvas->restore();
  }

  // Case 3: Mirrored RRect reorders corner radii.
  {
    canvas->save();
    canvas->scale(-1.0f, 1.0f);
    const auto rRect =
        RRect::MakeRectRadii(Rect::MakeWH(80, 60), {{{20, 10}, {30, 15}, {5, 5}, {15, 15}}});
    canvas->clipRRect(rRect);
    TGFX_PRIVATE_ACCESS({
      const auto& elem = canvas->clipStack->elements().back();
      EXPECT_EQ(elem.shape().type(), GeometryShape::Type::RRect);
      EXPECT_TRUE(elem.matrix().isIdentity());
      EXPECT_EQ(elem.shape().rRect().radii()[0], (Point{30, 15}));
      EXPECT_EQ(elem.shape().rRect().radii()[1], (Point{20, 10}));
      EXPECT_EQ(elem.shape().rRect().radii()[2], (Point{15, 15}));
      EXPECT_EQ(elem.shape().rRect().radii()[3], (Point{5, 5}));
    });
    canvas->restore();
  }

  // Case 4: Zero-radius RRect becomes rect clip element.
  {
    canvas->save();
    canvas->clipRRect(RRect::MakeRectXY(Rect::MakeWH(80, 60), 0, 0));
    TGFX_PRIVATE_ACCESS({
      const auto& elem = canvas->clipStack->elements().back();
      EXPECT_EQ(elem.shape().type(), GeometryShape::Type::Rect);
      EXPECT_EQ(elem.shape().rect(), Rect::MakeWH(80, 60));
    });
    canvas->restore();
  }
}

TGFX_TEST(ClipTest, SaveRestore) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 100, 100);
  auto canvas = surface->getCanvas();

  canvas->clipRect(Rect::MakeXYWH(0, 0, 100, 100), true);
  uint32_t uniqueIDBefore = 0;
  TGFX_PRIVATE_ACCESS({
    uniqueIDBefore = canvas->clipStack->uniqueID();
    EXPECT_EQ(canvas->clipStack->_data->records.size(), 1u);
  });

  // Case 1: Three saves without clip modification do not materialize new records.
  canvas->save();
  canvas->save();
  canvas->save();
  TGFX_PRIVATE_ACCESS({
    EXPECT_EQ(canvas->clipStack->uniqueID(), uniqueIDBefore);
    EXPECT_EQ(canvas->clipStack->_data->records.size(), 1u);
  });

  // Case 2: A modifying clip materializes a new record.
  canvas->clipRect(Rect::MakeXYWH(20, 20, 60, 60), true);
  TGFX_PRIVATE_ACCESS({
    EXPECT_NE(canvas->clipStack->uniqueID(), uniqueIDBefore);
    EXPECT_EQ(canvas->clipStack->_data->records.size(), 2u);
    EXPECT_EQ(canvas->clipStack->bounds(), Rect::MakeXYWH(20, 20, 60, 60));
  });

  // Case 3: Restoring rewinds the materialized record.
  canvas->restore();
  TGFX_PRIVATE_ACCESS({ EXPECT_EQ(canvas->clipStack->_data->records.size(), 1u); });
  canvas->restore();
  canvas->restore();
  TGFX_PRIVATE_ACCESS({
    EXPECT_EQ(canvas->clipStack->_data->records.size(), 1u);
    EXPECT_EQ(canvas->clipStack->bounds(), Rect::MakeXYWH(0, 0, 100, 100));
  });
}

TGFX_TEST(ClipTest, QueryClipAndBounds) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto surface = Surface::Make(context, 100, 100);
  auto canvas = surface->getCanvas();

  // Case 1: getTotalClip and getTotalClipBounds in WideOpen state.
  Path totalClip1 = canvas->getTotalClip();
  EXPECT_TRUE(totalClip1.isInverseFillType());
  EXPECT_FALSE(canvas->getTotalClipBounds().has_value());

  // Case 2: After adding a rect clip.
  canvas->clipRect(Rect::MakeXYWH(10, 10, 80, 80), true);
  Path totalClip2 = canvas->getTotalClip();
  EXPECT_FALSE(totalClip2.isInverseFillType());
  EXPECT_EQ(totalClip2.getBounds(), Rect::MakeXYWH(10, 10, 80, 80));
  auto clipBound = canvas->getTotalClipBounds();
  EXPECT_TRUE(clipBound.has_value());
  EXPECT_EQ(clipBound.value(), Rect::MakeXYWH(10, 10, 80, 80));
}

TGFX_TEST(ClipTest, Overview) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);

  constexpr int kCell = 200;
  constexpr int kCols = 6;
  constexpr int kRows = 3;
  constexpr int kWidth = kCell * kCols;
  constexpr int kHeight = kCell * kRows;
  auto surface = Surface::Make(context, kWidth, kHeight);
  auto canvas = surface->getCanvas();
  canvas->clear(Color::White());

  const Color tints[8] = {{0.95f, 0.92f, 0.88f, 1.0f}, {0.88f, 0.92f, 0.95f, 1.0f},
                          {0.95f, 0.92f, 0.95f, 1.0f}, {0.92f, 0.95f, 0.88f, 1.0f},
                          {0.92f, 0.92f, 0.95f, 1.0f}, {0.95f, 0.95f, 0.88f, 1.0f},
                          {0.88f, 0.95f, 0.92f, 1.0f}, {0.95f, 0.88f, 0.88f, 1.0f}};
  Paint fillPaint = {};
  fillPaint.setColor({0.72f, 0.22f, 0.18f, 1.0f});

  for (int row = 0; row < kRows; ++row) {
    for (int col = 0; col < kCols; ++col) {
      canvas->save();
      canvas->translate(static_cast<float>(col * kCell), static_cast<float>(row * kCell));
      Paint tintPaint = {};
      tintPaint.setColor(tints[(row * kCols + col) % 8]);
      canvas->drawRect(Rect::MakeWH(kCell, kCell), tintPaint);

      const int idx = row * kCols + col;
      switch (idx) {
        // ---------- Rect single-element variants ----------
        case 0: {
          // Cell 0: Rect under shear, AA. Non-orthogonal axes, RectEffect transform variant.
          canvas->translate(kCell / 2.f, kCell / 2.f);
          Matrix shear = Matrix::I();
          shear.setSkew(0.3f, 0);
          canvas->concat(shear);
          canvas->clipRect(Rect::MakeXYWH(-60, -60, 120, 120), true);
          canvas->resetMatrix();
          canvas->translate(static_cast<float>(col * kCell), static_cast<float>(row * kCell));
          canvas->drawRect(Rect::MakeWH(kCell, kCell), fillPaint);
          break;
        }
        case 1: {
          // Cell 1: Perspective Rect clip, NonAA. Falls back to mask texture path.
          canvas->translate(kCell / 2.f, kCell / 2.f);
          Matrix persp = Matrix::I();
          persp.setAll(1, 0, 0, 0, 1, 0, 0.002f, 0, 1);
          canvas->concat(persp);
          canvas->clipRect(Rect::MakeXYWH(-70, -70, 140, 140), false);
          canvas->resetMatrix();
          canvas->translate(static_cast<float>(col * kCell), static_cast<float>(row * kCell));
          canvas->drawRect(Rect::MakeWH(kCell, kCell), fillPaint);
          break;
        }

        // ---------- RRect single-element variants ----------
        case 2:
          // Cell 2: Simple RRect AA, identity. Basic RRectEffect analytic FP.
          canvas->clipRRect(RRect::MakeRectXY(Rect::MakeXYWH(30, 30, 140, 140), 25, 25), true);
          canvas->drawRect(Rect::MakeWH(kCell, kCell), fillPaint);
          break;
        case 3:
          // Cell 3: Complex RRect (4 distinct radii), identity, AA. Arc-box dispatch.
          canvas->clipRRect(RRect::MakeRectRadii(Rect::MakeXYWH(30, 30, 140, 140),
                                                 {{{50, 50}, {12, 30}, {8, 8}, {30, 15}}}),
                            true);
          canvas->drawRect(Rect::MakeWH(kCell, kCell), fillPaint);
          break;
        case 4:
          // Cell 4: Rotated Simple RRect, AA. RRectEffect transform variant.
          canvas->translate(kCell / 2.f, kCell / 2.f);
          canvas->rotate(20);
          canvas->clipRRect(RRect::MakeRectXY(Rect::MakeXYWH(-70, -55, 140, 110), 20, 20), true);
          canvas->translate(-kCell / 2.f, -kCell / 2.f);
          canvas->drawRect(Rect::MakeWH(kCell, kCell), fillPaint);
          break;
        case 5: {
          // Cell 5: Perspective Simple RRect, AA. Falls back to mask texture path.
          canvas->translate(kCell / 2.f, kCell / 2.f);
          Matrix persp = Matrix::I();
          persp.setAll(1, 0, 0, 0, 1, 0, 0.002f, 0, 1);
          canvas->concat(persp);
          canvas->clipRRect(RRect::MakeRectXY(Rect::MakeXYWH(-70, -70, 140, 140), 25, 25), true);
          canvas->resetMatrix();
          canvas->translate(static_cast<float>(col * kCell), static_cast<float>(row * kCell));
          canvas->drawRect(Rect::MakeWH(kCell, kCell), fillPaint);
          break;
        }

        // ---------- Path single-element variants ----------
        case 6:
          // Cell 6: Pentagon Path NonAA. Visually identical to the AA variant because the FreeType
          // path rasterizer always produces an 8-bit AA mask and ignores the antiAlias flag (see
          // FTPathRasterizer::onReadPixels).
          canvas->translate(kCell / 2.f, kCell / 2.f);
          canvas->clipPath(MakePath({{0, -75}, {72, -23}, {44, 60}, {-44, 60}, {-72, -23}}), false);
          canvas->translate(-kCell / 2.f, -kCell / 2.f);
          canvas->drawRect(Rect::MakeWH(kCell, kCell), fillPaint);
          break;
        case 7: {
          // Cell 7: Inverse-fill Path. The inset rect is excluded from the clip, leaving an outer
          // frame where fills remain visible.
          Path path = {};
          path.addRect(Rect::MakeXYWH(40, 40, 120, 120));
          path.toggleInverseFillType();
          canvas->clipPath(path);
          canvas->drawRect(Rect::MakeWH(kCell, kCell), fillPaint);
          break;
        }

        // ---------- Multi-element combinations ----------
        case 8:
          // Cell 8: Two RRects with offset bounds. The intersection's TR/BL corners equal
          // neither A's nor B's, so ConservativeIntersect falls back to CheckCornerContainment
          // and fails because the points lie outside both rounded corners. Both elements
          // survive and produce two FPs.
          canvas->clipRRect(RRect::MakeRectXY(Rect::MakeXYWH(20, 20, 140, 140), 50, 50), true);
          canvas->clipRRect(RRect::MakeRectXY(Rect::MakeXYWH(60, 60, 140, 140), 50, 50), true);
          canvas->drawRect(Rect::MakeWH(kCell, kCell), fillPaint);
          break;
        case 9:
          // Cell 9: 5 RRects overflow. First 4 use analytic FP, 5th falls to mask.
          for (int i = 0; i < 5; ++i) {
            canvas->translate(kCell / 2.f, kCell / 2.f);
            canvas->rotate(static_cast<float>(i) * 10.f + 5.f);
            canvas->clipRRect(RRect::MakeRectXY(Rect::MakeXYWH(-70, -70, 140, 140), 12, 12), true);
            canvas->resetMatrix();
            canvas->translate(static_cast<float>(col * kCell), static_cast<float>(row * kCell));
          }
          canvas->drawRect(Rect::MakeWH(kCell, kCell), fillPaint);
          break;
        case 10: {
          // Cell 10: Duplicate non-rect Path clip. Two separately constructed Paths with
          // identical geometry bypass isSame() elimination, forcing makeClipTexture to
          // compose them via inverse-fill + DstOut.
          canvas->translate(kCell / 2.f, kCell / 2.f);
          auto pathA = MakePath({{0, -75}, {72, -23}, {44, 60}, {-44, 60}, {-72, -23}});
          auto pathB = MakePath({{0, -75}, {72, -23}, {44, 60}, {-44, 60}, {-72, -23}});
          canvas->clipPath(pathA);
          canvas->clipPath(pathB);
          canvas->translate(-kCell / 2.f, -kCell / 2.f);
          canvas->drawRect(Rect::MakeWH(kCell, kCell), fillPaint);
          break;
        }
        case 11: {
          // Cell 11: Inverse-fill clip whose geometry covers the current clip bounds. Combined
          // with the cell-range clipRect, the keep-region collapses to empty and the drawRect
          // is fully suppressed. Verifies the fast-path that retires the clip stack to Empty
          // when an inverse-fill shape fully contains the current clip bounds.
          canvas->clipRect(Rect::MakeWH(kCell, kCell));
          Path inverseFill = {};
          inverseFill.addRect(Rect::MakeLTRB(-10, -10, kCell + 10, kCell + 10));
          inverseFill.toggleInverseFillType();
          canvas->clipPath(inverseFill);
          canvas->drawRect(Rect::MakeWH(kCell, kCell), fillPaint);
          break;
        }

        case 12: {
          // Cell 12: Outer RRect clip combined with an inverse-fill rect clip whose shape sits
          // inside the RRect. The keep-region is the RRect interior minus the inverse shape,
          // producing a rounded outer frame with a rectangular cutout in the middle.
          canvas->clipRRect(RRect::MakeRectXY(Rect::MakeXYWH(20, 20, 160, 160), 30, 30));
          Path inverseHole = {};
          inverseHole.addRect(Rect::MakeXYWH(60, 60, 80, 80));
          inverseHole.toggleInverseFillType();
          canvas->clipPath(inverseHole);
          canvas->drawRect(Rect::MakeWH(kCell, kCell), fillPaint);
          break;
        }

        // ---------- State degeneracy ----------
        case 13: {
          // Cell 13: Verifies that ClipStack updates its uniqueID when transitioning to the
          // Empty state. Without that update, the second drawRect can be incorrectly batched
          // with the first one's (Empty) clip via stale-uniqueID comparison and get fully
          // suppressed, even though after restore the clip is back to wide-open.
          canvas->save();
          Path path = {};
          path.addRect(Rect::MakeXYWH(500, 500, 50, 50));
          canvas->clipPath(path);
          canvas->drawRect(Rect::MakeXYWH(20, 20, 80, 80), fillPaint);
          canvas->restore();
          canvas->drawRect(Rect::MakeXYWH(50, 50, 100, 100), fillPaint);
          break;
        }
      }
      canvas->restore();
    }
  }
  EXPECT_TRUE(Baseline::Compare(surface, "ClipTest/ClipOverview"));
}

}  // namespace tgfx

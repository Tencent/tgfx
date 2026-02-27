/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "tgfx/core/Canvas.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(RecordingTest, BasicFlushAndSubmit) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto surface = Surface::Make(context, 100, 100);
  ASSERT_TRUE(surface != nullptr);

  auto canvas = surface->getCanvas();
  Paint paint;
  paint.setColor(Color::Red());
  canvas->drawRect(Rect::MakeWH(50, 50), paint);

  auto recording = context->flush();
  ASSERT_TRUE(recording != nullptr);

  context->submit(std::move(recording));
  EXPECT_TRUE(Baseline::Compare(surface, "RecordingTest/BasicFlushAndSubmit"));
}

TGFX_TEST(RecordingTest, MultipleRecordingsInOrder) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto surface = Surface::Make(context, 100, 100);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  Paint paint1;
  paint1.setColor(Color::Red());
  canvas->drawRect(Rect::MakeWH(50, 50), paint1);
  auto recording1 = context->flush();
  ASSERT_TRUE(recording1 != nullptr);

  Paint paint2;
  paint2.setColor(Color::Blue());
  canvas->drawRect(Rect::MakeXYWH(25, 25, 50, 50), paint2);
  auto recording2 = context->flush();
  ASSERT_TRUE(recording2 != nullptr);

  Paint paint3;
  paint3.setColor(Color::Green());
  canvas->drawRect(Rect::MakeXYWH(50, 50, 50, 50), paint3);
  auto recording3 = context->flush();
  ASSERT_TRUE(recording3 != nullptr);

  context->submit(std::move(recording1));
  context->submit(std::move(recording2));
  context->submit(std::move(recording3));
  EXPECT_TRUE(Baseline::Compare(surface, "RecordingTest/MultipleRecordingsInOrder"));
}

TGFX_TEST(RecordingTest, OutOfOrderSubmission) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto surface = Surface::Make(context, 100, 100);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  Paint paint1;
  paint1.setColor(Color::Red());
  canvas->drawRect(Rect::MakeWH(50, 50), paint1);
  auto recording1 = context->flush();
  ASSERT_TRUE(recording1 != nullptr);

  Paint paint2;
  paint2.setColor(Color::Blue());
  canvas->drawRect(Rect::MakeXYWH(25, 25, 50, 50), paint2);
  auto recording2 = context->flush();
  ASSERT_TRUE(recording2 != nullptr);

  Paint paint3;
  paint3.setColor(Color::Green());
  canvas->drawRect(Rect::MakeXYWH(50, 50, 50, 50), paint3);
  auto recording3 = context->flush();
  ASSERT_TRUE(recording3 != nullptr);

  context->submit(std::move(recording3));
  context->submit(std::move(recording1));
  context->submit(std::move(recording2));
  EXPECT_TRUE(Baseline::Compare(surface, "RecordingTest/OutOfOrderSubmission"));
}

TGFX_TEST(RecordingTest, EmptyFlush) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto recording = context->flush();
  ASSERT_TRUE(recording == nullptr);
}

TGFX_TEST(RecordingTest, FlushAndSubmitWithSync) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto surface = Surface::Make(context, 100, 100);
  ASSERT_TRUE(surface != nullptr);

  auto canvas = surface->getCanvas();
  Paint paint;
  paint.setColor(Color::Red());
  canvas->drawRect(Rect::MakeWH(50, 50), paint);

  auto recording = context->flush();
  ASSERT_TRUE(recording != nullptr);

  context->submit(std::move(recording), true);
  EXPECT_TRUE(Baseline::Compare(surface, "RecordingTest/FlushAndSubmitWithSync"));
}

TGFX_TEST(RecordingTest, FlushAndSubmitHelper) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto surface = Surface::Make(context, 100, 100);
  ASSERT_TRUE(surface != nullptr);

  auto canvas = surface->getCanvas();
  Paint paint;
  paint.setColor(Color::Red());
  canvas->drawRect(Rect::MakeWH(50, 50), paint);

  bool flushed = context->flushAndSubmit();
  ASSERT_TRUE(flushed);

  flushed = context->flushAndSubmit();
  ASSERT_FALSE(flushed);
  EXPECT_TRUE(Baseline::Compare(surface, "RecordingTest/FlushAndSubmitHelper"));
}

TGFX_TEST(RecordingTest, MultipleDrawingBuffers) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto surface1 = Surface::Make(context, 100, 100);
  ASSERT_TRUE(surface1 != nullptr);
  auto canvas1 = surface1->getCanvas();

  auto surface2 = Surface::Make(context, 100, 100);
  ASSERT_TRUE(surface2 != nullptr);
  auto canvas2 = surface2->getCanvas();

  Paint paint1;
  paint1.setColor(Color::Red());
  canvas1->drawRect(Rect::MakeWH(50, 50), paint1);

  Paint paint2;
  paint2.setColor(Color::Blue());
  canvas2->drawRect(Rect::MakeWH(50, 50), paint2);

  auto recording1 = context->flush();
  ASSERT_TRUE(recording1 != nullptr);

  Paint paint3;
  paint3.setColor(Color::Green());
  canvas1->drawRect(Rect::MakeXYWH(25, 25, 50, 50), paint3);

  auto recording2 = context->flush();
  ASSERT_TRUE(recording2 != nullptr);

  context->submit(std::move(recording1));
  context->submit(std::move(recording2));
  EXPECT_TRUE(Baseline::Compare(surface1, "RecordingTest/MultipleDrawingBuffers_surface1"));
  EXPECT_TRUE(Baseline::Compare(surface2, "RecordingTest/MultipleDrawingBuffers_surface2"));
}

TGFX_TEST(RecordingTest, RecordingWithSemaphore) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto surface = Surface::Make(context, 100, 100);
  ASSERT_TRUE(surface != nullptr);

  auto canvas = surface->getCanvas();
  Paint paint;
  paint.setColor(Color::Red());
  canvas->drawRect(Rect::MakeWH(50, 50), paint);

  BackendSemaphore signalSemaphore;
  auto recording = context->flush(&signalSemaphore);
  ASSERT_TRUE(recording != nullptr);

  context->submit(std::move(recording));
  EXPECT_TRUE(Baseline::Compare(surface, "RecordingTest/RecordingWithSemaphore"));
}

}  // namespace tgfx

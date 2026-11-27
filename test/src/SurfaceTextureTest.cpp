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

#if defined(__ANDROID__) && !defined(TGFX_USE_VULKAN) && defined(TGFX_TEST_ACCESS_PRIVATE)
#include <chrono>
#include <limits>
#include "base/TGFXTest.h"
#include "gpu/resources/TextureView.h"
#include "platform/android/SurfaceTexture.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/opengl/GLDevice.h"
#include "tgfx/platform/ImageReader.h"
#include "tgfx/platform/android/JNIEnvironment.h"

namespace tgfx {
namespace {
// The test APK must include test/android/RetrySurfaceTexture.java and initialize tgfx JNI.
static constexpr int WIDTH = 32;
static constexpr int HEIGHT = 32;

class SurfaceTextureTest : public TGFXTest {
 protected:
  void SetUp() override {
    TGFXTest::SetUp();
    env = environment.current();
    ASSERT_NE(env, nullptr);
    device = GLDevice::Make();
    ASSERT_NE(device, nullptr);
    context = device->lockContext();
    ASSERT_NE(context, nullptr);
    auto sourceClass = env->FindClass("org/tgfx/test/RetrySurfaceTexture");
    ASSERT_NE(sourceClass, nullptr);
    auto constructor = env->GetMethodID(sourceClass, "<init>", "()V");
    ASSERT_NE(constructor, nullptr);
    source = env->NewObject(sourceClass, constructor);
    ASSERT_NE(source, nullptr);
    ASSERT_FALSE(env->ExceptionCheck());
    auto setSize = env->GetMethodID(sourceClass, "setDefaultBufferSize", "(II)V");
    ASSERT_NE(setSize, nullptr);
    env->CallVoidMethod(source, setSize, WIDTH, HEIGHT);
    ASSERT_FALSE(env->ExceptionCheck());
    failuresRemaining = env->GetFieldID(sourceClass, "failuresRemaining", "I");
    ASSERT_NE(failuresRemaining, nullptr);
    latchCalls = env->GetFieldID(sourceClass, "latchCalls", "I");
    ASSERT_NE(latchCalls, nullptr);
    timestampFailuresRemaining = env->GetFieldID(sourceClass, "timestampFailuresRemaining", "I");
    ASSERT_NE(timestampFailuresRemaining, nullptr);
    fixedTimestamp = env->GetFieldID(sourceClass, "fixedTimestamp", "J");
    ASSERT_NE(fixedTimestamp, nullptr);
    timestampStep = env->GetFieldID(sourceClass, "timestampStep", "J");
    ASSERT_NE(timestampStep, nullptr);
    env->DeleteLocalRef(sourceClass);
    stream = std::shared_ptr<SurfaceTexture>(new SurfaceTexture(WIDTH, HEIGHT, env, source));
    reader = ImageReader::MakeFrom(stream);
    ASSERT_NE(reader, nullptr);
    input = stream->createInputSurface();
    ASSERT_NE(input, nullptr);
    ASSERT_FALSE(env->ExceptionCheck());
  }

  void TearDown() override {
    if (env != nullptr) {
      env->ExceptionClear();
      if (input != nullptr) {
        auto surfaceClass = env->GetObjectClass(input);
        auto release = env->GetMethodID(surfaceClass, "release", "()V");
        env->CallVoidMethod(input, release);
        env->DeleteLocalRef(surfaceClass);
        env->DeleteLocalRef(input);
        input = nullptr;
      }
    }
    reader = nullptr;
    stream = nullptr;
    if (env != nullptr && source != nullptr) {
      env->DeleteLocalRef(source);
      source = nullptr;
    }
    if (context != nullptr) {
      context->flushAndSubmit(true);
      device->unlock();
      context = nullptr;
    }
    device = nullptr;
    TGFXTest::TearDown();
  }

  void postColor(jint color) {
    auto surfaceClass = env->GetObjectClass(input);
    auto lockCanvas = env->GetMethodID(surfaceClass, "lockCanvas",
                                       "(Landroid/graphics/Rect;)Landroid/graphics/Canvas;");
    ASSERT_NE(lockCanvas, nullptr);
    auto canvas = env->CallObjectMethod(input, lockCanvas, nullptr);
    ASSERT_NE(canvas, nullptr);
    ASSERT_FALSE(env->ExceptionCheck());
    auto canvasClass = env->GetObjectClass(canvas);
    auto drawColor = env->GetMethodID(canvasClass, "drawColor", "(I)V");
    ASSERT_NE(drawColor, nullptr);
    env->CallVoidMethod(canvas, drawColor, color);
    ASSERT_FALSE(env->ExceptionCheck());
    auto unlock =
        env->GetMethodID(surfaceClass, "unlockCanvasAndPost", "(Landroid/graphics/Canvas;)V");
    ASSERT_NE(unlock, nullptr);
    env->CallVoidMethod(input, unlock, canvas);
    ASSERT_FALSE(env->ExceptionCheck());
    env->DeleteLocalRef(canvasClass);
    env->DeleteLocalRef(canvas);
    env->DeleteLocalRef(surfaceClass);
    stream->notifyFrameAvailable();
  }

  void expectColor(const std::shared_ptr<ImageBuffer>& buffer, uint8_t red, uint8_t green) {
    auto surface = Surface::Make(context, WIDTH, HEIGHT);
    ASSERT_NE(surface, nullptr);
    surface->getCanvas()->drawImage(Image::MakeFrom(buffer));
    uint8_t pixels[WIDTH * HEIGHT * 4] = {};
    auto info = ImageInfo::Make(WIDTH, HEIGHT, ColorType::RGBA_8888, AlphaType::Premultiplied);
    ASSERT_TRUE(surface->readPixels(info, pixels));
    auto offset = ((HEIGHT / 2) * WIDTH + WIDTH / 2) * 4;
    EXPECT_EQ(pixels[offset], red);
    EXPECT_EQ(pixels[offset + 1], green);
    EXPECT_EQ(pixels[offset + 2], 0);
    EXPECT_EQ(pixels[offset + 3], 255);
  }

  static int64_t ElapsedMilliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 start)
        .count();
  }

  JNIEnvironment environment = {};
  JNIEnv* env = nullptr;
  std::shared_ptr<GLDevice> device = nullptr;
  Context* context = nullptr;
  std::shared_ptr<SurfaceTexture> stream = nullptr;
  std::shared_ptr<ImageReader> reader = nullptr;
  jobject source = nullptr;
  jobject input = nullptr;
  jfieldID failuresRemaining = nullptr;
  jfieldID latchCalls = nullptr;
  jfieldID timestampFailuresRemaining = nullptr;
  jfieldID fixedTimestamp = nullptr;
  jfieldID timestampStep = nullptr;
};
}  // namespace

TEST_F(SurfaceTextureTest, FirstDeliveryRetriesJniLatchFailureWithoutNotification) {
  postColor(static_cast<jint>(0xFF00FF00u));
  ASSERT_FALSE(HasFatalFailure());
  env->SetIntField(source, failuresRemaining, 1);
  auto failed = reader->acquireNextBuffer();
  ASSERT_NE(failed, nullptr);
  EXPECT_EQ(TextureView::MakeFrom(context, failed), nullptr);
  EXPECT_EQ(failed->status(), ImageBufferStatus::Failed);
  EXPECT_FALSE(failed->expired());
  EXPECT_TRUE(stream->frameAvailable);
  EXPECT_FALSE(stream->latchedValid);
  EXPECT_EQ(stream->latchedTimestamp, -1);
  EXPECT_EQ(env->GetIntField(source, latchCalls), 1);
  EXPECT_FALSE(env->ExceptionCheck());

  auto retry = reader->acquireNextBuffer();
  ASSERT_NE(TextureView::MakeFrom(context, retry), nullptr);
  EXPECT_EQ(retry->status(), ImageBufferStatus::Ready);
  EXPECT_TRUE(failed->expired());
  EXPECT_FALSE(stream->frameAvailable);
  EXPECT_TRUE(stream->latchedValid);
  EXPECT_EQ(env->GetIntField(source, latchCalls), 2);
  expectColor(retry, 0, 255);
}

TEST_F(SurfaceTextureTest, TimestampFailureCannotReuseOldTimestampOrConsumedNotification) {
  postColor(static_cast<jint>(0xFFFF0000u));
  ASSERT_FALSE(HasFatalFailure());
  auto first = reader->acquireNextBuffer();
  ASSERT_NE(TextureView::MakeFrom(context, first), nullptr);
  auto firstTimestamp = stream->latchedTimestamp;
  expectColor(first, 255, 0);
  ASSERT_FALSE(HasFatalFailure());

  postColor(static_cast<jint>(0xFF00FF00u));
  ASSERT_FALSE(HasFatalFailure());
  env->SetIntField(source, timestampFailuresRemaining, 1);
  auto failed = reader->acquireNextBuffer(firstTimestamp);
  EXPECT_EQ(TextureView::MakeFrom(context, failed), nullptr);
  EXPECT_EQ(failed->status(), ImageBufferStatus::Failed);
  EXPECT_FALSE(failed->expired());
  EXPECT_TRUE(first->expired());
  EXPECT_FALSE(stream->frameAvailable);
  EXPECT_FALSE(stream->latchedValid);
  EXPECT_EQ(stream->latchedTimestamp, firstTimestamp);
  EXPECT_EQ(env->GetIntField(source, latchCalls), 2);
  EXPECT_FALSE(env->ExceptionCheck());

  auto sameTimestamp = reader->acquireNextBuffer(firstTimestamp);
  EXPECT_EQ(TextureView::MakeFrom(context, sameTimestamp), nullptr);
  EXPECT_EQ(sameTimestamp->status(), ImageBufferStatus::Timeout);
  EXPECT_EQ(env->GetIntField(source, latchCalls), 2);
  EXPECT_FALSE(stream->frameAvailable);

  postColor(static_cast<jint>(0xFFFFFF00u));
  ASSERT_FALSE(HasFatalFailure());
  auto recovered = reader->acquireNextBuffer();
  ASSERT_NE(TextureView::MakeFrom(context, recovered), nullptr);
  EXPECT_EQ(recovered->status(), ImageBufferStatus::Ready);
  EXPECT_TRUE(stream->latchedValid);
  EXPECT_EQ(env->GetIntField(source, latchCalls), 3);
  expectColor(recovered, 255, 255);
}

TEST_F(SurfaceTextureTest, NoNotificationWaitsOneSecondAndKeepsFallbackStatusTimeout) {
  auto empty = reader->acquireNextBuffer(0);
  auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(TextureView::MakeFrom(context, empty), nullptr);
  auto elapsed = ElapsedMilliseconds(start);
  EXPECT_GE(elapsed, 900);
  EXPECT_LT(elapsed, 3000);
  EXPECT_EQ(empty->status(), ImageBufferStatus::Timeout);
  EXPECT_FALSE(empty->expired());
  EXPECT_EQ(env->GetIntField(source, latchCalls), 0);
  EXPECT_FALSE(stream->latchedValid);

  postColor(static_cast<jint>(0xFFFF0000u));
  ASSERT_FALSE(HasFatalFailure());
  auto first = reader->acquireNextBuffer();
  auto texture = TextureView::MakeFrom(context, first);
  ASSERT_NE(texture, nullptr);
  expectColor(first, 255, 0);
  ASSERT_FALSE(HasFatalFailure());

  auto timeout = reader->acquireNextBuffer();
  start = std::chrono::steady_clock::now();
  EXPECT_EQ(TextureView::MakeFrom(context, timeout), texture);
  elapsed = ElapsedMilliseconds(start);
  EXPECT_GE(elapsed, 900);
  EXPECT_LT(elapsed, 3000);
  EXPECT_EQ(timeout->status(), ImageBufferStatus::Timeout);
  EXPECT_EQ(first->status(), ImageBufferStatus::Ready);
  EXPECT_EQ(env->GetIntField(source, latchCalls), 1);
  expectColor(first, 255, 0);
  EXPECT_EQ(timeout->status(), ImageBufferStatus::Timeout);

  env->SetLongField(source, fixedTimestamp, 0);
  env->SetLongField(source, timestampStep, 1);
  postColor(static_cast<jint>(0xFFFF0000u));
  ASSERT_FALSE(HasFatalFailure());
  auto target = reader->acquireNextBuffer(std::numeric_limits<int64_t>::max());
  start = std::chrono::steady_clock::now();
  ASSERT_NE(TextureView::MakeFrom(context, target), nullptr);
  elapsed = ElapsedMilliseconds(start);
  EXPECT_GE(elapsed, 900);
  EXPECT_LT(elapsed, 3000);
  EXPECT_EQ(target->status(), ImageBufferStatus::Timeout);
  EXPECT_GT(env->GetIntField(source, latchCalls), 2);
  EXPECT_TRUE(stream->latchedValid);
  EXPECT_FALSE(env->ExceptionCheck());
}

TEST_F(SurfaceTextureTest, EqualTimestampNotificationStillLatchesNewPixels) {
  env->SetLongField(source, fixedTimestamp, 0);
  postColor(static_cast<jint>(0xFFFF0000u));
  ASSERT_FALSE(HasFatalFailure());
  auto first = reader->acquireNextBuffer(0);
  ASSERT_NE(TextureView::MakeFrom(context, first), nullptr);
  EXPECT_EQ(first->status(), ImageBufferStatus::Ready);
  expectColor(first, 255, 0);
  ASSERT_FALSE(HasFatalFailure());
  EXPECT_EQ(env->GetIntField(source, latchCalls), 1);

  auto unchanged = reader->acquireNextBuffer(0);
  ASSERT_NE(TextureView::MakeFrom(context, unchanged), nullptr);
  EXPECT_EQ(unchanged->status(), ImageBufferStatus::Ready);
  EXPECT_EQ(env->GetIntField(source, latchCalls), 1);

  postColor(static_cast<jint>(0xFF00FF00u));
  ASSERT_FALSE(HasFatalFailure());
  auto second = reader->acquireNextBuffer(0);
  ASSERT_NE(TextureView::MakeFrom(context, second), nullptr);
  EXPECT_EQ(second->status(), ImageBufferStatus::Ready);
  EXPECT_TRUE(first->expired());
  EXPECT_TRUE(unchanged->expired());
  EXPECT_EQ(env->GetIntField(source, latchCalls), 2);
  expectColor(second, 0, 255);
}

TEST_F(SurfaceTextureTest, TargetMissReturnsQuicklyButNotificationCanSeekBackward) {
  env->SetLongField(source, fixedTimestamp, 100);
  postColor(static_cast<jint>(0xFFFF0000u));
  ASSERT_FALSE(HasFatalFailure());
  auto missed = reader->acquireNextBuffer(90);
  auto start = std::chrono::steady_clock::now();
  ASSERT_NE(TextureView::MakeFrom(context, missed), nullptr);
  EXPECT_LT(ElapsedMilliseconds(start), 500);
  EXPECT_EQ(missed->status(), ImageBufferStatus::Timeout);
  EXPECT_TRUE(stream->latchedValid);
  EXPECT_EQ(stream->latchedTimestamp, 100);
  EXPECT_EQ(env->GetIntField(source, latchCalls), 1);

  auto older = reader->acquireNextBuffer(80);
  start = std::chrono::steady_clock::now();
  ASSERT_NE(TextureView::MakeFrom(context, older), nullptr);
  EXPECT_LT(ElapsedMilliseconds(start), 500);
  EXPECT_EQ(older->status(), ImageBufferStatus::Timeout);
  EXPECT_EQ(env->GetIntField(source, latchCalls), 1);

  env->SetLongField(source, fixedTimestamp, 80);
  postColor(static_cast<jint>(0xFF00FF00u));
  ASSERT_FALSE(HasFatalFailure());
  auto seek = reader->acquireNextBuffer(80);
  ASSERT_NE(TextureView::MakeFrom(context, seek), nullptr);
  EXPECT_EQ(seek->status(), ImageBufferStatus::Ready);
  EXPECT_EQ(stream->latchedTimestamp, 80);
  EXPECT_EQ(env->GetIntField(source, latchCalls), 2);
  expectColor(seek, 0, 255);
}

}  // namespace tgfx
#endif

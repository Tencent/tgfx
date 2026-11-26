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
// The Android test APK must include test/android/RetrySurfaceTexture.java and initialize JNI.
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
    latchCalls = env->GetFieldID(sourceClass, "latchCalls", "I");
    timestampFailuresRemaining = env->GetFieldID(sourceClass, "timestampFailuresRemaining", "I");
    ASSERT_NE(timestampFailuresRemaining, nullptr);
    getTimestamp = env->GetMethodID(sourceClass, "getTimestamp", "()J");
    ASSERT_NE(failuresRemaining, nullptr);
    ASSERT_NE(latchCalls, nullptr);
    ASSERT_NE(getTimestamp, nullptr);
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
      }
    }
    reader = nullptr;
    stream = nullptr;
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
  jmethodID getTimestamp = nullptr;
};
}  // namespace

TEST_F(SurfaceTextureTest, FirstDeliveryCanRecoverAfterJniLatchFailure) {
  postColor(static_cast<jint>(0xFF00FF00u));
  ASSERT_FALSE(HasFatalFailure());
  env->SetIntField(source, failuresRemaining, 1);
  auto failed = reader->acquireBuffer();
  EXPECT_EQ(TextureView::MakeFrom(context, failed), nullptr);
  EXPECT_EQ(failed->state(), ImageBufferState::Expired);
  EXPECT_EQ(failed->timestamp(), -1);
  EXPECT_TRUE(stream->frameAvailable);
  EXPECT_EQ(stream->lastLatchedTimestamp, -1);
  EXPECT_EQ(env->GetIntField(source, latchCalls), 1);
  EXPECT_FALSE(env->ExceptionCheck());

  auto buffer = reader->acquireBuffer();
  ASSERT_NE(TextureView::MakeFrom(context, buffer), nullptr);
  EXPECT_EQ(buffer->state(), ImageBufferState::Ready);
  EXPECT_EQ(buffer->timestamp(), env->CallLongMethod(source, getTimestamp));
  EXPECT_FALSE(stream->frameAvailable);
  EXPECT_EQ(env->GetIntField(source, latchCalls), 2);
  EXPECT_EQ(failed->state(), ImageBufferState::Expired);
  expectColor(buffer, 0, 255);
}

TEST_F(SurfaceTextureTest, NewCredentialRetriesFailedLatchWithoutAnotherNotification) {
  postColor(static_cast<jint>(0xFFFF0000u));
  ASSERT_FALSE(HasFatalFailure());
  auto first = reader->acquireBuffer();
  auto texture = TextureView::MakeFrom(context, first);
  ASSERT_NE(texture, nullptr);
  EXPECT_EQ(first->state(), ImageBufferState::Ready);
  auto firstTimestamp = first->timestamp();
  expectColor(first, 255, 0);
  ASSERT_FALSE(HasFatalFailure());

  postColor(static_cast<jint>(0xFF00FF00u));
  ASSERT_FALSE(HasFatalFailure());
  env->SetIntField(source, failuresRemaining, 2);
  ASSERT_FALSE(env->ExceptionCheck());
  std::shared_ptr<ImageBuffer> failed = nullptr;
  for (int count = 0; count < 2; count++) {
    failed = reader->acquireBuffer();
    EXPECT_EQ(TextureView::MakeFrom(context, failed), nullptr);
    EXPECT_EQ(failed->state(), ImageBufferState::Expired);
    EXPECT_EQ(failed->timestamp(), -1);
    EXPECT_TRUE(stream->frameAvailable);
    EXPECT_EQ(stream->lastLatchedTimestamp, firstTimestamp);
    EXPECT_FALSE(env->ExceptionCheck());
  }
  EXPECT_TRUE(first->expired());
  EXPECT_EQ(first->timestamp(), firstTimestamp);

  EXPECT_EQ(env->GetIntField(source, latchCalls), 3);
  auto retry = reader->acquireBuffer();
  ASSERT_NE(TextureView::MakeFrom(context, retry), nullptr);
  EXPECT_EQ(retry->state(), ImageBufferState::Ready);
  EXPECT_EQ(retry->timestamp(), env->CallLongMethod(source, getTimestamp));
  EXPECT_GT(retry->timestamp(), firstTimestamp);
  EXPECT_FALSE(stream->frameAvailable);
  EXPECT_EQ(env->GetIntField(source, latchCalls), 4);
  EXPECT_EQ(failed->state(), ImageBufferState::Expired);
  EXPECT_EQ(TextureView::MakeFrom(context, failed), nullptr);
  expectColor(retry, 0, 255);
  ASSERT_FALSE(HasFatalFailure());

  auto empty = reader->acquireBuffer();
  EXPECT_EQ(TextureView::MakeFrom(context, empty), nullptr);
  EXPECT_EQ(empty->state(), ImageBufferState::Expired);
  EXPECT_FALSE(retry->expired());
}

TEST_F(SurfaceTextureTest, TimestampFailureDoesNotRestoreConsumedNotification) {
  postColor(static_cast<jint>(0xFFFF0000u));
  ASSERT_FALSE(HasFatalFailure());
  auto first = reader->acquireBuffer();
  ASSERT_NE(TextureView::MakeFrom(context, first), nullptr);
  auto firstTimestamp = first->timestamp();
  expectColor(first, 255, 0);
  ASSERT_FALSE(HasFatalFailure());

  postColor(static_cast<jint>(0xFF00FF00u));
  ASSERT_FALSE(HasFatalFailure());
  env->SetIntField(source, timestampFailuresRemaining, 1);
  auto failed = reader->acquireBuffer();
  EXPECT_EQ(TextureView::MakeFrom(context, failed), nullptr);
  EXPECT_EQ(failed->state(), ImageBufferState::Expired);
  EXPECT_EQ(failed->timestamp(), -1);
  EXPECT_FALSE(stream->frameAvailable);
  EXPECT_EQ(stream->lastLatchedTimestamp, firstTimestamp);
  EXPECT_TRUE(first->expired());
  EXPECT_EQ(env->GetIntField(source, latchCalls), 2);
  EXPECT_FALSE(env->ExceptionCheck());

  auto empty = reader->acquireBuffer();
  EXPECT_EQ(TextureView::MakeFrom(context, empty), nullptr);
  EXPECT_EQ(empty->state(), ImageBufferState::Expired);
  EXPECT_EQ(env->GetIntField(source, latchCalls), 2);

  postColor(static_cast<jint>(0xFFFFFF00u));
  ASSERT_FALSE(HasFatalFailure());
  auto next = reader->acquireBuffer();
  ASSERT_NE(TextureView::MakeFrom(context, next), nullptr);
  EXPECT_EQ(next->state(), ImageBufferState::Ready);
  EXPECT_EQ(next->timestamp(), env->CallLongMethod(source, getTimestamp));
  EXPECT_GT(next->timestamp(), firstTimestamp);
  EXPECT_EQ(env->GetIntField(source, latchCalls), 3);
  EXPECT_EQ(failed->state(), ImageBufferState::Expired);
  expectColor(next, 255, 255);
}
}  // namespace tgfx
#endif

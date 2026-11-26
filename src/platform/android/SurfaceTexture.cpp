/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "platform/android/SurfaceTexture.h"
#include <chrono>
#include "HandlerThread.h"
#include "JNIUtil.h"
#include "core/utils/ColorSpaceHelper.h"
#include "core/utils/Log.h"
#include "gpu/opengl/GLFunctions.h"
#include "gpu/opengl/GLGPU.h"
#include "gpu/opengl/GLTexture.h"
#include "gpu/resources/DefaultTextureView.h"

namespace tgfx {
static Global<jclass> SurfaceTextureClass;
static jmethodID SurfaceTexture_Constructor_singleBufferMode;
static jmethodID SurfaceTexture_Constructor_texName;
static jmethodID SurfaceTexture_setOnFrameAvailableListenerHandler;
static jmethodID SurfaceTexture_setOnFrameAvailableListener;
static jfieldID SurfaceTexture_mEventHandler;
static jmethodID SurfaceTexture_updateTexImage;
static jmethodID SurfaceTexture_attachToGLContext;
static jmethodID SurfaceTexture_detachFromGLContext;
static jmethodID SurfaceTexture_release;
static jmethodID SurfaceTexture_getDataSpace;
static jmethodID SurfaceTexture_getTransformMatrix;
static jmethodID SurfaceTexture_getTimestamp;
static Global<jclass> SurfaceClass;
static jmethodID Surface_Constructor;
static Global<jclass> HandlerClass;
static jmethodID Handler_Constructor;
static Global<jclass> EventHandlerClass;
static jmethodID EventHandler_Constructor;
static Global<jclass> DataSpaceClass;
static jmethodID DataSpaceClass_getStandard;
static jmethodID DataSpaceClass_getTransfer;

void SurfaceTexture::JNIInit(JNIEnv* env) {
  SurfaceTextureClass = env->FindClass("android/graphics/SurfaceTexture");
  SurfaceTexture_Constructor_singleBufferMode =
      env->GetMethodID(SurfaceTextureClass.get(), "<init>", "(Z)V");
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    SurfaceTexture_Constructor_texName =
        env->GetMethodID(SurfaceTextureClass.get(), "<init>", "(I)V");
  }
  SurfaceTexture_setOnFrameAvailableListenerHandler = env->GetMethodID(
      SurfaceTextureClass.get(), "setOnFrameAvailableListener",
      "(Landroid/graphics/SurfaceTexture$OnFrameAvailableListener;Landroid/os/Handler;)V");
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    SurfaceTexture_setOnFrameAvailableListener =
        env->GetMethodID(SurfaceTextureClass.get(), "setOnFrameAvailableListener",
                         "(Landroid/graphics/SurfaceTexture$OnFrameAvailableListener;)V");
    SurfaceTexture_mEventHandler =
        env->GetFieldID(SurfaceTextureClass.get(), "mEventHandler",
                        "Landroid/graphics/SurfaceTexture$EventHandler;");
    EventHandlerClass = env->FindClass("android/graphics/SurfaceTexture$EventHandler");
    EventHandler_Constructor =
        env->GetMethodID(EventHandlerClass.get(), "<init>",
                         "(Landroid/graphics/SurfaceTexture;Landroid/os/Looper;)V");
  } else {
    HandlerClass = env->FindClass("android/os/Handler");
    Handler_Constructor = env->GetMethodID(HandlerClass.get(), "<init>", "(Landroid/os/Looper;)V");
  }
  SurfaceTexture_updateTexImage =
      env->GetMethodID(SurfaceTextureClass.get(), "updateTexImage", "()V");
  SurfaceTexture_attachToGLContext =
      env->GetMethodID(SurfaceTextureClass.get(), "attachToGLContext", "(I)V");
  SurfaceTexture_detachFromGLContext =
      env->GetMethodID(SurfaceTextureClass.get(), "detachFromGLContext", "()V");
  SurfaceTexture_release = env->GetMethodID(SurfaceTextureClass.get(), "release", "()V");
  SurfaceTexture_getTimestamp = env->GetMethodID(SurfaceTextureClass.get(), "getTimestamp", "()J");
  SurfaceTexture_getDataSpace = env->GetMethodID(SurfaceTextureClass.get(), "getDataSpace", "()I");
  if (SurfaceTexture_getDataSpace == nullptr) {
    env->ExceptionClear();
  }
  SurfaceTexture_getTransformMatrix =
      env->GetMethodID(SurfaceTextureClass.get(), "getTransformMatrix", "([F)V");
  if (SurfaceTexture_getTransformMatrix == nullptr) {
    env->ExceptionClear();
  }
  SurfaceClass = env->FindClass("android/view/Surface");
  Surface_Constructor =
      env->GetMethodID(SurfaceClass.get(), "<init>", "(Landroid/graphics/SurfaceTexture;)V");
  DataSpaceClass = env->FindClass("android/hardware/DataSpace");
  if (DataSpaceClass.get() != nullptr) {
    DataSpaceClass_getStandard =
        env->GetStaticMethodID(DataSpaceClass.get(), "getStandard", "(I)I");
    DataSpaceClass_getTransfer =
        env->GetStaticMethodID(DataSpaceClass.get(), "getTransfer", "(I)I");
  } else {
    env->ExceptionClear();
  }
}

static std::mutex threadLocker = {};
static std::shared_ptr<HandlerThread> handlerThread;

static jobject GetGlobalLooper() {
  std::lock_guard<std::mutex> autoLock(threadLocker);
  if (handlerThread == nullptr) {
    handlerThread = HandlerThread::Make();
  }
  if (handlerThread == nullptr) {
    return nullptr;
  }
  return handlerThread->getLooper();
}

static void SetOnFrameAvailableListener(JNIEnv* env, jobject surfaceTexture, jobject listener,
                                        jobject looper) {
  if (SurfaceTexture_setOnFrameAvailableListenerHandler != nullptr) {
    auto handler = env->NewObject(HandlerClass.get(), Handler_Constructor, looper);
    env->CallVoidMethod(surfaceTexture, SurfaceTexture_setOnFrameAvailableListenerHandler, listener,
                        handler);
  } else {
    env->CallVoidMethod(surfaceTexture, SurfaceTexture_setOnFrameAvailableListener, listener);
    auto handler =
        env->NewObject(EventHandlerClass.get(), EventHandler_Constructor, surfaceTexture, looper);
    env->SetObjectField(surfaceTexture, SurfaceTexture_mEventHandler, handler);
  }
}

std::shared_ptr<SurfaceTexture> SurfaceTexture::Make(int width, int height, jobject listener) {
  if (width <= 0 || height <= 0 || listener == nullptr) {
    return nullptr;
  }
  JNIEnvironment environment;
  auto env = environment.current();
  if (env == nullptr) {
    return nullptr;
  }
  jobject stObject;
  if (SurfaceTexture_Constructor_singleBufferMode != nullptr) {
    stObject = env->NewObject(SurfaceTextureClass.get(),
                              SurfaceTexture_Constructor_singleBufferMode, JNI_FALSE);
  } else {
    stObject = env->NewObject(SurfaceTextureClass.get(), SurfaceTexture_Constructor_texName, 0);
    if (stObject != nullptr) {
      env->CallVoidMethod(stObject, SurfaceTexture_detachFromGLContext);
    }
  }
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    LOGE("SurfaceImageReader::MakeFrom(): failed to create a new SurfaceTexture!");
    return nullptr;
  }
  auto looper = GetGlobalLooper();
  if (looper == nullptr) {
    return nullptr;
  }
  SetOnFrameAvailableListener(env, stObject, listener, looper);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    LOGE("SurfaceImageReader::MakeFrom(): failed to set the OnFrameAvailableListener!");
    return nullptr;
  }
  return std::shared_ptr<SurfaceTexture>(new SurfaceTexture(width, height, env, stObject));
}

SurfaceTexture::SurfaceTexture(int width, int height, JNIEnv*, jobject st)
    : ImageStream(width, height) {
  surfaceTexture = st;
}

SurfaceTexture::~SurfaceTexture() {
  JNIEnvironment environment;
  auto env = environment.current();
  if (env == nullptr) {
    return;
  }
  env->CallVoidMethod(surfaceTexture.get(), SurfaceTexture_release);
}

jobject SurfaceTexture::createInputSurface() const {
  // Use JNIEnvironment::Current() to fetch the thread-local JNIEnv without pushing a new local
  // frame. Wrapping this in a stack JNIEnvironment would trigger PopLocalFrame(nullptr) on scope
  // exit and release the local ref returned by NewObject before the caller can use it. The
  // returned local ref instead lives in the caller's current local frame.
  auto env = JNIEnvironment::Current();
  if (env == nullptr) {
    return nullptr;
  }
  return env->NewObject(SurfaceClass.get(), Surface_Constructor, surfaceTexture.get());
}

void SurfaceTexture::notifyFrameAvailable() {
  // Note: If there is a pending frame available already, SurfaceTexture will not dispatch any new
  // frame-available event until you have called the SurfaceTexture.updateTexImage().
  std::lock_guard<std::mutex> autoLock(locker);
  frameAvailable = true;
  condition.notify_all();
}

static ISize ComputeTextureSize(const float matrix[16], int width, int height) {
  // SurfaceTexture.getTransformMatrix() returns a column-major 4x4 that maps normalized texture
  // coordinates to the region the decoder actually produced inside its (possibly padded) buffer.
  // We only recover the padded buffer size when that matrix is a pure crop plus vertical flip, the
  // form the targeted decoders emit: matrix[0] > 0, matrix[5] < 0, and the off-diagonal terms are
  // zero. Rotation, horizontal flip, or a missing vertical flip are rejected by the guard below.
  // The scale and translation terms encode cropSize / bufferSize (the translation cancels the
  // half-texel filtering shrink), so dividing the content size by them recovers the buffer size;
  // this matches the real device matrices this fix was validated against. The recovery is only
  // guaranteed for a crop anchored at the buffer origin. A crop offset from the origin recovers a
  // size larger than the content but still wrong (for example content 720 inside a 768 buffer with
  // an 8-pixel left crop recovers 751), and the final check below only rejects results smaller than
  // the content, so it cannot catch that case. Non-origin crops are a known, unhandled limitation.
  if (matrix[1] != 0 || matrix[4] != 0 || matrix[0] <= 0 || matrix[5] >= 0) {
    return {};
  }
  auto denomX = matrix[0] + matrix[12] * 2;
  auto scaleY = -matrix[5];
  auto denomY = scaleY + (matrix[13] - scaleY) * 2;
  if (denomX <= 0 || denomY <= 0) {
    return {};
  }
  Size size = {static_cast<float>(width) / denomX, static_cast<float>(height) / denomY};
  auto result = size.toRound();
  // A valid padded buffer is never smaller than the content it carries, so a smaller result means
  // the matrix was not the expected crop form; fall back instead of trusting it.
  if (result.width < width || result.height < height) {
    return {};
  }
  return result;
}

ImageStream::ReadResult SurfaceTexture::onReadTexture(Context* context, bool,
                                                      std::shared_ptr<TextureView> textureView,
                                                      int64_t timestamp) {
  if (textureView != nullptr) {
    auto frame = updateTexImage(timestamp);
    return {std::move(textureView), frame.status, frame.latched};
  }
  auto textureID = makeExternalOESTexture(context);
  if (textureID == 0) {
    return {};
  }
  auto frame = updateTexImage(timestamp);
  if (!frame.latched || frame.status == ImageBufferStatus::Failed) {
    releaseExternalOESTexture(context, textureID);
    return {nullptr, frame.status, false};
  }
  // The descriptor must match the real size of the OES texture, otherwise getTextureCoord()
  // converts pixel coordinates with a wrong base: hardware decoders pad the output buffer to an
  // aligned size (e.g. 720x1560 -> 720x1568), and every pixel->UV conversion that relies on it
  // would be scaled by contentSize / bufferSize. That shifted the whole video and, for RGBAAA
  // sequences whose alpha plane is packed next to the color plane, displaced alpha against color
  // and exposed the black background of the video as an outline around the content. The padding
  // rows are kept out of the sampling by the subset clamp that needSubset() enables for external
  // textures.
  auto descriptorWidth = textureSize.isEmpty() ? width() : textureSize.width;
  auto descriptorHeight = textureSize.isEmpty() ? height() : textureSize.height;
  TextureDescriptor descriptor = {descriptorWidth,
                                  descriptorHeight,
                                  PixelFormat::RGBA_8888,
                                  false,
                                  1,
                                  TextureUsage::TEXTURE_BINDING};
  auto gpu = static_cast<GLGPU*>(context->gpu());
  auto texture = gpu->makeResource<GLTexture>(
      descriptor, static_cast<unsigned>(GL_TEXTURE_EXTERNAL_OES), textureID);
  textureView = Resource::AddToCache(context, new DefaultTextureView(std::move(texture)));
  return {std::move(textureView), frame.status, true};
}

unsigned SurfaceTexture::makeExternalOESTexture(Context* context) {
  std::lock_guard<std::mutex> autoLock(locker);
  JNIEnvironment environment;
  auto env = environment.current();
  if (env == nullptr) {
    return 0;
  }
  auto gl = static_cast<GLGPU*>(context->gpu())->functions();
  unsigned textureID = 0;
  gl->genTextures(1, &textureID);
  if (textureID == 0) {
    return 0;
  }
  env->CallVoidMethod(surfaceTexture.get(), SurfaceTexture_attachToGLContext, textureID);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    gl->deleteTextures(1, &textureID);
    LOGE("NativeImageReader::makeTexture(): failed to attached to a SurfaceTexture!");
    return 0;
  }
  return textureID;
}

void SurfaceTexture::releaseExternalOESTexture(Context* context, unsigned textureID) {
  std::lock_guard<std::mutex> autoLock(locker);
  JNIEnvironment environment;
  auto env = environment.current();
  if (env != nullptr) {
    // Detaching also deletes the texture, and allows attaching again later.
    env->CallVoidMethod(surfaceTexture.get(), SurfaceTexture_detachFromGLContext);
    if (!env->ExceptionCheck()) {
      return;
    }
    env->ExceptionClear();
  }
  auto gl = static_cast<GLGPU*>(context->gpu())->functions();
  gl->deleteTextures(1, &textureID);
}

SurfaceTexture::FrameResult SurfaceTexture::updateTexImage(int64_t timestamp) {
  static const auto TIMEOUT = std::chrono::seconds(1);
  auto deadline = std::chrono::steady_clock::now() + TIMEOUT;
  std::unique_lock<std::mutex> autoLock(locker);
  JNIEnvironment environment;
  auto env = environment.current();
  if (env == nullptr) {
    return {};
  }
  FrameResult result = {};
  if (timestamp >= 0 && latchedTimestamp == timestamp) {
    result.status = ImageBufferStatus::Ready;
    return result;
  }
  while (true) {
    if (!frameAvailable) {
      if (condition.wait_until(autoLock, deadline) == std::cv_status::timeout && !frameAvailable) {
        LOGE("SurfaceTexture::updateTexImage(): timeout when waiting for the frame available!");
        result.status = ImageBufferStatus::Timeout;
        return result;
      }
      continue;
    }
    frameAvailable = false;
    auto previousTimestamp = latchedTimestamp;
    result.latched = true;
    if (!latchTexImage(env)) {
      // No new notification arrives until the queued frame is latched.
      frameAvailable = true;
      result.status = ImageBufferStatus::Failed;
      return result;
    }
    if (timestamp < 0 || latchedTimestamp == timestamp) {
      result.status = ImageBufferStatus::Ready;
      return result;
    }
    if (latchedTimestamp != previousTimestamp) {
      // Queued frames may share one notification, so latch again before waiting.
      frameAvailable = true;
    }
  }
}

bool SurfaceTexture::latchTexImage(JNIEnv* env) {
  env->CallVoidMethod(surfaceTexture.get(), SurfaceTexture_updateTexImage);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    LOGE("SurfaceTexture::latchTexImage(): failed to updateTexImage!");
    return false;
  }
  latchedTimestamp =
      static_cast<int64_t>(env->CallLongMethod(surfaceTexture.get(), SurfaceTexture_getTimestamp));
  if (SurfaceTexture_getDataSpace) {
    jint dataSpace = env->CallIntMethod(surfaceTexture.get(), SurfaceTexture_getDataSpace);
    jint standard =
        env->CallStaticIntMethod(DataSpaceClass.get(), DataSpaceClass_getStandard, dataSpace);
    jint transfer =
        env->CallStaticIntMethod(DataSpaceClass.get(), DataSpaceClass_getTransfer, dataSpace);
    _colorSpace = AndroidDataSpaceToColorSpace(standard, transfer);
  }
  if (SurfaceTexture_getTransformMatrix != nullptr && !textureSizeResolved) {
    auto floatArray = env->NewFloatArray(16);
    if (floatArray == nullptr) {
      env->ExceptionClear();
    } else {
      env->CallVoidMethod(surfaceTexture.get(), SurfaceTexture_getTransformMatrix, floatArray);
      if (env->ExceptionCheck()) {
        env->ExceptionClear();
      } else {
        auto matrix = env->GetFloatArrayElements(floatArray, nullptr);
        if (matrix != nullptr) {
          // The content size is fixed for the lifetime of a SurfaceTexture, so the buffer padding
          // never changes after the first frame; resolve the texture size once and reuse it.
          textureSize = ComputeTextureSize(matrix, width(), height());
          textureSizeResolved = true;
          env->ReleaseFloatArrayElements(floatArray, matrix, JNI_ABORT);
        } else {
          env->ExceptionClear();
        }
      }
      env->DeleteLocalRef(floatArray);
    }
  }
  return true;
}

}  // namespace tgfx

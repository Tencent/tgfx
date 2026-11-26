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
    LOGE("SurfaceTexture::Make(): failed to create a new SurfaceTexture!");
    return nullptr;
  }
  auto looper = GetGlobalLooper();
  if (looper == nullptr) {
    return nullptr;
  }
  SetOnFrameAvailableListener(env, stObject, listener, looper);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    LOGE("SurfaceTexture::Make(): failed to set the OnFrameAvailableListener!");
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
  // Notifications can be coalesced. This flag records availability, not a queued frame count.
  std::lock_guard<std::mutex> autoLock(locker);
  frameAvailable = true;
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

ImageStream::TextureReadResult SurfaceTexture::onReadTexture(
    Context* context, bool, std::shared_ptr<TextureView> textureView) {
  unsigned textureID = 0;
  if (textureView == nullptr) {
    {
      std::lock_guard<std::mutex> lock(locker);
      if (!frameAvailable) {
        return {nullptr, false, lastLatchedTimestamp, false, false};
      }
    }
    textureID = makeExternalOESTexture(context);
    if (textureID == 0) {
      return {};
    }
  }
  auto result = updateTexImage();
  if (textureView != nullptr) {
    return {std::move(textureView), result.advanced, result.timestamp, result.contentChanged,
            result.failed};
  }
  if (!result.advanced) {
    releaseExternalOESTexture(context, textureID);
    return {nullptr, false, -1, result.contentChanged, result.failed};
  }
  // The descriptor must match the real size of the OES texture, otherwise getTextureCoord()
  // converts pixel coordinates with a wrong base: hardware decoders pad the output buffer to an
  // aligned size (e.g. 720x1560 -> 720x1568), and every pixel->UV conversion that relies on it
  // would be scaled by contentSize / bufferSize. That shifted the whole video and, for RGBAAA
  // sequences whose alpha plane is packed next to the color plane, displaced alpha against color
  // and exposed the black background of the video as an outline around the content. The padding
  // rows are kept out of the sampling by the subset clamp that needSubset() enables for external
  // textures. If size recovery is unavailable or rejected, the descriptor uses the requested
  // content size; padded sampling is not guaranteed in that fallback.
  auto descriptorWidth = result.textureSize.isEmpty() ? width() : result.textureSize.width;
  auto descriptorHeight = result.textureSize.isEmpty() ? height() : result.textureSize.height;
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
  return {std::move(textureView), result.advanced, result.timestamp, result.contentChanged,
          result.failed};
}

bool SurfaceTexture::latchTexture(JNIEnv* env) {
  env->CallVoidMethod(surfaceTexture.get(), SurfaceTexture_updateTexImage);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    LOGE("SurfaceTexture::latchTexture(): failed to updateTexImage!");
    return false;
  }
  if (SurfaceTexture_getDataSpace) {
    jint dataSpace = env->CallIntMethod(surfaceTexture.get(), SurfaceTexture_getDataSpace);
    jint standard =
        env->CallStaticIntMethod(DataSpaceClass.get(), DataSpaceClass_getStandard, dataSpace);
    jint transfer =
        env->CallStaticIntMethod(DataSpaceClass.get(), DataSpaceClass_getTransfer, dataSpace);
    _colorSpace = AndroidDataSpaceToColorSpace(standard, transfer);
  }
  return true;
}

int64_t SurfaceTexture::currentTimestamp(JNIEnv* env) {
  return static_cast<int64_t>(
      env->CallLongMethod(surfaceTexture.get(), SurfaceTexture_getTimestamp));
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
    LOGE("SurfaceTexture::makeExternalOESTexture(): failed to attach to a SurfaceTexture!");
    return 0;
  }
  return textureID;
}

void SurfaceTexture::releaseExternalOESTexture(Context* context, unsigned textureID) {
  JNIEnvironment environment;
  auto env = environment.current();
  if (env != nullptr) {
    env->CallVoidMethod(surfaceTexture.get(), SurfaceTexture_detachFromGLContext);
    if (!env->ExceptionCheck()) {
      // Successful detach deletes the associated GL texture; do not delete its name a second time.
      // Requires the owning GL context to be current. See detachFromGLContext() in:
      // https://developer.android.com/reference/android/graphics/SurfaceTexture
      return;
    }
    env->ExceptionClear();
  }
  auto gl = static_cast<GLGPU*>(context->gpu())->functions();
  gl->deleteTextures(1, &textureID);
}

SurfaceTexture::FrameUpdateResult SurfaceTexture::updateTexImage() {
  std::unique_lock<std::mutex> lock(locker);
  // No notification is not proof of an empty native queue, but no JNI call is proof this read
  // leaves its content unchanged. A late notification is handled by a later acquisition.
  if (!frameAvailable) {
    return {false, lastLatchedTimestamp, textureSize, false, false};
  }
  JNIEnvironment environment;
  auto env = environment.current();
  if (env == nullptr) {
    return {false, lastLatchedTimestamp, textureSize, false};
  }
  frameAvailable = false;
  auto previous = lastLatchedTimestamp;
  if (!latchTexture(env)) {
    // A failed latch may leave an image pending; another notification is not guaranteed.
    // Preserve retry eligibility, not a guarantee that the queue or previous pixels are unchanged.
    frameAvailable = true;
    return {false, previous, textureSize, true};
  }
  auto timestamp = currentTimestamp(env);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return {false, previous, textureSize, true, true};
  }
  lastLatchedTimestamp = timestamp;
  resolveTextureSize(env);
  // Every actual latch can replace pixels, even when its timestamp equals the previous one.
  return {true, timestamp, textureSize, true, false};
}

void SurfaceTexture::resolveTextureSize(JNIEnv* env) {
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
          // This source assumes fixed content dimensions and padding. Cache the first retrieved
          // matrix's size estimate, including an empty result that uses the content-size fallback.
          // Later transform changes are not re-evaluated.
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
}

}  // namespace tgfx

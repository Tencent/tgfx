/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/AlphaType.h"
#if defined(__ANDROID__) || defined(ANDROID) || defined(__OHOS__)

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include "EGLHardwareTexture.h"
#include "gpu/Gpu.h"
#include "opengl/GLSampler.h"
#include "tgfx/opengl/egl/EGLDevice.h"
#include "utils/PixelFormatUtil.h"
#include "utils/UniqueID.h"
#if defined(__OHOS__)
#include <native_buffer/native_buffer.h>
#include <native_window/external_window.h>
#else
#include <android/hardware_buffer.h>
#endif

namespace tgfx {

typedef EGLClientBuffer(EGLAPIENTRYP PFNEGLGETNATIVECLIENTBUFFERPROC)(HardwareBufferRef buffer);

namespace eglext {
static PFNEGLGETNATIVECLIENTBUFFERPROC eglGetNativeClientBuffer = nullptr;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = nullptr;
static PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = nullptr;
static PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = nullptr;
}  // namespace eglext

static bool InitEGLEXTProc() {
#if defined(__OHOS__)
#define EGL_NATIVE_BUFFER_TARGET EGL_NATIVE_BUFFER_OHOS
  eglext::eglGetNativeClientBuffer =
      (PFNEGLGETNATIVECLIENTBUFFERPROC)OH_NativeWindow_CreateNativeWindowBufferFromNativeBuffer;
#else
#define EGL_NATIVE_BUFFER_TARGET EGL_NATIVE_BUFFER_ANDROID
  eglext::eglGetNativeClientBuffer =
      (PFNEGLGETNATIVECLIENTBUFFERPROC)eglGetProcAddress("eglGetNativeClientBufferANDROID");
#endif
  eglext::glEGLImageTargetTexture2DOES =
      (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
  eglext::eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
  eglext::eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
  return eglext::eglGetNativeClientBuffer && eglext::glEGLImageTargetTexture2DOES &&
         eglext::eglCreateImageKHR && eglext::eglDestroyImageKHR;
}

std::shared_ptr<EGLHardwareTexture> EGLHardwareTexture::MakeFrom(Context* context,
                                                                 HardwareBufferRef hardwareBuffer) {
  static const bool initialized = InitEGLEXTProc();
  if (!initialized || hardwareBuffer == nullptr) {
    return nullptr;
  }
  unsigned target = GL_TEXTURE_2D;
  auto format = PixelFormat::RGBA_8888;
  int width, height;
  auto info = HardwareBufferGetInfo(hardwareBuffer);
  if (!info.isEmpty()) {
    format = ColorTypeToPixelFormat(info.colorType());
    width = info.width();
    height = info.height();
  } else {
#if defined(__OHOS__)
    OH_NativeBuffer_Config config;
    OH_NativeBuffer_GetConfig(hardwareBuffer, &config);
    if (config.format < NATIVEBUFFER_PIXEL_FMT_YUV_422_I ||
        config.format > NATIVEBUFFER_PIXEL_FMT_YCRCB_P010) {
      return nullptr;
    }
    target = GL_TEXTURE_EXTERNAL_OES;
    width = config.width;
    height = config.height;
#else
    return nullptr;
#endif
  }
  auto scratchKey = ComputeScratchKey(hardwareBuffer);
  auto glTexture = Resource::Find<EGLHardwareTexture>(context, scratchKey);
  if (glTexture != nullptr) {
    return glTexture;
  }
  auto clientBuffer = eglext::eglGetNativeClientBuffer(hardwareBuffer);
  if (!clientBuffer) {
    return nullptr;
  }
  auto display = static_cast<EGLDevice*>(context->device())->getDisplay();
  EGLint attributes[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
  EGLImageKHR eglImage = eglext::eglCreateImageKHR(
      display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_TARGET, clientBuffer, attributes);
  if (eglImage == EGL_NO_IMAGE_KHR) {
    return nullptr;
  }

  auto sampler = std::make_unique<GLSampler>();
  sampler->target = target;
  sampler->format = format;
  glGenTextures(1, &sampler->id);
  if (sampler->id == 0) {
    eglext::eglDestroyImageKHR(display, eglImage);
    return nullptr;
  }
  glBindTexture(sampler->target, sampler->id);
  glTexParameteri(sampler->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(sampler->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(sampler->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(sampler->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  eglext::glEGLImageTargetTexture2DOES(sampler->target, (GLeglImageOES)eglImage);
  auto eglHardwareTexture = new EGLHardwareTexture(hardwareBuffer, eglImage, width, height);
  glTexture = Resource::AddToCache(context, eglHardwareTexture, scratchKey);
  glTexture->sampler = std::move(sampler);
  return glTexture;
}

EGLHardwareTexture::EGLHardwareTexture(HardwareBufferRef hardwareBuffer, EGLImageKHR eglImage,
                                       int width, int height)
    : Texture(width, height, ImageOrigin::TopLeft),
      hardwareBuffer(HardwareBufferRetain(hardwareBuffer)), eglImage(eglImage) {
}

EGLHardwareTexture::~EGLHardwareTexture() {
  HardwareBufferRelease(hardwareBuffer);
}

ScratchKey EGLHardwareTexture::ComputeScratchKey(void* hardwareBuffer) {
  static const uint32_t ResourceType = UniqueID::Next();
  BytesKey bytesKey(3);
  bytesKey.write(ResourceType);
  bytesKey.write(hardwareBuffer);
  return bytesKey;
}

size_t EGLHardwareTexture::memoryUsage() const {
  auto info = HardwareBufferGetInfo(hardwareBuffer);
  return info.byteSize();
}

void EGLHardwareTexture::onReleaseGPU() {
  context->gpu()->deleteSampler(sampler.get());
  auto display = static_cast<EGLDevice*>(context->device())->getDisplay();
  eglext::eglDestroyImageKHR(display, eglImage);
}
}  // namespace tgfx

#endif
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

#include "tgfx/core/AlphaType.h"
#if defined(__ANDROID__) || defined(ANDROID) || defined(__OHOS__)

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include "EGLHardwareTextureSampler.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/Gpu.h"
#include "tgfx/gpu/opengl/egl/EGLDevice.h"
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

std::unique_ptr<EGLHardwareTextureSampler> EGLHardwareTextureSampler::MakeFrom(
    Context* context, HardwareBufferRef hardwareBuffer) {
  static const bool initialized = InitEGLEXTProc();
  if (!initialized || hardwareBuffer == nullptr) {
    return nullptr;
  }
  unsigned target = GL_TEXTURE_2D;
  auto format = PixelFormat::RGBA_8888;
  auto info = HardwareBufferGetInfo(hardwareBuffer);
  if (!info.isEmpty()) {
    format = ColorTypeToPixelFormat(info.colorType());
  } else {
#if defined(__OHOS__)
    OH_NativeBuffer_Config config;
    OH_NativeBuffer_GetConfig(hardwareBuffer, &config);
    if (config.format < NATIVEBUFFER_PIXEL_FMT_YUV_422_I ||
        config.format > NATIVEBUFFER_PIXEL_FMT_YCRCB_P010) {
      return nullptr;
    }
    target = GL_TEXTURE_EXTERNAL_OES;
#else
    return nullptr;
#endif
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

  unsigned samplerID = 0;
  glGenTextures(1, &samplerID);
  if (samplerID == 0) {
    eglext::eglDestroyImageKHR(display, eglImage);
    return nullptr;
  }
  glBindTexture(target, samplerID);
  glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  eglext::glEGLImageTargetTexture2DOES(target, (GLeglImageOES)eglImage);
  return std::unique_ptr<EGLHardwareTextureSampler>(
      new EGLHardwareTextureSampler(hardwareBuffer, eglImage, samplerID, target, format));
}

EGLHardwareTextureSampler::EGLHardwareTextureSampler(HardwareBufferRef hardwareBuffer,
                                                     EGLImageKHR eglImage, unsigned id,
                                                     unsigned target, PixelFormat format)
    : GLTextureSampler(id, target, format), hardwareBuffer(hardwareBuffer), eglImage(eglImage) {
  HardwareBufferRetain(hardwareBuffer);
}

EGLHardwareTextureSampler::~EGLHardwareTextureSampler() {
  HardwareBufferRelease(hardwareBuffer);
}

void EGLHardwareTextureSampler::releaseGPU(Context* context) {
  GLTextureSampler::releaseGPU(context);
  auto display = static_cast<EGLDevice*>(context->device())->getDisplay();
  eglext::eglDestroyImageKHR(display, eglImage);
}
}  // namespace tgfx

#endif
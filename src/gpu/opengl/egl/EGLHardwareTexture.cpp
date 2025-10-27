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

#if defined(__ANDROID__) || defined(ANDROID) || defined(__OHOS__)
#include "EGLHardwareTexture.h"
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include "core/utils/PixelFormatUtil.h"
#include "gpu/GPU.h"
#include "tgfx/gpu/PixelFormat.h"
#include "tgfx/gpu/opengl/egl/EGLDevice.h"
#include "tgfx/platform/HardwareBuffer.h"
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

std::shared_ptr<EGLHardwareTexture> EGLHardwareTexture::MakeFrom(EGLGPU* gpu,
                                                                 HardwareBufferRef hardwareBuffer,
                                                                 uint32_t usage) {
  static const bool initialized = InitEGLEXTProc();
  if (!initialized) {
    return nullptr;
  }
  auto info = HardwareBufferGetInfo(hardwareBuffer);
  auto format = PixelFormat::Unknown;
  bool isYUV = false;
  switch (info.format) {
    case HardwareBufferFormat::ALPHA_8:
      format = PixelFormat::ALPHA_8;
      break;
    case HardwareBufferFormat::RGBA_8888:
      format = PixelFormat::RGBA_8888;
      break;
    case HardwareBufferFormat::YCBCR_420_SP:
      isYUV = true;
      format = PixelFormat::RGBA_8888;
      break;
    default:
      break;
  }
  if (format == PixelFormat::Unknown) {
    return nullptr;
  }
  if (usage & TextureUsage::RENDER_ATTACHMENT && (isYUV || !gpu->isFormatRenderable(format))) {
    return nullptr;
  }
  unsigned target = isYUV ? GL_TEXTURE_EXTERNAL_OES : GL_TEXTURE_2D;
  auto clientBuffer = eglext::eglGetNativeClientBuffer(hardwareBuffer);
  if (!clientBuffer) {
    return nullptr;
  }
  auto display = gpu->getDisplay();
  EGLint attributes[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
  EGLImageKHR eglImage = eglext::eglCreateImageKHR(
      display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_TARGET, clientBuffer, attributes);
  if (eglImage == EGL_NO_IMAGE_KHR) {
    return nullptr;
  }

  unsigned textureID = 0;
  glGenTextures(1, &textureID);
  if (textureID == 0) {
    eglext::eglDestroyImageKHR(display, eglImage);
    return nullptr;
  }
  TextureDescriptor descriptor = {info.width, info.height, format, false, 1, usage};
  auto texture = gpu->makeResource<EGLHardwareTexture>(descriptor, hardwareBuffer, eglImage, target,
                                                       textureID);
  auto state = gpu->state();
  state->bindTexture(texture.get());
  eglext::glEGLImageTargetTexture2DOES(target, (GLeglImageOES)eglImage);
  if (usage & TextureUsage::RENDER_ATTACHMENT && !texture->checkFrameBuffer(gpu)) {
    return nullptr;
  }
  return texture;
}

EGLHardwareTexture::EGLHardwareTexture(const TextureDescriptor& descriptor,
                                       HardwareBufferRef hardwareBuffer, EGLImageKHR eglImage,
                                       unsigned target, unsigned textureID)
    : GLTexture(descriptor, target, textureID), hardwareBuffer(hardwareBuffer), eglImage(eglImage) {
  HardwareBufferRetain(hardwareBuffer);
}

EGLHardwareTexture::~EGLHardwareTexture() {
  HardwareBufferRelease(hardwareBuffer);
}

void EGLHardwareTexture::onReleaseTexture(GLGPU* gpu) {
  GLTexture::onReleaseTexture(gpu);
  auto display = static_cast<EGLGPU*>(gpu)->getDisplay();
  eglext::eglDestroyImageKHR(display, eglImage);
}
}  // namespace tgfx

#endif
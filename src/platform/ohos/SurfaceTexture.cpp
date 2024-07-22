/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "SurfaceTexture.h"
#include <GLES2/gl2ext.h>
#include <chrono>
#include "GLExternalOESTexture.h"
#include "opengl/GLSampler.h"
#include "tgfx/opengl/GLDefines.h"
#include "utils/Log.h"

namespace tgfx {
static std::mutex threadLocker = {};
using GetPlatformDisplayExt = PFNEGLGETPLATFORMDISPLAYEXTPROC;

bool CheckEglExtension(const char *extensions, const char *extension) {
    size_t extlen = strlen(extension);
    const char *end = extensions + strlen(extensions);

    while (extensions < end) {
        size_t n = 0;
        if (*extensions == ' ') {
            extensions++;
            continue;
        }
        n = strcspn(extensions, " ");
        if (n == extlen && strncmp(extension, extensions, n) == 0) {
            return true;
        }
        extensions += n;
    }
    return false;
}

// 获取当前的显示设备
static EGLDisplay GetPlatformEglDisplay(EGLenum platform, void *native_display, const EGLint *attrib_list) {
    static GetPlatformDisplayExt eglGetPlatformDisplayExt = NULL;

    if (!eglGetPlatformDisplayExt) {
        const char *extensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
        if (extensions && (CheckEglExtension(extensions, "EGL_EXT_platform_wayland") ||
                           CheckEglExtension(extensions, "EGL_KHR_platform_wayland"))) {
            eglGetPlatformDisplayExt = (GetPlatformDisplayExt)eglGetProcAddress("eglGetPlatformDisplayEXT");
        }
    }

    if (eglGetPlatformDisplayExt) {
        return eglGetPlatformDisplayExt(platform, native_display, attrib_list);
    }

    return eglGetDisplay((EGLNativeDisplayType)native_display);
}

void InitEGLEnv() {
    // 获取当前的显示设备
    EGLDisplay eglDisplay_ = GetPlatformEglDisplay(EGL_PLATFORM_OHOS_KHR, EGL_DEFAULT_DISPLAY, NULL);
    if (eglDisplay_ == EGL_NO_DISPLAY) {
    }

    EGLint major, minor;
    // 初始化EGLDisplay
    if (eglInitialize(eglDisplay_, &major, &minor) == EGL_FALSE) {
    }

    // 绑定图形绘制的API为OpenGLES
    if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
    }

    unsigned int ret;
    EGLint count;
    EGLint config_attribs[] = {EGL_SURFACE_TYPE,
                               EGL_WINDOW_BIT,
                               EGL_RED_SIZE,
                               8,
                               EGL_GREEN_SIZE,
                               8,
                               EGL_BLUE_SIZE,
                               8,
                               EGL_ALPHA_SIZE,
                               8,
                               EGL_RENDERABLE_TYPE,
                               EGL_OPENGL_ES3_BIT,
                               EGL_NONE};

    // 获取一个有效的系统配置信息
    EGLConfig config_;
    ret = eglChooseConfig(eglDisplay_, config_attribs, &config_, 1, &count);
    if (!(ret && static_cast<unsigned int>(count) >= 1)) {
    }

    static const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

    // 创建上下文
    EGLContext eglContext_ = eglCreateContext(eglDisplay_, config_, EGL_NO_CONTEXT, context_attribs);
    if (eglContext_ == EGL_NO_CONTEXT) {
    }

    // 创建eglSurface
    OHNativeWindow *eglNativeWindow_ = nullptr;
    EGLSurface eglSurface_ = eglCreateWindowSurface(eglDisplay_, config_, eglNativeWindow_, context_attribs);
    if (eglSurface_ == EGL_NO_SURFACE) {
    }

    // 关联上下文
    eglMakeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_);

}


void OnFrameAvailable(void *context) {
    reinterpret_cast<SurfaceTexture*>(context)->notifyFrameAvailable();
}
std::shared_ptr<SurfaceTexture> SurfaceTexture::Make(int width, int height) {
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  InitEGLEnv();
  GLuint textureId;
  glGenTextures(1, &textureId);  
  OH_NativeImage* nativeImage = OH_NativeImage_Create(0, GL_TEXTURE_EXTERNAL_OES);
  if (nativeImage == nullptr) {
     return nullptr;
  }  
  int ret = OH_NativeImage_DetachContext(nativeImage);
  if (ret != 0) {
     OH_NativeImage_Destroy(&nativeImage);
     return nullptr;
  }  
  OHNativeWindow* nativeWindow = OH_NativeImage_AcquireNativeWindow(nativeImage);  
  if (nativeWindow == nullptr) {
     OH_NativeImage_Destroy(&nativeImage);
     return nullptr;
  }  
  auto surfaceTexture = new SurfaceTexture(width, height, nativeImage, nativeWindow);   
  OH_OnFrameAvailableListener listener;
  listener.context = surfaceTexture;
  listener.onFrameAvailable = OnFrameAvailable; 
  ret = OH_NativeImage_SetOnFrameAvailableListener(nativeImage, listener);
  if (ret != 0) {
      delete surfaceTexture;
      return nullptr;   
  }  
  ret = OH_NativeWindow_NativeWindowHandleOpt(nativeWindow, SET_BUFFER_GEOMETRY, width, height);
  if (ret != 0) {
      delete surfaceTexture;
      return nullptr;     
  }  
  return std::shared_ptr<SurfaceTexture>(surfaceTexture);
}

SurfaceTexture::SurfaceTexture(int width, int height, OH_NativeImage* nativeImage, NativeWindow* nativeWindow)
    : _width(width), _height(height), _nativeImage(nativeImage), _nativeWindow(nativeWindow) {
}

SurfaceTexture::~SurfaceTexture() {
   OH_NativeImage_UnsetOnFrameAvailableListener(_nativeImage);
   OH_NativeImage_DetachContext(_nativeImage);  
   OH_NativeWindow_DestroyNativeWindow(_nativeWindow);
   OH_NativeImage_Destroy(&_nativeImage);
}

NativeWindow* SurfaceTexture::getInputSurface() const {
  return _nativeWindow;
}

void SurfaceTexture::notifyFrameAvailable() {
  // Note: If there is a pending frame available already, SurfaceTexture will not dispatch any new
  // frame-available event until you have called the SurfaceTexture.updateTexImage().
  std::lock_guard<std::mutex> autoLock(locker);
  frameAvailable = true;
  condition.notify_all();
}

static ISize ComputeTextureSize(float matrix[16], int width, int height) {
  Size size = {static_cast<float>(width), static_cast<float>(height)};
  auto scaleX = fabsf(matrix[0]);
  if (scaleX > 0) {
    size.width = size.width / (scaleX + matrix[12] * 2);
  }
  float scaleY = fabsf(matrix[5]);
  if (scaleY > 0) {
    size.height = size.height / (scaleY + (matrix[13] - scaleY) * 2);
  }
  return size.toRound();
}

std::shared_ptr<Texture> SurfaceTexture::onMakeTexture(Context* context, bool) {
  auto texture = makeTexture(context);
  if (texture != nullptr) {
    onUpdateTexture(texture, Rect::MakeWH(_width, _height));
  }
  return texture;
}

bool SurfaceTexture::onUpdateTexture(std::shared_ptr<Texture> texture, const Rect&) {
  std::unique_lock<std::mutex> autoLock(locker);
  if (!frameAvailable) {
    static const auto TIMEOUT = std::chrono::seconds(1);
    auto status = condition.wait_for(autoLock, TIMEOUT);
    if (status == std::cv_status::timeout) {
      LOGE("NativeImageReader::onUpdateTexture(): timeout when waiting for the frame available!");
      return false;
    }
  }
  frameAvailable = false;
  int ret = OH_NativeImage_UpdateSurfaceImage(_nativeImage);
  if (ret != 0) {
    LOGE("NativeImageReader::onUpdateTexture(): failed to updateTexImage!");
    return false;
  }
  float matrix[16];
  ret = OH_NativeImage_GetTransformMatrix(_nativeImage, matrix);
  if (ret != 0) {
    LOGE("NativeImageReader::onUpdateTexture(): failed to getTransformMatrix!");
    return false;
  }  
  auto textureSize = ComputeTextureSize(matrix, width(), height());
  std::static_pointer_cast<GLExternalOESTexture>(texture)->updateTextureSize(textureSize.width,
                                                                             textureSize.height);
  return true;
}

std::shared_ptr<Texture> SurfaceTexture::makeTexture(Context* context) {
  std::lock_guard<std::mutex> autoLock(locker);
  auto texture = GLExternalOESTexture::Make(context, width(), height());
  if (texture == nullptr) {
    return nullptr;
  }
  auto sampler = static_cast<const GLSampler*>(texture->getSampler());
  OH_NativeImage_AttachContext(_nativeImage, sampler->id);
  if (OH_NativeImage_AttachContext(_nativeImage, sampler->id) != 0) {
    texture = nullptr;
    LOGE("NativeImageReader::makeTexture(): failed to attached to a SurfaceTexture!");
    return nullptr;
  }
  return texture;
}
}  // namespace tgfx

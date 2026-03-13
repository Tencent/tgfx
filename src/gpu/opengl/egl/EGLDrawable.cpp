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

#include "tgfx/gpu/opengl/egl/EGLDrawable.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
// X11/Xlib.h defines macros (e.g. None, Status) that conflict with C++ identifiers.
#undef None
#undef Status
#include "gpu/opengl/GLDefines.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {
EGLDrawable::EGLDrawable(void* display, void* surface, int width, int height,
                         std::shared_ptr<ColorSpace> colorSpace)
    : Drawable(width, height, std::move(colorSpace)), eglDisplay(display), eglSurface(surface) {
}

void EGLDrawable::setPresentationTime(int64_t time) {
  presentationTime = time;
}

std::shared_ptr<RenderTargetProxy> EGLDrawable::onCreateProxy(Context* context) {
  GLFrameBufferInfo frameBuffer = {};
  frameBuffer.id = 0;
  frameBuffer.format = GL_RGBA8;
  BackendRenderTarget backendRT(frameBuffer, width(), height());
  return RenderTargetProxy::MakeFrom(context, backendRT, ImageOrigin::BottomLeft);
}

void EGLDrawable::onPresent(Context*, std::shared_ptr<CommandBuffer>) {
  auto display = static_cast<EGLDisplay>(eglDisplay);
  auto surface = static_cast<EGLSurface>(eglSurface);
  if (presentationTime.has_value()) {
    static auto eglPresentationTimeANDROID = reinterpret_cast<PFNEGLPRESENTATIONTIMEANDROIDPROC>(
        eglGetProcAddress("eglPresentationTimeANDROID"));
    if (eglPresentationTimeANDROID) {
      // egl uses nano seconds
      eglPresentationTimeANDROID(display, surface, *presentationTime * 1000);
      presentationTime = std::nullopt;
    }
  }
  eglSwapBuffers(display, surface);
}
}  // namespace tgfx

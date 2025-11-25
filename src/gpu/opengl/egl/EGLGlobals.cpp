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

#include "tgfx/gpu/opengl/egl/EGLGlobals.h"
#include <EGL/eglext.h>
#include <atomic>
#include <cstring>
#if defined(_WIN32)
#include "EGLDisplayWrapper.h"
#endif

namespace tgfx {
EGLGlobals InitializeEGL() {
  EGLGlobals globals = {};
  EGLint majorVersion;
  EGLint minorVersion;
#if defined(_WIN32)
  do {
    globals.display = EGLDisplayWrapper::EGLGetPlatformDisplay();
  } while (eglInitialize(globals.display, &majorVersion, &minorVersion) == EGL_FALSE &&
           EGLDisplayWrapper::HasNext());
  globals.windowSurfaceAttributes = {EGL_DIRECT_COMPOSITION_ANGLE, EGL_TRUE, EGL_NONE};
#else
  globals.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  eglInitialize(globals.display, &majorVersion, &minorVersion);
}
#endif
  globals.pbufferSurfaceAttributes = {EGL_WIDTH,           1,        EGL_HEIGHT, 1,
                                      EGL_LARGEST_PBUFFER, EGL_TRUE, EGL_NONE};
  eglBindAPI(EGL_OPENGL_ES_API);
  EGLint numConfigs = 0;
  const EGLint configAttribs[] = {EGL_SURFACE_TYPE,
                                  EGL_WINDOW_BIT,
                                  EGL_RENDERABLE_TYPE,
                                  EGL_OPENGL_ES2_BIT,
                                  EGL_RED_SIZE,
                                  8,
                                  EGL_GREEN_SIZE,
                                  8,
                                  EGL_BLUE_SIZE,
                                  8,
                                  EGL_ALPHA_SIZE,
                                  8,
                                  EGL_STENCIL_SIZE,
                                  8,
                                  EGL_NONE};
  eglChooseConfig(globals.display, configAttribs, &globals.windowConfig, 1, &numConfigs);

  const EGLint configPbufferAttribs[] = {EGL_SURFACE_TYPE,
                                         EGL_PBUFFER_BIT,
                                         EGL_RENDERABLE_TYPE,
                                         EGL_OPENGL_ES2_BIT,
                                         EGL_RED_SIZE,
                                         8,
                                         EGL_GREEN_SIZE,
                                         8,
                                         EGL_BLUE_SIZE,
                                         8,
                                         EGL_ALPHA_SIZE,
                                         8,
                                         EGL_STENCIL_SIZE,
                                         8,
                                         EGL_NONE};
  eglChooseConfig(globals.display, configPbufferAttribs, &globals.pbufferConfig, 1, &numConfigs);
  return globals;
}

static std::atomic<const EGLGlobals*> externalGlobals = {nullptr};

void EGLGlobals::Set(const EGLGlobals* globals) {
  externalGlobals.store(globals, std::memory_order_relaxed);
}

const EGLGlobals* EGLGlobals::Get() {
  auto globals = externalGlobals.load(std::memory_order_relaxed);
  if (!globals) {
    static const EGLGlobals eglGlobals = InitializeEGL();
    globals = &eglGlobals;
  }
  return globals;
}
}  // namespace tgfx

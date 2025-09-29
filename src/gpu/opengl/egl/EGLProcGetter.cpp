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

#include "EGLProcGetter.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <cstring>
#include "gpu/opengl/GLCoreFunctions.h"

namespace tgfx {

static void* egl_get_gl_proc(void*, const char name[]) {
  // https://www.khronos.org/registry/EGL/extensions/KHR/EGL_KHR_get_all_proc_addresses.txt
  // eglGetProcAddress() is not guaranteed to support the querying of non-extension EGL functions.
#define M(X)                           \
  if (0 == strcmp(#X, name)) {         \
    return reinterpret_cast<void*>(X); \
  }

  GL_CORE_FUNCTIONS_EACH(M)
  M(eglGetCurrentDisplay)
  M(eglQueryString)
  M(glMapBufferRange)
  M(glUnmapBuffer)

#undef M
  auto fun = eglGetProcAddress(name);
  return reinterpret_cast<void*>(fun);
}

void* EGLProcGetter::getProcAddress(const char* name) const {
  return egl_get_gl_proc(nullptr, name);
}

std::unique_ptr<GLProcGetter> GLProcGetter::Make() {
  return std::make_unique<EGLProcGetter>();
}
}  // namespace tgfx
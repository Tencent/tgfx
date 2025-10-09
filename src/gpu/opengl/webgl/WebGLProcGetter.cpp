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

#include "WebGLProcGetter.h"
#include <webgl/webgl1.h>
#include <webgl/webgl2.h>
#include <cstring>
#include "gpu/opengl/GLCoreFunctions.h"

namespace tgfx {
static void emscripten_glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
  auto timeoutLo = static_cast<uint32_t>(timeout);
  uint32_t timeoutHi = timeout >> 32;
  emscripten_glWaitSync(sync, flags, timeoutLo, timeoutHi);
}

void* WebGLProcGetter::getProcAddress(const char* name) const {
#define M(X)                                        \
  if (0 == strcmp(#X, name)) {                      \
    return reinterpret_cast<void*>(emscripten_##X); \
  }

  GL_CORE_FUNCTIONS_EACH(M)
  M(glBindVertexArrayOES)
  M(glDeleteVertexArraysOES)
  M(glGenVertexArraysOES)

#undef M

  // We explicitly do not use GetProcAddress or something similar because its code size is quite
  // large. We shouldn't need GetProcAddress because emscripten provides us all the valid function
  // pointers for WebGL via the included headers.
  // https://github.com/emscripten-core/emscripten/blob/7ba7700902c46734987585409502f3c63beb650f/system/include/emscripten/html5_webgl.h#L93
  return nullptr;
}

std::unique_ptr<GLProcGetter> GLProcGetter::Make() {
  return std::make_unique<WebGLProcGetter>();
}
}  // namespace tgfx

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

#include "GLSemaphore.h"
#include "gpu/opengl/GLGPU.h"
#include "tgfx/gpu/opengl/GLFunctions.h"

namespace tgfx {
BackendSemaphore GLSemaphore::stealBackend() {
  if (_glSync == nullptr) {
    return {};
  }
  GLSyncInfo glSyncInfo = {};
  glSyncInfo.sync = _glSync;
  _glSync = nullptr;
  return {glSyncInfo};
}

void GLSemaphore::onRelease(GLGPU* gpu) {
  if (_glSync != nullptr) {
    auto gl = gpu->functions();
    gl->deleteSync(_glSync);
    _glSync = nullptr;
  }
}

}  // namespace tgfx
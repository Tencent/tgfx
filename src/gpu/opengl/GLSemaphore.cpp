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
#include "tgfx/gpu/opengl/GLFunctions.h"

namespace tgfx {
std::shared_ptr<Semaphore> Semaphore::Wrap(Context* context,
                                           const BackendSemaphore& backendSemaphore) {
  GLSyncInfo glSyncInfo = {};
  if (context == nullptr || !context->caps()->semaphoreSupport ||
      !backendSemaphore.getGLSync(&glSyncInfo)) {
    return nullptr;
  }
  auto semaphore = new GLSemaphore(glSyncInfo.sync);
  return Resource::AddToCache(context, semaphore);
}

BackendSemaphore GLSemaphore::releaseBackend() {
  if (_glSync == nullptr) {
    return {};
  }
  GLSyncInfo glSyncInfo = {};
  glSyncInfo.sync = _glSync;
  // Release ownership of the sync object.
  _glSync = nullptr;
  return {glSyncInfo};
}

void GLSemaphore::onReleaseGPU() {
  if (_glSync != nullptr) {
    auto gl = GLFunctions::Get(context);
    gl->deleteSync(_glSync);
    _glSync = nullptr;
  }
}

}  // namespace tgfx
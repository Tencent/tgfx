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

#pragma once

#include "gpu/Semaphore.h"
#include "gpu/opengl/GLResource.h"

namespace tgfx {
/**
 * GLSemaphore is a wrapper class for an OpenGL sync object.
 */
class GLSemaphore : public Semaphore, public GLResource {
 public:
  explicit GLSemaphore(void* glSync) : _glSync(glSync) {
  }

  void* glSync() const {
    return _glSync;
  }

  BackendSemaphore stealBackend() override;

 protected:
  void onRelease(GLGPU* gpu) override;

 private:
  void* _glSync = nullptr;
};
}  // namespace tgfx

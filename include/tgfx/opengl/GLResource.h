/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/gpu/Context.h"
#include "tgfx/opengl/GLFunctions.h"

namespace tgfx {
/**
 * The base class for custom OpenGL resources. Overrides the onReleaseGPU() method to free all GPU
 * resources. No backend API calls should be made during destructuring since there may be no OpenGL
 * context that is current on the calling thread.
 */
class GLResource {
 public:
  /**
   * Attaches a GLResource to the associated Context. The onReleaseGPU() method will be called when
   * all external references to the GLResource are released or when the associated Context is
   * destroyed.
   */
  static void AttachToContext(Context* context, std::shared_ptr<GLResource> resource);

  virtual ~GLResource() = default;

  /**
   * Retrieves the context associated with the resource. Returns nullptr if the resource has been
   * released.
   */
  Context* getContext() const {
    return context;
  }

 protected:
  virtual void onReleaseGPU() = 0;

 private:
  Context* context = nullptr;

  friend class GLExternalResource;
};
}  // namespace tgfx

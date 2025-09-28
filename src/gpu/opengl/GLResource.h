/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include <list>
#include "core/utils/ReturnQueue.h"

namespace tgfx {
class GLGPU;

/**
 * GLResource is the base class for OpenGL resources. Subclasses must implement the onRelease()
 * method to free all underlying GPU resources. No OpenGL API calls should be made during
 * destructuring since there may be no OpenGL context that is current on the calling thread.
 */
class GLResource : public ReturnNode {
 protected:
  /**
   * Overridden to free the underlying OpenGL resources. After calling this method, the GLResource
   * must not be used, as doing so may lead to undefined behavior.
   */
  virtual void onRelease(GLGPU* gpu) = 0;

 private:
  std::list<GLResource*>::iterator cachedPosition;

  friend class GLGPU;
};
}  // namespace tgfx

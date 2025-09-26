/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

namespace tgfx {
/**
 * The base class for custom GPU programs. Overrides the onReleaseGPU() method to free all GPU
 * resources. No backend API calls should be made during destructuring since there may be no GPU
 * context that is current on the calling thread.
 */
class RuntimeProgram {
 public:
  explicit RuntimeProgram(Context* context) : context(context) {
  }

  virtual ~RuntimeProgram() = default;

  /**
   * Retrieves the context associated with this RuntimeProgram.
   */
  Context* getContext() const {
    return context;
  }

 protected:
  /**
   * Overrides this method to free all GPU resources. After this method is called, the associated
   * context will be set to nullptr.
   */
  virtual void onReleaseGPU() = 0;

 private:
  Context* context = nullptr;

  friend class GLRuntimeProgram;
};
}  // namespace tgfx

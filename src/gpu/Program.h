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

#include "tgfx/core/BytesKey.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
/**
 * The base class for GPU program. Overrides the onReleaseGPU() method to free all GPU resources.
 * No backend API calls should be made during destructuring since there may be no GPU context that
 * is current on the calling thread.
 */
class Program {
 public:
  explicit Program(Context* context) : context(context) {
  }

  virtual ~Program() = default;

  /**
   * Retrieves the context associated with this Program.
   */
  Context* getContext() const {
    return context;
  }

 protected:
  Context* context = nullptr;

  /**
   * Overridden to free GPU resources in the backend API.
   */
  virtual void onReleaseGPU() = 0;

 private:
  BytesKey programKey = {};

  friend class ProgramCache;
};
}  // namespace tgfx

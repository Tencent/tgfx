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

#include "gpu/opengl/GLCaps.h"
#include "gpu/opengl/GLFunctions.h"
#include "gpu/opengl/GLProcGetter.h"

namespace tgfx {
class GLInterface {
 public:
  static std::shared_ptr<GLInterface> GetNative();

  const GLCaps* caps() const {
    return _caps.get();
  }

  const GLFunctions* functions() const {
    return _functions.get();
  }

 private:
  std::unique_ptr<GLCaps> _caps = nullptr;
  std::unique_ptr<GLFunctions> _functions = nullptr;

  static std::shared_ptr<GLInterface> MakeNativeInterface(const GLProcGetter* getter);

  GLInterface(std::unique_ptr<GLCaps> caps, std::unique_ptr<GLFunctions> functions)
      : _caps(std::move(caps)), _functions(std::move(functions)) {
  }
};
}  // namespace tgfx

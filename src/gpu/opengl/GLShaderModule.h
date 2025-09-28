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

#include "gpu/ShaderModule.h"
#include "gpu/opengl/GLResource.h"

namespace tgfx {
class GLShaderModule : public ShaderModule, public GLResource {
 public:
  explicit GLShaderModule(unsigned shader) : _shader(shader) {
  }

  unsigned shader() const {
    return _shader;
  }

 protected:
  void onRelease(GLGPU* gpu) override;

 private:
  unsigned _shader = 0;
};
}  // namespace tgfx

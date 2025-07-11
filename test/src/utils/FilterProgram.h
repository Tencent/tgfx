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

#include "gpu/opengl/GLUtil.h"
#include "tgfx/gpu/RuntimeProgram.h"
#include "tgfx/gpu/opengl/GLFunctions.h"

namespace tgfx {
class Uniforms {
 public:
  virtual ~Uniforms() = default;
};

class FilterProgram : public RuntimeProgram {
 public:
  static std::unique_ptr<FilterProgram> Make(Context* context, const std::string& vertex,
                                             const std::string& fragment);

  unsigned program = 0;
  unsigned int vertexArray = 0;
  unsigned int vertexBuffer = 0;
  std::unique_ptr<Uniforms> uniforms = nullptr;

 protected:
  void onReleaseGPU() override;

 private:
  explicit FilterProgram(Context* context) : RuntimeProgram(context) {
  }
};
}  // namespace tgfx

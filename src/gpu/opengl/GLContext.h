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

#include "GLInterface.h"
#include "gpu/opengl/GLFrameBuffer.h"
#include "gpu/opengl/GLTextureSampler.h"
#include "gpu/opengl/GLVertexArray.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
class GLCaps;

class GLContext : public Context {
 public:
  static GLContext* Unwrap(Context* context) {
    return static_cast<GLContext*>(context);
  }

  GLContext(Device* device, const GLInterface* glInterface);

  ~GLContext() override;

  Backend backend() const override {
    return Backend::OPENGL;
  }

  const GLFunctions* functions() const {
    return glInterface->functions.get();
  }

  const Caps* caps() const override {
    return glInterface->caps.get();
  }

  GPU* gpu() const override {
    return _gpu;
  }

  std::shared_ptr<GLVertexArray> sharedVertexArray() const {
    return _sharedVertexArray;
  }

  std::shared_ptr<GLFrameBuffer> sharedFrameBuffer() const {
    return _sharedFrameBuffer;
  }

 private:
  GPU* _gpu = nullptr;
  const GLInterface* glInterface = nullptr;
  std::shared_ptr<GLVertexArray> _sharedVertexArray = nullptr;
  std::shared_ptr<GLFrameBuffer> _sharedFrameBuffer = nullptr;

  friend class GLDevice;
  friend class GLInterface;
};
}  // namespace tgfx

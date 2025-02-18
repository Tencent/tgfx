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

#include <optional>
#include "GLContext.h"
#include "gpu/Program.h"
#include "gpu/ProgramInfo.h"
#include "gpu/SLType.h"
#include "gpu/opengl/GLRenderTarget.h"
#include "gpu/opengl/GLUniformHandler.h"

namespace tgfx {
class GLProgram : public Program {
 public:
  struct Attribute {
    SLType gpuType = SLType::Float;
    size_t offset = 0;
    int location = 0;
  };

  GLProgram(Context* context, unsigned programID, std::unique_ptr<GLUniformBuffer> uniformBuffer,
            std::vector<Attribute> attributes, int vertexStride);

  void setupSamplerUniforms(const std::vector<GLUniform>& textureSamplers) const;

  /**
   * Gets the GL program ID for this program.
   */
  unsigned programID() const {
    return programId;
  }

  /**
   * This function uploads uniforms, calls each GL*Processor's setData. It binds all fragment
   * processor textures.
   *
   * It is the caller's responsibility to ensure the program is bound before calling.
   */
  void updateUniformsAndTextureBindings(const RenderTarget* renderTarget,
                                        const ProgramInfo* programInfo);

  int vertexStride() const {
    return _vertexStride;
  }

  const std::vector<Attribute>& vertexAttributes() const {
    return attributes;
  }

 protected:
  void onReleaseGPU() override;

 private:
  struct RenderTargetState {
    std::optional<int> width;
    std::optional<int> height;
    std::optional<ImageOrigin> origin;
  };

  void setRenderTargetState(const RenderTarget* renderTarget);

  RenderTargetState renderTargetState;
  unsigned programId = 0;
  std::unique_ptr<GLUniformBuffer> uniformBuffer = nullptr;

  std::vector<Attribute> attributes;
  int _vertexStride = 0;
};
}  // namespace tgfx

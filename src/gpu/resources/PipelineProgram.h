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

#include "gpu/GPUBuffer.h"
#include "gpu/resources/Program.h"

namespace tgfx {
class PipelineProgram : public Program {
 public:
  explicit PipelineProgram(std::shared_ptr<RenderPipeline> pipeline,
                           std::unique_ptr<UniformData> vertexUniformData,
                           std::unique_ptr<UniformData> fragmentUniformData)
      : pipeline(std::move(pipeline)), vertexUniformData(std::move(vertexUniformData)),
        fragmentUniformData(std::move(fragmentUniformData)) {
  }

  std::shared_ptr<RenderPipeline> getPipeline() const {
    return pipeline;
  }

  UniformData* getUniformData(ShaderStage stage) const {
    if (stage == ShaderStage::Vertex) {
      return vertexUniformData.get();
    }
    if (stage == ShaderStage::Fragment) {
      return fragmentUniformData.get();
    }
    return nullptr;
  }

 private:
  std::shared_ptr<RenderPipeline> pipeline = nullptr;
  std::unique_ptr<UniformData> vertexUniformData = nullptr;
  std::unique_ptr<UniformData> fragmentUniformData = nullptr;

  friend class ProgramInfo;
};
}  // namespace tgfx

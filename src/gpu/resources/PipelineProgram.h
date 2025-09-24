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
  explicit PipelineProgram(std::unique_ptr<GPURenderPipeline> pipeline,
                           std::unique_ptr<UniformBuffer> vertexUniformBuffer,
                           std::unique_ptr<UniformBuffer> fragmentUniformBuffer)
      : pipeline(std::move(pipeline)), vertexUniformBuffer(std::move(vertexUniformBuffer)),
        fragmentUniformBuffer(std::move(fragmentUniformBuffer)) {
  }

  GPURenderPipeline* getPipeline() const {
    return pipeline.get();
  }

 protected:
  void onReleaseGPU() override {
    pipeline->release(context->gpu());
  }

 private:
  std::unique_ptr<GPURenderPipeline> pipeline = nullptr;
  std::unique_ptr<UniformBuffer> vertexUniformBuffer = nullptr;
  std::unique_ptr<UniformBuffer> fragmentUniformBuffer = nullptr;
  std::shared_ptr<GPUBuffer> uniformGPUBuffer = nullptr;
  size_t uniformGPUBufferBaseOffset = 0;

  friend class ProgramInfo;
};
}  // namespace tgfx

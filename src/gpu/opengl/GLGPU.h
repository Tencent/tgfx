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

#include <concurrentqueue.h>
#include "gpu/GPU.h"
#include "gpu/opengl/GLCommandQueue.h"
#include "gpu/opengl/GLResource.h"
#include "gpu/opengl/GLState.h"

namespace tgfx {
class GLGPU : public GPU {
 public:
  ~GLGPU() override;

  Backend backend() const override {
    return Backend::OPENGL;
  }

  const Caps* caps() const override {
    return interface->caps();
  }

  const GLFunctions* functions() const {
    return interface->functions();
  }

  GLState* state() const {
    return _state.get();
  }

  CommandQueue* queue() const override {
    return commandQueue.get();
  }

  void resetGLState() override {
    _state->reset();
  }

  std::shared_ptr<GPUBuffer> createBuffer(size_t size, uint32_t usage) override;

  std::shared_ptr<GPUTexture> createTexture(const GPUTextureDescriptor& descriptor) override;

  PixelFormat getExternalTextureFormat(const BackendTexture& backendTexture) const override;

  PixelFormat getExternalTextureFormat(const BackendRenderTarget& renderTarget) const override;

  std::shared_ptr<GPUTexture> importExternalTexture(const BackendTexture& backendTexture,
                                                    uint32_t usage, bool adopted) override;

  std::shared_ptr<GPUFence> importExternalFence(const BackendSemaphore& semaphore) override;

  std::shared_ptr<GPUTexture> importExternalTexture(
      const BackendRenderTarget& renderTarget) override;

  std::shared_ptr<GPUSampler> createSampler(const GPUSamplerDescriptor& descriptor) override;

  std::shared_ptr<ShaderModule> createShaderModule(
      const ShaderModuleDescriptor& descriptor) override;

  std::shared_ptr<RenderPipeline> createRenderPipeline(
      const RenderPipelineDescriptor& descriptor) override;

  std::shared_ptr<CommandEncoder> createCommandEncoder() override;

  void processUnreferencedResources();

  void releaseAll(bool releaseGPU);

  template <typename T, typename... Args>
  std::shared_ptr<T> makeResource(Args&&... args) {
    static_assert(std::is_base_of_v<GLResource, T>, "T must be a subclass of GLResource!");
    auto resource = new T(std::forward<Args>(args)...);
    return std::static_pointer_cast<T>(addResource(resource));
  }

 protected:
  explicit GLGPU(std::shared_ptr<GLInterface> glInterface);

 private:
  std::shared_ptr<GLState> _state = nullptr;
  std::shared_ptr<GLInterface> interface = nullptr;
  std::unique_ptr<GLCommandQueue> commandQueue = nullptr;
  std::list<GLResource*> resources = {};
  std::shared_ptr<ReturnQueue> returnQueue = ReturnQueue::Make();

  std::shared_ptr<GLResource> addResource(GLResource* resource);
};
}  // namespace tgfx

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

  const GPUInfo* info() const override {
    return interface->caps()->info();
  }

  const GLCaps* caps() const {
    return interface->caps();
  }

  const GPUFeatures* features() const override {
    return interface->caps()->features();
  }

  const GPULimits* limits() const override {
    return interface->caps()->limits();
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

  bool isFormatRenderable(PixelFormat pixelFormat) const override {
    return interface->caps()->isFormatRenderable(pixelFormat);
  }

  int getSampleCount(int requestedCount, PixelFormat pixelFormat) const override {
    return interface->caps()->getSampleCount(requestedCount, pixelFormat);
  }

  void resetGLState() override {
    _state->reset();
  }

  std::shared_ptr<GPUBuffer> createBuffer(size_t size, uint32_t usage) override;

  std::shared_ptr<Texture> createTexture(const TextureDescriptor& descriptor) override;

  std::shared_ptr<Texture> importBackendTexture(const BackendTexture& backendTexture,
                                                uint32_t usage, bool adopted) override;

  std::shared_ptr<Texture> importBackendRenderTarget(
      const BackendRenderTarget& renderTarget) override;

  std::shared_ptr<Semaphore> importBackendSemaphore(const BackendSemaphore& semaphore) override;

  BackendSemaphore stealBackendSemaphore(std::shared_ptr<Semaphore> semaphore) override;

  std::shared_ptr<Sampler> createSampler(const SamplerDescriptor& descriptor) override;

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

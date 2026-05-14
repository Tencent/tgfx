/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include <list>
#include <memory>
#include <unordered_map>
#include "D3D12Defines.h"
#include "D3D12FrameSession.h"
#include "D3D12Util.h"
#include "core/utils/ReturnQueue.h"
#include "tgfx/gpu/GPU.h"

namespace shaderc {
class Compiler;
};

namespace tgfx {

class D3D12CommandQueue;
class D3D12Resource;

/**
 * D3D12 GPU implementation.
 */
class D3D12GPU : public GPU {
 public:
  static std::unique_ptr<D3D12GPU> Make(ComPtr<ID3D12Device> device);

  ~D3D12GPU();

  ID3D12Device* device() const {
    return d3d12Device.Get();
  }

  IDXGIAdapter1* adapter() const {
    return dxgiAdapter.Get();
  }

  const GPUInfo* info() const override {
    return &_info;
  }

  const GPUFeatures* features() const override {
    return &_features;
  }

  const GPULimits* limits() const override {
    return &_limits;
  }

  CommandQueue* queue() const override;

  const shaderc::Compiler* shaderCompiler() const;

  unsigned getDXGIFormat(PixelFormat format) const {
    return PixelFormatToDXGIFormat(format);
  }

  bool isFormatRenderable(PixelFormat format) const override;

  std::shared_ptr<GPUBuffer> createBuffer(size_t size, uint32_t usage) override;

  std::shared_ptr<Texture> createTexture(const TextureDescriptor& descriptor) override;

  std::shared_ptr<Sampler> createSampler(const SamplerDescriptor& descriptor) override;

  std::shared_ptr<ShaderModule> createShaderModule(
      const ShaderModuleDescriptor& descriptor) override;

  std::shared_ptr<RenderPipeline> createRenderPipeline(
      const RenderPipelineDescriptor& descriptor) override;

  std::shared_ptr<CommandEncoder> createCommandEncoder() override;

  int getSampleCount(int requestedCount, PixelFormat pixelFormat) const override;

  std::vector<std::shared_ptr<Texture>> importHardwareTextures(HardwareBufferRef hardwareBuffer,
                                                               uint32_t usage) override;

  std::shared_ptr<Texture> importBackendTexture(const BackendTexture& backendTexture,
                                                uint32_t usage, bool adopted = false) override;

  std::shared_ptr<Texture> importBackendRenderTarget(
      const BackendRenderTarget& backendRenderTarget) override;

  std::shared_ptr<Semaphore> importBackendSemaphore(const BackendSemaphore& semaphore) override;

  BackendSemaphore stealBackendSemaphore(std::shared_ptr<Semaphore> semaphore) override;

  template <typename T, typename... Args>
  std::shared_ptr<T> makeResource(Args&&... args) {
    static_assert(std::is_base_of_v<D3D12Resource, T>, "T must be a subclass of D3D12Resource!");
    auto resource = new T(std::forward<Args>(args)...);
    return std::static_pointer_cast<T>(addResource(resource));
  }

  void processUnreferencedResources();

  void releaseAll(bool releaseGPU);

  /**
   * Reclaims resources from a D3D12FrameSession that was created but never submitted (abandon
   * path). Invoked by D3D12CommandBuffer's destructor and by D3D12CommandEncoder::onRelease(). This
   * is the same unified cleanup path used after the GPU fence signals successful completion.
   */
  void reclaimAbandonedSession(D3D12FrameSession session);

 private:
  explicit D3D12GPU(ComPtr<ID3D12Device> device, ComPtr<IDXGIAdapter1> adapter);

  std::shared_ptr<D3D12Resource> addResource(D3D12Resource* resource);

  static uint32_t MakeSamplerKey(const SamplerDescriptor& descriptor);

  void initInfo();
  void initFeatures();
  void initLimits();

  ComPtr<ID3D12Device> d3d12Device = nullptr;
  ComPtr<IDXGIAdapter1> dxgiAdapter = nullptr;
  GPUInfo _info = {};
  GPUFeatures _features = {};
  GPULimits _limits = {};
  std::unique_ptr<D3D12CommandQueue> commandQueue = nullptr;
  std::unique_ptr<shaderc::Compiler> compiler = nullptr;
  std::list<D3D12Resource*> resources = {};
  std::shared_ptr<ReturnQueue> returnQueue = ReturnQueue::Make();
  std::unordered_map<uint32_t, std::shared_ptr<Sampler>> samplerCache = {};
};

}  // namespace tgfx

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

#include <array>
#include <limits>
#include <optional>
#include <unordered_map>
#include "gpu/GPU.h"
#include "gpu/opengl/GLCommandQueue.h"
#include "gpu/opengl/GLInterface.h"

namespace tgfx {
class GLTexture;

static constexpr unsigned INVALID_VALUE = std::numeric_limits<unsigned>::max();

enum class FrameBufferTarget { Draw, Read, Both };

class GLGPU : public GPU {
 public:
  Backend backend() const override {
    return Backend::OPENGL;
  }

  const Caps* caps() const override {
    return interface->caps();
  }

  const GLFunctions* functions() const {
    return interface->functions();
  }

  CommandQueue* queue() const override {
    return commandQueue.get();
  }

  std::unique_ptr<GPUBuffer> createBuffer(size_t size, uint32_t usage) override;

  std::unique_ptr<GPUTexture> createTexture(const GPUTextureDescriptor& descriptor) override;

  PixelFormat getExternalTextureFormat(const BackendTexture& backendTexture) const override;

  PixelFormat getExternalTextureFormat(const BackendRenderTarget& renderTarget) const override;

  std::unique_ptr<GPUTexture> importExternalTexture(const BackendTexture& backendTexture,
                                                    uint32_t usage, bool adopted) override;

  std::unique_ptr<GPUFence> importExternalFence(const BackendSemaphore& semaphore) override;

  std::unique_ptr<GPUTexture> importExternalTexture(
      const BackendRenderTarget& renderTarget) override;

  std::unique_ptr<GPUSampler> createSampler(const GPUSamplerDescriptor& descriptor) override;

  std::unique_ptr<GPUShaderModule> createShaderModule(
      const GPUShaderModuleDescriptor& descriptor) override;

  std::unique_ptr<GPURenderPipeline> createRenderPipeline(
      const GPURenderPipelineDescriptor& descriptor) override;

  std::shared_ptr<CommandEncoder> createCommandEncoder() override;

  void resetGLState() override;

  void enableCapability(unsigned capability, bool enabled);

  void setScissorRect(int x, int y, int width, int height);

  void setViewport(int x, int y, int width, int height);

  void setClearColor(Color color);

  void bindTexture(GLTexture* texture, unsigned textureUnit = 0);

  void bindFramebuffer(GLTexture* texture, FrameBufferTarget target = FrameBufferTarget::Both);

  void useProgram(unsigned programID);

  void bindVertexArray(unsigned vertexArray);

  void setBlendFunc(unsigned srcColorFactor, unsigned dstColorFactor, unsigned srcAlphaFactor,
                    unsigned dstAlphaFactor);

  void setBlendEquation(unsigned colorOp, unsigned alphaOp);

  void setColorMask(uint32_t colorMask);

 protected:
  explicit GLGPU(std::shared_ptr<GLInterface> glInterface);

 private:
  std::shared_ptr<GLInterface> interface = nullptr;
  std::unique_ptr<GLCommandQueue> commandQueue = nullptr;
  std::unordered_map<unsigned, bool> capabilities = {};
  std::vector<uint32_t> textureUnits = {};
  std::array<int, 4> scissorRect = {0, 0, 0, 0};
  std::array<int, 4> viewport = {0, 0, 0, 0};
  std::optional<Color> clearColor = std::nullopt;
  unsigned activeTextureUint = INVALID_VALUE;
  unsigned readFramebuffer = INVALID_VALUE;
  unsigned drawFramebuffer = INVALID_VALUE;
  unsigned program = INVALID_VALUE;
  unsigned vertexArray = INVALID_VALUE;
  unsigned srcColorBlendFactor = INVALID_VALUE;
  unsigned dstColorBlendFactor = INVALID_VALUE;
  unsigned srcAlphaBlendFactor = INVALID_VALUE;
  unsigned dstAlphaBlendFactor = INVALID_VALUE;
  unsigned colorBlendOp = INVALID_VALUE;
  unsigned alphaBlendOp = INVALID_VALUE;
  uint32_t colorWriteMask = 0xF;
};
}  // namespace tgfx

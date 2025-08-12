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

#include "gpu/GPU.h"
#include "gpu/opengl/GLCommandQueue.h"
#include "gpu/opengl/GLInterface.h"

namespace tgfx {
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

  std::unique_ptr<GPUTexture> createTexture(int width, int height, PixelFormat format,
                                            bool mipmapped) override;

  PixelFormat getExternalTextureFormat(const BackendTexture& backendTexture) const override;

  std::unique_ptr<GPUTexture> importExternalTexture(const BackendTexture& backendTexture,
                                                    bool adopted) override;

  std::shared_ptr<CommandEncoder> createCommandEncoder() override;

 protected:
  explicit GLGPU(std::shared_ptr<GLInterface> glInterface);

 private:
  std::shared_ptr<GLInterface> interface = nullptr;
  std::unique_ptr<GLCommandQueue> commandQueue = nullptr;
};
}  // namespace tgfx

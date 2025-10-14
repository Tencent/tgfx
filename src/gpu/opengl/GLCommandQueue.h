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

#include "gpu/CommandQueue.h"

namespace tgfx {
class GLGPU;

class GLCommandQueue : public CommandQueue {
 public:
  explicit GLCommandQueue(GLGPU* gpu) : gpu(gpu) {
  }

  void writeBuffer(std::shared_ptr<GPUBuffer> buffer, size_t bufferOffset, const void* data,
                   size_t size) override;

  void writeTexture(std::shared_ptr<GPUTexture> texture, const Rect& rect, const void* pixels,
                    size_t rowBytes) override;

  void submit(std::shared_ptr<CommandBuffer>) override;

  std::shared_ptr<GPUFence> insertFence() override;

  void waitForFence(std::shared_ptr<GPUFence> fence) override;

  void waitUntilCompleted() override;

 private:
  GLGPU* gpu = nullptr;
};
}  // namespace tgfx

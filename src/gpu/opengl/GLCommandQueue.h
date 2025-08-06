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
#include "gpu/opengl/GLInterface.h"

namespace tgfx {
class GLCommandQueue : public CommandQueue {
 public:
  explicit GLCommandQueue(std::shared_ptr<GLInterface> interface)
      : interface(std::move(interface)) {
  }

  bool writeBuffer(GPUBuffer* buffer, size_t bufferOffset, const void* data, size_t size) override;

  void submit(std::shared_ptr<CommandBuffer>) override;

  void waitUntilCompleted() override;

 private:
  std::shared_ptr<GLInterface> interface = nullptr;
};
}  // namespace tgfx

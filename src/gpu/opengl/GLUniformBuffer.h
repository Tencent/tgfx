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

#include "gpu/UniformBuffer.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
class GLUniformBuffer : public UniformBuffer {
 public:
  GLUniformBuffer(std::vector<Uniform> uniforms, std::vector<int> locations);

  ~GLUniformBuffer() override;

  void uploadToGPU(Context* context);

 protected:
  void onCopyData(size_t index, size_t offset, size_t size, const void* data) override;

 private:
  uint8_t* buffer = nullptr;
  bool bufferChanged = false;
  std::vector<int> locations = {};
  std::vector<bool> dirtyFlags = {};
};
}  // namespace tgfx

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

#include "GLGPU.h"
#include "gpu/opengl/GLBuffer.h"
#include "gpu/opengl/GLCommandEncoder.h"

namespace tgfx {
GLGPU::GLGPU(std::shared_ptr<GLInterface> glInterface) : interface(std::move(glInterface)) {
  commandQueue = std::make_unique<GLCommandQueue>(interface);
}

std::unique_ptr<GPUBuffer> GLGPU::createBuffer(size_t size, uint32_t usage) const {
  if (size == 0) {
    return nullptr;
  }
  unsigned target = 0;
  if (usage & GPUBufferUsage::VERTEX) {
    target = GL_ARRAY_BUFFER;
  } else if (usage & GPUBufferUsage::INDEX) {
    target = GL_ELEMENT_ARRAY_BUFFER;
  } else {
    LOGE("GLGPU::createBuffer() invalid buffer usage!");
    return nullptr;
  }
  auto gl = interface->functions();
  unsigned bufferID = 0;
  gl->genBuffers(1, &bufferID);
  if (bufferID == 0) {
    return nullptr;
  }
  gl->bindBuffer(target, bufferID);
  gl->bufferData(target, static_cast<GLsizeiptr>(size), nullptr, GL_STATIC_DRAW);
  gl->bindBuffer(target, 0);
  return std::make_unique<GLBuffer>(bufferID, size, usage);
}

std::shared_ptr<CommandEncoder> GLGPU::createCommandEncoder() const {
  return std::make_shared<GLCommandEncoder>(interface);
}
}  // namespace tgfx

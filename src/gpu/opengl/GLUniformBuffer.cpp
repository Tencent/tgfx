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

#include "GLUniformBuffer.h"
#include "core/utils/UniqueID.h"

namespace tgfx {

GLUniformBuffer::GLUniformBuffer(size_t size)
    : GLBuffer(nullptr, 0, size, GPUBufferUsage::UNIFORM), uniqueID(UniqueID::Next()) {
}

GLUniformBuffer::~GLUniformBuffer() {
  releaseInternal();
}

void* GLUniformBuffer::map() {
  mappedAddress = malloc(_size);
  return mappedAddress;
}

void GLUniformBuffer::unmap() {

}

void GLUniformBuffer::releaseInternal() {
  if (mappedAddress != nullptr) {
    free(mappedAddress);
    mappedAddress = nullptr;
  }
}
}  // namespace tgfx

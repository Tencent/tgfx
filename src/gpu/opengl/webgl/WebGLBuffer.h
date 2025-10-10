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

#include "gpu/opengl/GLBuffer.h"

namespace tgfx {
class WebGLBuffer : public GLBuffer {
 public:
  /**
   * Creates a new WebGLBuffer with the specified size and usage flags.
   */
  WebGLBuffer(std::shared_ptr<GLInterface> interface, unsigned bufferID, size_t size,
              uint32_t usage);

  void* map(size_t offset, size_t size) override;

  void unmap() override;

 private:
  size_t subDataOffset = 0;
  size_t subDataSize = 0;
};
}  // namespace tgfx

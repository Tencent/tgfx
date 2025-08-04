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

#include <cstdint>

namespace tgfx {
/**
 * GPUBufferUsage defines the usage flags for GPU buffers.
 */
class GPUBufferUsage {
 public:
  /**
   * The buffer can be used as an index buffer, for example as the buffer argument passed to
   * RenderPass::setIndexBuffer().
   */
  static constexpr uint32_t INDEX = 0x10;

  /**
   * The buffer can be used as a vertex buffer, for example as the buffer argument passed to
   * RenderPass::setVertexBuffer().
   */
  static constexpr uint32_t VERTEX = 0x20;
};
}  // namespace tgfx

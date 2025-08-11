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

#include <memory>
#include "gpu/CommandEncoder.h"
#include "gpu/CommandQueue.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/Caps.h"
#include "tgfx/platform/HardwareBuffer.h"

namespace tgfx {
/**
 * This is the main interface for accessing GPU functionality. In Metal, Vulkan, and WebGPU, its
 * equivalents are MTLDevice, VkDevice, and GPUDevice. For OpenGL, it simply refers to GL functions.
 */
class GPU {
 public:
  virtual ~GPU() = default;

  /**
   * Returns the backend type of the GPU.
   */
  virtual Backend backend() const = 0;

  /**
   * Returns the capability info of the GPU.
   */
  virtual const Caps* caps() const = 0;

  /**
   * Returns the primary CommandQueue associated with this GPU.
   */
  virtual CommandQueue* queue() const = 0;

  /**
   * Creates a GPUBuffer with the specified size and usage flags. The usage flags determine how the
   * buffer can be used in GPU operations, such as vertex or index buffers.
   * @param size The size of the buffer in bytes.
   * @param usage The usage flags for the buffer, defined in GPUBufferUsage.
   * @return A unique pointer to the created GPUBuffer. The caller is responsible for managing the
   * lifetime of the buffer. If the creation fails, it returns nullptr.
   */
  virtual std::unique_ptr<GPUBuffer> createBuffer(size_t size, uint32_t usage) = 0;

  /**
   * Returns the PixelFormat of the texture created from the given hardware buffer. If the
   * hardwareBuffer is invalid or contains multiple planes (such as YUV formats), returns
   * PixelFormat::Unknown.
   */
  virtual PixelFormat getPixelFormat(HardwareBufferRef hardwareBuffer) const = 0;

  /**
   * Creates hardware textures from a platform-specific hardware buffer, such as AHardwareBuffer on
   * Android or CVPixelBufferRef on Apple platforms. Multiple textures can be created from the same
   * hardwareBuffer (typically for YUV formats). If yuvFormat is not nullptr, it will be set to the
   * hardwareBuffer's YUVFormat. Returns nullptr if any parameter is invalid or the GPU backend does
   * not support the hardwareBuffer, leaving yuvFormat unchanged.
   */
  virtual std::vector<std::unique_ptr<GPUTexture>> createHardwareTextures(
      HardwareBufferRef hardwareBuffer, YUVFormat* yuvFormat = nullptr) = 0;

  /**
   * Creates a command encoder that can be used to encode commands to be issued to the GPU.
   */
  virtual std::shared_ptr<CommandEncoder> createCommandEncoder() = 0;
};
}  // namespace tgfx

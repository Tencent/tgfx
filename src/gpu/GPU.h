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
#include "gpu/YUVFormat.h"
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
   * Creates a new GPUTexture with the given descriptor. The descriptor specifies the texture's
   * properties, such as width, height, format, mip levels, sample count and usage flags. Returns
   * nullptr if the texture cannot be created.
   */
  virtual std::unique_ptr<GPUTexture> createTexture(const GPUTextureDescriptor& descriptor) = 0;

  /**
   * Returns the pixel formats for textures created from a platform-specific hardware buffer, such
   * as AHardwareBuffer on Android or CVPixelBufferRef on Apple platforms. If yuvFormat is not
   * nullptr, it will be set to the YUV format of the hardwareBuffer if applicable. Note: On some
   * platforms, only a single pixel format may be returned, even if yuvFormat is not
   * YUVFormat::Unknown. In this case, the resulting texture will be read-only and cannot be used as
   * a render target. Returns an empty vector if the hardwareBuffer is invalid or not supported by
   * the GPU backend.
   */
  virtual std::vector<PixelFormat> getHardwareTextureFormats(
      HardwareBufferRef hardwareBuffer, YUVFormat* yuvFormat = nullptr) const = 0;

  /**
   * Creates one or more textures from a platform-specific hardware buffer, such as AHardwareBuffer
   * on Android or CVPixelBufferRef on Apple platforms. Multiple textures may be created from the
   * same hardwareBuffer, especially for YUV formats.
   * @param hardwareBuffer The platform-specific hardware buffer to import textures from.
   * @param usage A bitmask of GPUTextureUsage flags specifying how the textures will be used.
   * @return An empty vector if the hardwareBuffer is invalid or not supported by the GPU backend.
   */
  virtual std::vector<std::unique_ptr<GPUTexture>> importHardwareTextures(
      HardwareBufferRef hardwareBuffer, uint32_t usage) = 0;

  /**
   * Returns the pixel format of the given backend texture. If the backend texture is invalid,
   * returns PixelFormat::Unknown.
   */
  virtual PixelFormat getExternalTextureFormat(const BackendTexture& backendTexture) const = 0;

  /**
   * Returns the pixel format of the given backend render target. If the backend render target is
   * invalid, returns PixelFormat::Unknown.
   */
  virtual PixelFormat getExternalTextureFormat(
      const BackendRenderTarget& backendRenderTarget) const = 0;

  /**
  * Creates a GPUTexture that wraps the specified backend texture.
  * @param backendTexture The backend texture to be wrapped.
  * @param usage A bitmask of GPUTextureUsage flags specifying how the texture will be used.
  * @param adopted If true, the returned GPUTexture takes ownership of the backend texture and will
  * destroy it when no longer needed. If false, the backend texture must remain valid for the
  * lifetime of the GPUTexture.
  * @return A unique pointer to the created GPUTexture. Returns nullptr if the backend texture is
  * invalid or not supported by the GPU backend.
  */
  virtual std::unique_ptr<GPUTexture> importExternalTexture(const BackendTexture& backendTexture,
                                                            uint32_t usage,
                                                            bool adopted = false) = 0;

  /**
   * Creates a GPUTexture that wraps the given backend render target. The caller must ensure the
   * backend render target is valid for the lifetime of the returned GPUTexture. Returns nullptr
   * if the backend render target is invalid.
   */
  virtual std::unique_ptr<GPUTexture> importExternalTexture(
      const BackendRenderTarget& backendRenderTarget) = 0;

  /**
   * Creates a command encoder that can be used to encode commands to be issued to the GPU.
   */
  virtual std::shared_ptr<CommandEncoder> createCommandEncoder() = 0;
};
}  // namespace tgfx

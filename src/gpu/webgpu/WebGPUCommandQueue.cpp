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

#include "WebGPUCommandQueue.h"
#include "WebGPUBuffer.h"
#include "WebGPUCommandBuffer.h"
#include "WebGPUGPU.h"
#include "WebGPUTexture.h"
#include "core/utils/Log.h"

namespace tgfx {

WebGPUCommandQueue::WebGPUCommandQueue(WebGPUGPU* gpu) : gpu(gpu) {
}

void WebGPUCommandQueue::writeBuffer(std::shared_ptr<GPUBuffer> buffer, size_t bufferOffset,
                                     const void* data, size_t size) {
  if (data == nullptr || size == 0) {
    LOGE("WebGPUCommandQueue::writeBuffer() data is null or size is zero!");
    return;
  }
  if (bufferOffset + size > buffer->size()) {
    LOGE("WebGPUCommandQueue::writeBuffer() size exceeds buffer size!");
    return;
  }
  auto webgpuBuffer = std::static_pointer_cast<WebGPUBuffer>(buffer);
  gpu->wgpuQueue().WriteBuffer(webgpuBuffer->wgpuBuffer(), bufferOffset, data, size);
}

void WebGPUCommandQueue::writeTexture(std::shared_ptr<Texture> texture, const Rect& rect,
                                      const void* pixels, size_t rowBytes) {
  if (texture == nullptr || rect.isEmpty() || pixels == nullptr) {
    return;
  }
  if (!(texture->usage() & TextureUsage::TEXTURE_BINDING)) {
    return;
  }
  auto webgpuTexture = std::static_pointer_cast<WebGPUTexture>(texture);

  wgpu::ImageCopyTexture destination = {};
  destination.texture = webgpuTexture->wgpuTexture();
  destination.mipLevel = 0;
  destination.origin = {static_cast<uint32_t>(rect.x()), static_cast<uint32_t>(rect.y()), 0};
  destination.aspect = wgpu::TextureAspect::All;

  wgpu::TextureDataLayout dataLayout = {};
  dataLayout.offset = 0;
  dataLayout.bytesPerRow = static_cast<uint32_t>(rowBytes);
  dataLayout.rowsPerImage = static_cast<uint32_t>(rect.height());

  wgpu::Extent3D writeSize = {static_cast<uint32_t>(rect.width()),
                              static_cast<uint32_t>(rect.height()), 1};

  auto dataSize = rowBytes * static_cast<size_t>(rect.height());
  gpu->wgpuQueue().WriteTexture(&destination, pixels, dataSize, &dataLayout, &writeSize);
}

void WebGPUCommandQueue::submit(std::shared_ptr<CommandBuffer> commandBuffer) {
  if (commandBuffer == nullptr) {
    return;
  }
  auto webgpuCommandBuffer = std::static_pointer_cast<WebGPUCommandBuffer>(commandBuffer);
  wgpu::CommandBuffer cmdBuffer = webgpuCommandBuffer->wgpuCommandBuffer();
  gpu->wgpuQueue().Submit(1, &cmdBuffer);
}

std::shared_ptr<Semaphore> WebGPUCommandQueue::insertSemaphore() {
  // WebGPU does not support semaphore.
  return nullptr;
}

void WebGPUCommandQueue::waitSemaphore(std::shared_ptr<Semaphore>) {
  // WebGPU does not support semaphore.
}

void WebGPUCommandQueue::waitUntilCompleted() {
  // WebGPU uses onSubmittedWorkDone for async completion notification.
  // For synchronous wait, we need to use emscripten's asyncify or polling.
  // Currently leave as no-op since WebGPU is inherently async.
}

}  // namespace tgfx

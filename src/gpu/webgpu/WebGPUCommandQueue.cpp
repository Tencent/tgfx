/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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
#include "WebGPUUtil.h"
#include "core/utils/Log.h"

namespace tgfx {

WebGPUCommandQueue::WebGPUCommandQueue(WebGPUGPU* gpu) : _gpu(gpu) {
  queue = wgpuDeviceGetQueue(gpu->device());
}

void WebGPUCommandQueue::submit(std::shared_ptr<CommandBuffer> commandBuffer) {
  if (!commandBuffer) {
    return;
  }
  auto webgpuCommandBuffer = std::static_pointer_cast<WebGPUCommandBuffer>(commandBuffer);
  auto cmdBuffer = webgpuCommandBuffer->webgpuCommandBuffer();
  if (cmdBuffer != nullptr) {
    wgpuQueueSubmit(queue, 1, &cmdBuffer);
  }
  _gpu->processUnreferencedResources();
}

void WebGPUCommandQueue::writeBuffer(std::shared_ptr<GPUBuffer> buffer, size_t bufferOffset,
                                     const void* data, size_t dataSize) {
  if (!buffer || !data || dataSize == 0) {
    return;
  }
  auto webgpuBuffer = std::static_pointer_cast<WebGPUBuffer>(buffer);
  wgpuQueueWriteBuffer(queue, webgpuBuffer->webgpuBuffer(), bufferOffset, data, dataSize);
}

void WebGPUCommandQueue::writeTexture(std::shared_ptr<Texture> texture, const Rect& rect,
                                      const void* pixels, size_t rowBytes) {
  if (!texture || !pixels) {
    LOGE("WebGPUCommandQueue::writeTexture() invalid parameters: texture=%p, pixels=%p",
         static_cast<void*>(texture.get()), pixels);
    return;
  }
  auto webgpuTexture = std::static_pointer_cast<WebGPUTexture>(texture);
  auto wgpuTexture = webgpuTexture->webgpuTexture();
  if (wgpuTexture == nullptr) {
    LOGE("WebGPUCommandQueue::writeTexture() webgpuTexture is null");
    return;
  }

  auto bytesPerPixel = WGPUTextureFormatBytesPerPixel(webgpuTexture->webgpuFormat());
  auto width = static_cast<uint32_t>(rect.width());
  auto height = static_cast<uint32_t>(rect.height());
  if (rowBytes == 0) {
    rowBytes = width * bytesPerPixel;
  }

  WGPUImageCopyTexture destination = {};
  destination.texture = wgpuTexture;
  destination.origin = {static_cast<uint32_t>(rect.x()), static_cast<uint32_t>(rect.y()), 0};

  WGPUTextureDataLayout dataLayout = {};
  dataLayout.offset = 0;
  dataLayout.bytesPerRow = static_cast<uint32_t>(rowBytes);
  dataLayout.rowsPerImage = height;

  WGPUExtent3D writeSize = {width, height, 1};

  wgpuQueueWriteTexture(queue, &destination, pixels, rowBytes * height, &dataLayout, &writeSize);
}

std::shared_ptr<Semaphore> WebGPUCommandQueue::insertSemaphore() {
  // WebGPU has no cross-queue synchronization support.
  return nullptr;
}

void WebGPUCommandQueue::waitSemaphore(std::shared_ptr<Semaphore>) {
  // WebGPU has no cross-queue synchronization support.
}

void WebGPUCommandQueue::waitUntilCompleted() {
  // WebGPU is asynchronous by design. In Emscripten single-threaded mode,
  // all submitted work completes before the next event loop iteration.
}

}  // namespace tgfx

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

#include "WebGPUCommandEncoder.h"
#include "WebGPUBuffer.h"
#include "WebGPUCommandBuffer.h"
#include "WebGPUGPU.h"
#include "WebGPURenderPass.h"
#include "WebGPUTexture.h"
#include "WebGPUUtil.h"
#ifdef __EMSCRIPTEN__
#include <emscripten/console.h>
#endif

namespace tgfx {

std::shared_ptr<WebGPUCommandEncoder> WebGPUCommandEncoder::Make(WebGPUGPU* gpu) {
  if (gpu == nullptr) {
    return nullptr;
  }
  return gpu->makeResource<WebGPUCommandEncoder>(gpu);
}

WebGPUCommandEncoder::WebGPUCommandEncoder(WebGPUGPU* gpu) : _gpu(gpu) {
  WGPUCommandEncoderDescriptor desc = {};
  commandEncoder = wgpuDeviceCreateCommandEncoder(gpu->device(), &desc);
}

GPU* WebGPUCommandEncoder::gpu() const {
  return _gpu;
}

std::shared_ptr<RenderPass> WebGPUCommandEncoder::onBeginRenderPass(
    const RenderPassDescriptor& descriptor) {
  emscripten_console_logf("[WebGPU CmdEncoder] beginRenderPass: colorAttachments=%zu hasDS=%d",
                          descriptor.colorAttachments.size(),
                          descriptor.depthStencilAttachment.texture != nullptr);
  return WebGPURenderPass::Make(_gpu, commandEncoder, descriptor);
}

void WebGPUCommandEncoder::copyTextureToTexture(std::shared_ptr<Texture> srcTexture,
                                                const Rect& srcRect,
                                                std::shared_ptr<Texture> dstTexture,
                                                const Point& dstOffset) {
  if (commandEncoder == nullptr || srcTexture == nullptr || dstTexture == nullptr) {
    return;
  }
  auto src = std::static_pointer_cast<WebGPUTexture>(srcTexture);
  auto dst = std::static_pointer_cast<WebGPUTexture>(dstTexture);

  WGPUImageCopyTexture srcCopy = {};
  srcCopy.texture = src->webgpuTexture();
  srcCopy.origin = {static_cast<uint32_t>(srcRect.x()), static_cast<uint32_t>(srcRect.y()), 0};

  WGPUImageCopyTexture dstCopy = {};
  dstCopy.texture = dst->webgpuTexture();
  dstCopy.origin = {static_cast<uint32_t>(dstOffset.x), static_cast<uint32_t>(dstOffset.y), 0};

  WGPUExtent3D copySize = {static_cast<uint32_t>(srcRect.width()),
                           static_cast<uint32_t>(srcRect.height()), 1};

  wgpuCommandEncoderCopyTextureToTexture(commandEncoder, &srcCopy, &dstCopy, &copySize);
}

void WebGPUCommandEncoder::copyTextureToBuffer(std::shared_ptr<Texture> srcTexture,
                                               const Rect& srcRect,
                                               std::shared_ptr<GPUBuffer> dstBuffer,
                                               size_t dstOffset, size_t dstRowBytes) {
  if (commandEncoder == nullptr || srcTexture == nullptr || dstBuffer == nullptr) {
    return;
  }
  auto src = std::static_pointer_cast<WebGPUTexture>(srcTexture);
  auto dst = std::static_pointer_cast<WebGPUBuffer>(dstBuffer);

  auto bytesPerPixel = WGPUTextureFormatBytesPerPixel(src->webgpuFormat());
  auto width = static_cast<uint32_t>(srcRect.width());
  if (dstRowBytes == 0) {
    dstRowBytes = width * bytesPerPixel;
  }
  // WebGPU requires bytesPerRow to be a multiple of 256.
  auto alignedRowBytes = (dstRowBytes + 255) & ~static_cast<size_t>(255);

  WGPUImageCopyTexture srcCopy = {};
  srcCopy.texture = src->webgpuTexture();
  srcCopy.origin = {static_cast<uint32_t>(srcRect.x()), static_cast<uint32_t>(srcRect.y()), 0};

  WGPUImageCopyBuffer dstCopy = {};
  dstCopy.buffer = dst->webgpuBuffer();
  dstCopy.layout.offset = dstOffset;
  dstCopy.layout.bytesPerRow = static_cast<uint32_t>(alignedRowBytes);
  dstCopy.layout.rowsPerImage = static_cast<uint32_t>(srcRect.height());

  WGPUExtent3D copySize = {width, static_cast<uint32_t>(srcRect.height()), 1};

  wgpuCommandEncoderCopyTextureToBuffer(commandEncoder, &srcCopy, &dstCopy, &copySize);
}

void WebGPUCommandEncoder::generateMipmapsForTexture(std::shared_ptr<Texture>) {
  // WebGPU has no built-in mipmap generation. This is a known limitation.
}

std::shared_ptr<CommandBuffer> WebGPUCommandEncoder::onFinish() {
  if (commandEncoder == nullptr) {
    return nullptr;
  }
  auto cmdBuffer = wgpuCommandEncoderFinish(commandEncoder, nullptr);
  if (cmdBuffer == nullptr) {
    return nullptr;
  }
  return std::make_shared<WebGPUCommandBuffer>(cmdBuffer);
}

void WebGPUCommandEncoder::onRelease(WebGPUGPU*) {
  if (commandEncoder != nullptr) {
    wgpuCommandEncoderRelease(commandEncoder);
    commandEncoder = nullptr;
  }
}

}  // namespace tgfx

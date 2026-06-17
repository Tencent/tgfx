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
#include <algorithm>
#include "WebGPUBuffer.h"
#include "WebGPUCommandBuffer.h"
#include "WebGPUGPU.h"
#include "WebGPURenderPass.h"
#include "WebGPUTexture.h"
#include "WebGPUUtil.h"
#include "core/utils/Log.h"

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

void WebGPUCommandEncoder::generateMipmapsForTexture(std::shared_ptr<Texture> texture) {
  if (commandEncoder == nullptr || texture == nullptr) {
    return;
  }
  auto webgpuTexture = std::static_pointer_cast<WebGPUTexture>(texture);
  auto mipLevelCount = webgpuTexture->mipLevelCount();
  if (mipLevelCount <= 1) {
    return;
  }
  auto format = webgpuTexture->webgpuFormat();
  auto mipmapPipeline = _gpu->getMipmapPipeline(format);
  if (mipmapPipeline == nullptr) {
    LOGE("generateMipmapsForTexture: failed to get mipmap pipeline.");
    return;
  }
  auto device = _gpu->device();
  auto pipeline = mipmapPipeline->pipeline;
  auto bindGroupLayout = mipmapPipeline->bindGroupLayout;
  auto sampler = mipmapPipeline->sampler;

  // Generate each mip level by rendering from the previous level.
  for (int level = 1; level < mipLevelCount; level++) {
    // Create source view (previous level, single mip).
    WGPUTextureViewDescriptor srcViewDesc = {};
    srcViewDesc.dimension = WGPUTextureViewDimension_2D;
    srcViewDesc.format = format;
    srcViewDesc.baseMipLevel = static_cast<uint32_t>(level - 1);
    srcViewDesc.mipLevelCount = 1;
    srcViewDesc.baseArrayLayer = 0;
    srcViewDesc.arrayLayerCount = 1;
    auto srcView = wgpuTextureCreateView(webgpuTexture->webgpuTexture(), &srcViewDesc);
    if (srcView == nullptr) {
      break;
    }

    // Create destination view (current level, single mip).
    WGPUTextureViewDescriptor dstViewDesc = {};
    dstViewDesc.dimension = WGPUTextureViewDimension_2D;
    dstViewDesc.format = format;
    dstViewDesc.baseMipLevel = static_cast<uint32_t>(level);
    dstViewDesc.mipLevelCount = 1;
    dstViewDesc.baseArrayLayer = 0;
    dstViewDesc.arrayLayerCount = 1;
    auto dstView = wgpuTextureCreateView(webgpuTexture->webgpuTexture(), &dstViewDesc);
    if (dstView == nullptr) {
      wgpuTextureViewRelease(srcView);
      break;
    }

    // Create bind group with source texture view + sampler.
    WGPUBindGroupEntry bgEntries[2] = {};
    bgEntries[0].binding = 0;
    bgEntries[0].textureView = srcView;
    bgEntries[1].binding = 1;
    bgEntries[1].sampler = sampler;
    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bindGroupLayout;
    bgDesc.entryCount = 2;
    bgDesc.entries = bgEntries;
    auto bindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
    if (bindGroup == nullptr) {
      wgpuTextureViewRelease(srcView);
      wgpuTextureViewRelease(dstView);
      break;
    }

    // Begin render pass targeting this mip level.
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = dstView;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0.0, 0.0, 0.0, 0.0};
    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    auto passEncoder = wgpuCommandEncoderBeginRenderPass(commandEncoder, &passDesc);
    if (passEncoder == nullptr) {
      wgpuBindGroupRelease(bindGroup);
      wgpuTextureViewRelease(srcView);
      wgpuTextureViewRelease(dstView);
      break;
    }

    wgpuRenderPassEncoderSetPipeline(passEncoder, pipeline);
    wgpuRenderPassEncoderSetBindGroup(passEncoder, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(passEncoder, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(passEncoder);

    wgpuRenderPassEncoderRelease(passEncoder);
    wgpuBindGroupRelease(bindGroup);
    wgpuTextureViewRelease(srcView);
    wgpuTextureViewRelease(dstView);
  }
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

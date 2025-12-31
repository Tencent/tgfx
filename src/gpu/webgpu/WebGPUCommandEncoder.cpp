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

#include "WebGPUCommandEncoder.h"
#include "WebGPUCommandBuffer.h"
#include "WebGPUGPU.h"
#include "WebGPURenderPass.h"
#include "WebGPUTexture.h"
#include "WebGPUUtil.h"
#include "core/utils/Log.h"
#include "core/utils/PixelFormatUtil.h"

namespace tgfx {

static wgpu::LoadOp ToWGPULoadOp(LoadAction loadAction) {
  switch (loadAction) {
    case LoadAction::DontCare:
      return wgpu::LoadOp::Clear;
    case LoadAction::Load:
      return wgpu::LoadOp::Load;
    case LoadAction::Clear:
      return wgpu::LoadOp::Clear;
  }
  return wgpu::LoadOp::Clear;
}

static wgpu::StoreOp ToWGPUStoreOp(StoreAction storeAction) {
  switch (storeAction) {
    case StoreAction::DontCare:
      return wgpu::StoreOp::Discard;
    case StoreAction::Store:
      return wgpu::StoreOp::Store;
  }
  return wgpu::StoreOp::Store;
}

WebGPUCommandEncoder::WebGPUCommandEncoder(WebGPUGPU* gpu) : _gpu(gpu) {
  wgpu::CommandEncoderDescriptor encoderDesc = {};
  _encoder = _gpu->wgpuDevice().CreateCommandEncoder(&encoderDesc);
}

GPU* WebGPUCommandEncoder::gpu() const {
  return _gpu;
}

std::shared_ptr<RenderPass> WebGPUCommandEncoder::onBeginRenderPass(
    const RenderPassDescriptor& descriptor) {
  if (descriptor.colorAttachments.empty()) {
    LOGE(
        "WebGPUCommandEncoder::beginRenderPass() Invalid render pass descriptor, no color "
        "attachments!");
    return nullptr;
  }
  if (descriptor.colorAttachments.size() > 1) {
    LOGE(
        "WebGPUCommandEncoder::onBeginRenderPass() Multiple color attachments are not yet "
        "supported!");
    return nullptr;
  }
  auto& colorAttachment = descriptor.colorAttachments[0];
  if (colorAttachment.texture == nullptr) {
    LOGE(
        "WebGPUCommandEncoder::beginRenderPass() Invalid render pass descriptor, color attachment "
        "texture is null!");
    return nullptr;
  }
  if (colorAttachment.texture == colorAttachment.resolveTexture) {
    LOGE(
        "WebGPUCommandEncoder::beginRenderPass() Invalid render pass descriptor, color attachment "
        "texture and resolve texture cannot be the same!");
    return nullptr;
  }

  // Build color attachment
  auto webgpuTexture = static_cast<WebGPUTexture*>(colorAttachment.texture.get());
  wgpu::RenderPassColorAttachment wgpuColorAttachment = {};
  wgpuColorAttachment.view = webgpuTexture->createTextureView();
  wgpuColorAttachment.loadOp = ToWGPULoadOp(colorAttachment.loadAction);
  wgpuColorAttachment.storeOp = ToWGPUStoreOp(colorAttachment.storeAction);
  wgpuColorAttachment.clearValue = {
      colorAttachment.clearValue.red, colorAttachment.clearValue.green,
      colorAttachment.clearValue.blue, colorAttachment.clearValue.alpha};

  // Handle MSAA resolve
  if (colorAttachment.resolveTexture != nullptr) {
    auto resolveTexture = static_cast<WebGPUTexture*>(colorAttachment.resolveTexture.get());
    wgpuColorAttachment.resolveTarget = resolveTexture->createTextureView();
  }

  // Build depth stencil attachment if provided
  wgpu::RenderPassDepthStencilAttachment wgpuDepthStencilAttachment = {};
  bool hasDepthStencil = false;
  auto& depthStencilAttachment = descriptor.depthStencilAttachment;
  if (depthStencilAttachment.texture != nullptr) {
    if (depthStencilAttachment.texture->format() != PixelFormat::DEPTH24_STENCIL8) {
      LOGE(
          "WebGPUCommandEncoder::beginRenderPass() Invalid render pass descriptor, depthStencil "
          "attachment texture format must be DEPTH24_STENCIL8!");
      return nullptr;
    }
    hasDepthStencil = true;
    auto depthTexture = static_cast<WebGPUTexture*>(depthStencilAttachment.texture.get());
    wgpuDepthStencilAttachment.view = depthTexture->createTextureView();
    wgpuDepthStencilAttachment.depthLoadOp = ToWGPULoadOp(depthStencilAttachment.loadAction);
    wgpuDepthStencilAttachment.depthStoreOp = ToWGPUStoreOp(depthStencilAttachment.storeAction);
    wgpuDepthStencilAttachment.depthClearValue = depthStencilAttachment.depthClearValue;
    wgpuDepthStencilAttachment.depthReadOnly = depthStencilAttachment.depthReadOnly;
    wgpuDepthStencilAttachment.stencilLoadOp = ToWGPULoadOp(depthStencilAttachment.loadAction);
    wgpuDepthStencilAttachment.stencilStoreOp = ToWGPUStoreOp(depthStencilAttachment.storeAction);
    wgpuDepthStencilAttachment.stencilClearValue = depthStencilAttachment.stencilClearValue;
    wgpuDepthStencilAttachment.stencilReadOnly = depthStencilAttachment.stencilReadOnly;
  }

  // Create render pass descriptor
  wgpu::RenderPassDescriptor passDesc = {};
  passDesc.colorAttachmentCount = 1;
  passDesc.colorAttachments = &wgpuColorAttachment;
  if (hasDepthStencil) {
    passDesc.depthStencilAttachment = &wgpuDepthStencilAttachment;
  }

  wgpu::RenderPassEncoder passEncoder = _encoder.BeginRenderPass(&passDesc);
  if (passEncoder == nullptr) {
    LOGE("WebGPUCommandEncoder::beginRenderPass() failed to create render pass encoder!");
    return nullptr;
  }

  // Set default viewport to cover the entire render target
  auto renderTexture = colorAttachment.texture;
  passEncoder.SetViewport(0.0f, 0.0f, static_cast<float>(renderTexture->width()),
                          static_cast<float>(renderTexture->height()), 0.0f, 1.0f);
  passEncoder.SetScissorRect(0, 0, static_cast<uint32_t>(renderTexture->width()),
                             static_cast<uint32_t>(renderTexture->height()));

  return std::make_shared<WebGPURenderPass>(_gpu, descriptor, std::move(passEncoder));
}

void WebGPUCommandEncoder::copyTextureToTexture(std::shared_ptr<Texture> srcTexture,
                                                const Rect& srcRect,
                                                std::shared_ptr<Texture> dstTexture,
                                                const Point& dstOffset) {
  if (srcTexture == nullptr || dstTexture == nullptr || srcRect.isEmpty()) {
    LOGE("WebGPUCommandEncoder::copyTextureToTexture() invalid arguments!");
    return;
  }
  auto webgpuSrcTexture = static_cast<WebGPUTexture*>(srcTexture.get());
  auto webgpuDstTexture = static_cast<WebGPUTexture*>(dstTexture.get());

  wgpu::ImageCopyTexture srcCopy = {};
  srcCopy.texture = webgpuSrcTexture->wgpuTexture();
  srcCopy.mipLevel = 0;
  srcCopy.origin = {static_cast<uint32_t>(srcRect.left), static_cast<uint32_t>(srcRect.top), 0};
  srcCopy.aspect = wgpu::TextureAspect::All;

  wgpu::ImageCopyTexture dstCopy = {};
  dstCopy.texture = webgpuDstTexture->wgpuTexture();
  dstCopy.mipLevel = 0;
  dstCopy.origin = {static_cast<uint32_t>(dstOffset.x), static_cast<uint32_t>(dstOffset.y), 0};
  dstCopy.aspect = wgpu::TextureAspect::All;

  wgpu::Extent3D copySize = {static_cast<uint32_t>(srcRect.width()),
                             static_cast<uint32_t>(srcRect.height()), 1};

  _encoder.CopyTextureToTexture(&srcCopy, &dstCopy, &copySize);
}

void WebGPUCommandEncoder::copyTextureToBuffer(std::shared_ptr<Texture> srcTexture,
                                               const Rect& srcRect,
                                               std::shared_ptr<GPUBuffer> dstBuffer,
                                               size_t dstOffset, size_t dstRowBytes) {
  if (srcTexture == nullptr || srcRect.isEmpty()) {
    LOGE("WebGPUCommandEncoder::copyTextureToBuffer() source texture or rectangle is invalid!");
    return;
  }
  if (dstBuffer == nullptr || !(dstBuffer->usage() & GPUBufferUsage::READBACK)) {
    LOGE("WebGPUCommandEncoder::copyTextureToBuffer() destination buffer is invalid!");
    return;
  }

  auto format = srcTexture->format();
  auto bytesPerPixel = PixelFormatBytesPerPixel(format);
  auto minRowBytes = static_cast<size_t>(srcRect.width()) * bytesPerPixel;
  if (dstRowBytes == 0) {
    dstRowBytes = minRowBytes;
  } else if (dstRowBytes < minRowBytes) {
    LOGE("WebGPUCommandEncoder::copyTextureToBuffer() dstRowBytes is too small!");
    return;
  }

  // WebGPU requires bytesPerRow to be a multiple of 256.
  size_t alignedRowBytes = (dstRowBytes + 255) & ~static_cast<size_t>(255);

  auto requiredSize = dstOffset + static_cast<size_t>(srcRect.height()) * alignedRowBytes;
  if (dstBuffer->size() < requiredSize) {
    LOGE("WebGPUCommandEncoder::copyTextureToBuffer() destination buffer is too small!");
    return;
  }

  auto webgpuSrcTexture = static_cast<WebGPUTexture*>(srcTexture.get());
  auto webgpuDstBuffer = static_cast<WebGPUBuffer*>(dstBuffer.get());

  wgpu::ImageCopyTexture srcCopy = {};
  srcCopy.texture = webgpuSrcTexture->wgpuTexture();
  srcCopy.mipLevel = 0;
  srcCopy.origin = {static_cast<uint32_t>(srcRect.left), static_cast<uint32_t>(srcRect.top), 0};
  srcCopy.aspect = wgpu::TextureAspect::All;

  wgpu::ImageCopyBuffer dstCopy = {};
  dstCopy.buffer = webgpuDstBuffer->wgpuBuffer();
  dstCopy.layout.offset = dstOffset;
  dstCopy.layout.bytesPerRow = static_cast<uint32_t>(alignedRowBytes);
  dstCopy.layout.rowsPerImage = static_cast<uint32_t>(srcRect.height());

  wgpu::Extent3D copySize = {static_cast<uint32_t>(srcRect.width()),
                             static_cast<uint32_t>(srcRect.height()), 1};

  _encoder.CopyTextureToBuffer(&srcCopy, &dstCopy, &copySize);
}

void WebGPUCommandEncoder::generateMipmapsForTexture(std::shared_ptr<Texture> texture) {
  if (texture == nullptr) {
    return;
  }
  auto webgpuTexture = static_cast<WebGPUTexture*>(texture.get());
  uint32_t mipLevelCount = static_cast<uint32_t>(texture->mipLevelCount());
  if (mipLevelCount <= 1) {
    return;
  }

  // Get or create the mipmap generation pipeline
  wgpu::RenderPipeline mipmapPipeline = _gpu->getMipmapPipeline(texture->format());
  wgpu::Sampler mipmapSampler = _gpu->getMipmapSampler();
  wgpu::BindGroupLayout mipmapBindGroupLayout = _gpu->getMipmapBindGroupLayout();
  if (mipmapPipeline == nullptr || mipmapSampler == nullptr || mipmapBindGroupLayout == nullptr) {
    LOGE("WebGPUCommandEncoder::generateMipmapsForTexture() failed to get mipmap pipeline!");
    return;
  }

  uint32_t width = static_cast<uint32_t>(texture->width());
  uint32_t height = static_cast<uint32_t>(texture->height());

  for (uint32_t level = 1; level < mipLevelCount; level++) {
    // Source: previous mip level
    wgpu::TextureView srcView =
        webgpuTexture->createTextureView(wgpu::TextureViewDimension::e2D, level - 1, 1);

    // Destination: current mip level as render target
    wgpu::TextureView dstView =
        webgpuTexture->createTextureView(wgpu::TextureViewDimension::e2D, level, 1);

    width = std::max(1u, width / 2);
    height = std::max(1u, height / 2);

    // Create bind group for this level
    std::array<wgpu::BindGroupEntry, 2> bindGroupEntries = {};
    bindGroupEntries[0].binding = 0;
    bindGroupEntries[0].textureView = srcView;
    bindGroupEntries[1].binding = 1;
    bindGroupEntries[1].sampler = mipmapSampler;

    wgpu::BindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.layout = mipmapBindGroupLayout;
    bindGroupDesc.entryCount = bindGroupEntries.size();
    bindGroupDesc.entries = bindGroupEntries.data();
    wgpu::BindGroup bindGroup = _gpu->wgpuDevice().CreateBindGroup(&bindGroupDesc);

    // Begin render pass with dstView as color attachment
    wgpu::RenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = dstView;
    colorAttachment.loadOp = wgpu::LoadOp::Clear;
    colorAttachment.storeOp = wgpu::StoreOp::Store;
    colorAttachment.clearValue = {0.0, 0.0, 0.0, 0.0};

    wgpu::RenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    wgpu::RenderPassEncoder pass = _encoder.BeginRenderPass(&passDesc);
    pass.SetPipeline(mipmapPipeline);
    pass.SetBindGroup(0, bindGroup);
    pass.Draw(3);  // Fullscreen triangle
    pass.End();
  }
}

std::shared_ptr<CommandBuffer> WebGPUCommandEncoder::onFinish() {
  wgpu::CommandBuffer commandBuffer = _encoder.Finish();
  if (commandBuffer == nullptr) {
    return nullptr;
  }
  return std::make_shared<WebGPUCommandBuffer>(std::move(commandBuffer));
}

}  // namespace tgfx

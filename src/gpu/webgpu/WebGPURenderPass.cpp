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

#include "WebGPURenderPass.h"
#include "WebGPUBuffer.h"
#include "WebGPUGPU.h"
#include "WebGPURenderPipeline.h"
#include "WebGPUSampler.h"
#include "WebGPUTexture.h"
#include "WebGPUUtil.h"
#include "core/utils/Log.h"
#ifdef __EMSCRIPTEN__
#include <emscripten/console.h>
#endif

namespace tgfx {

std::shared_ptr<WebGPURenderPass> WebGPURenderPass::Make(WebGPUGPU* gpu, WGPUCommandEncoder encoder,
                                                         const RenderPassDescriptor& descriptor) {
  if (gpu == nullptr || encoder == nullptr) {
    return nullptr;
  }

  std::vector<WGPURenderPassColorAttachment> colorAttachments = {};
  for (auto& attachment : descriptor.colorAttachments) {
    if (attachment.texture == nullptr) {
      continue;
    }
    auto texture = std::static_pointer_cast<WebGPUTexture>(attachment.texture);
    WGPURenderPassColorAttachment colorAttach = {};
    colorAttach.view = texture->webgpuRenderView();
    colorAttach.loadOp = ToWGPULoadOp(attachment.loadAction);
    colorAttach.storeOp = ToWGPUStoreOp(attachment.storeAction);
    colorAttach.clearValue = {attachment.clearValue.red, attachment.clearValue.green,
                              attachment.clearValue.blue, attachment.clearValue.alpha};
    colorAttach.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    emscripten_console_logf("[WebGPU RenderPass] Color[%zu]: view=%p loadOp=%d storeOp=%d",
                            colorAttachments.size(), static_cast<void*>(colorAttach.view),
                            colorAttach.loadOp, colorAttach.storeOp);
    if (attachment.resolveTexture != nullptr) {
      auto resolveTexture = std::static_pointer_cast<WebGPUTexture>(attachment.resolveTexture);
      colorAttach.resolveTarget = resolveTexture->webgpuRenderView();
    }
    colorAttachments.push_back(colorAttach);
  }

  WGPURenderPassDepthStencilAttachment depthStencilAttach = {};
  bool hasDepthStencil = (descriptor.depthStencilAttachment.texture != nullptr);
  if (hasDepthStencil) {
    auto depthTexture =
        std::static_pointer_cast<WebGPUTexture>(descriptor.depthStencilAttachment.texture);
    depthStencilAttach.view = depthTexture->webgpuTextureView();
    depthStencilAttach.depthLoadOp = ToWGPULoadOp(descriptor.depthStencilAttachment.loadAction);
    depthStencilAttach.depthStoreOp = ToWGPUStoreOp(descriptor.depthStencilAttachment.storeAction);
    depthStencilAttach.depthClearValue = descriptor.depthStencilAttachment.depthClearValue;
    depthStencilAttach.depthReadOnly = descriptor.depthStencilAttachment.depthReadOnly;
    depthStencilAttach.stencilLoadOp = ToWGPULoadOp(descriptor.depthStencilAttachment.loadAction);
    depthStencilAttach.stencilStoreOp =
        ToWGPUStoreOp(descriptor.depthStencilAttachment.storeAction);
    depthStencilAttach.stencilClearValue = descriptor.depthStencilAttachment.stencilClearValue;
    depthStencilAttach.stencilReadOnly = descriptor.depthStencilAttachment.stencilReadOnly;
    emscripten_console_logf(
        "[WebGPU RenderPass] DS: view=%p depthLoad=%d depthStore=%d stencilLoad=%d stencilStore=%d "
        "depthRO=%d stencilRO=%d",
        static_cast<void*>(depthStencilAttach.view), depthStencilAttach.depthLoadOp,
        depthStencilAttach.depthStoreOp, depthStencilAttach.stencilLoadOp,
        depthStencilAttach.stencilStoreOp, depthStencilAttach.depthReadOnly,
        depthStencilAttach.stencilReadOnly);
  }

  WGPURenderPassDescriptor passDesc = {};
  passDesc.colorAttachmentCount = colorAttachments.size();
  passDesc.colorAttachments = colorAttachments.data();
  passDesc.depthStencilAttachment = hasDepthStencil ? &depthStencilAttach : nullptr;

  emscripten_console_logf(
      "[WebGPU RenderPass] Calling wgpuCommandEncoderBeginRenderPass colorCount=%zu hasDS=%d",
      colorAttachments.size(), hasDepthStencil);
  auto passEncoder = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
  emscripten_console_logf("[WebGPU RenderPass] beginRenderPass: colorCount=%zu hasDS=%d => %p",
                          colorAttachments.size(), hasDepthStencil,
                          static_cast<void*>(passEncoder));
  if (passEncoder == nullptr) {
    return nullptr;
  }

  return std::shared_ptr<WebGPURenderPass>(new WebGPURenderPass(gpu, passEncoder, descriptor));
}

WebGPURenderPass::WebGPURenderPass(WebGPUGPU* gpu, WGPURenderPassEncoder encoder,
                                   const RenderPassDescriptor& descriptor)
    : RenderPass(descriptor), _gpu(gpu), passEncoder(encoder) {
}

GPU* WebGPURenderPass::gpu() const {
  return _gpu;
}

void WebGPURenderPass::setViewport(int x, int y, int width, int height) {
  if (passEncoder == nullptr) {
    return;
  }
  wgpuRenderPassEncoderSetViewport(passEncoder, static_cast<float>(x), static_cast<float>(y),
                                   static_cast<float>(width), static_cast<float>(height), 0.0f,
                                   1.0f);
}

void WebGPURenderPass::setScissorRect(int x, int y, int width, int height) {
  if (passEncoder == nullptr) {
    return;
  }
  emscripten_console_logf("[WebGPU] setScissorRect: x=%d y=%d w=%d h=%d", x, y, width, height);
  // Clamp negative origins and adjust dimensions accordingly. WebGPU requires unsigned values.
  if (x < 0) {
    width += x;
    x = 0;
  }
  if (y < 0) {
    height += y;
    y = 0;
  }
  if (width <= 0 || height <= 0) {
    return;
  }
  wgpuRenderPassEncoderSetScissorRect(passEncoder, static_cast<uint32_t>(x),
                                      static_cast<uint32_t>(y), static_cast<uint32_t>(width),
                                      static_cast<uint32_t>(height));
}

void WebGPURenderPass::setPipeline(std::shared_ptr<RenderPipeline> pipeline) {
  if (passEncoder == nullptr || pipeline == nullptr) {
    return;
  }
  currentPipeline = std::static_pointer_cast<WebGPURenderPipeline>(pipeline);
  if (currentPipeline->webgpuRenderPipeline() == nullptr) {
    currentPipeline = nullptr;
    return;
  }
  wgpuRenderPassEncoderSetPipeline(passEncoder, currentPipeline->webgpuRenderPipeline());
  pendingUniforms.clear();
  pendingTextures.clear();
  bindGroupDirty = true;
}

void WebGPURenderPass::setUniformBuffer(unsigned binding, std::shared_ptr<GPUBuffer> buffer,
                                        size_t offset, size_t size) {
  pendingUniforms[binding] = {std::move(buffer), offset, size};
  bindGroupDirty = true;
}

void WebGPURenderPass::setTexture(unsigned binding, std::shared_ptr<Texture> texture,
                                  std::shared_ptr<Sampler> sampler) {
  pendingTextures[binding] = {std::move(texture), std::move(sampler)};
  bindGroupDirty = true;
}

void WebGPURenderPass::setVertexBuffer(unsigned slot, std::shared_ptr<GPUBuffer> buffer,
                                       size_t offset) {
  if (passEncoder == nullptr || buffer == nullptr) {
    return;
  }
  auto webgpuBuffer = std::static_pointer_cast<WebGPUBuffer>(buffer);
  auto bufferSize = webgpuBuffer->size();
  emscripten_console_logf("[WebGPU] setVertexBuffer: slot=%u buffer=%p offset=%zu bufferSize=%zu",
                          slot, static_cast<void*>(webgpuBuffer->webgpuBuffer()), offset,
                          bufferSize);
  wgpuRenderPassEncoderSetVertexBuffer(passEncoder, slot, webgpuBuffer->webgpuBuffer(), offset,
                                       bufferSize - offset);
}

void WebGPURenderPass::setIndexBuffer(std::shared_ptr<GPUBuffer> buffer, IndexFormat format) {
  if (passEncoder == nullptr || buffer == nullptr) {
    return;
  }
  auto webgpuBuffer = std::static_pointer_cast<WebGPUBuffer>(buffer);
  wgpuRenderPassEncoderSetIndexBuffer(passEncoder, webgpuBuffer->webgpuBuffer(),
                                      ToWGPUIndexFormat(format), 0, webgpuBuffer->size());
}

void WebGPURenderPass::setStencilReference(uint32_t reference) {
  if (passEncoder == nullptr) {
    return;
  }
  wgpuRenderPassEncoderSetStencilReference(passEncoder, reference);
}

void WebGPURenderPass::updateBindGroup() {
  if (!bindGroupDirty || currentPipeline == nullptr || passEncoder == nullptr) {
    return;
  }
  bindGroupDirty = false;

  std::vector<WGPUBindGroupEntry> entries = {};

  for (auto& [binding, uniform] : pendingUniforms) {
    if (uniform.buffer == nullptr) {
      continue;
    }
    auto webgpuBuffer = std::static_pointer_cast<WebGPUBuffer>(uniform.buffer);
    WGPUBindGroupEntry entry = {};
    entry.binding = binding;
    entry.buffer = webgpuBuffer->webgpuBuffer();
    entry.offset = uniform.offset;
    entry.size = uniform.size;
    entries.push_back(entry);
  }

  for (auto& [binding, texBinding] : pendingTextures) {
    if (texBinding.texture == nullptr || texBinding.sampler == nullptr) {
      continue;
    }
    auto webgpuTexture = std::static_pointer_cast<WebGPUTexture>(texBinding.texture);
    auto webgpuSampler = std::static_pointer_cast<WebGPUSampler>(texBinding.sampler);

    unsigned textureIndex = currentPipeline->getTextureIndex(binding);
    WGPUBindGroupEntry textureEntry = {};
    textureEntry.binding = textureIndex;
    textureEntry.textureView = webgpuTexture->webgpuTextureView();
    entries.push_back(textureEntry);

    WGPUBindGroupEntry samplerEntry = {};
    samplerEntry.binding = textureIndex + 1;
    samplerEntry.sampler = webgpuSampler->webgpuSampler();
    entries.push_back(samplerEntry);
  }

  WGPUBindGroupDescriptor bindGroupDesc = {};
  bindGroupDesc.layout = currentPipeline->bindGroupLayout();
  bindGroupDesc.entryCount = entries.size();
  bindGroupDesc.entries = entries.data();
  emscripten_console_logf("[WebGPU BindGroup] Creating with %zu entries, layout=%p", entries.size(),
                          static_cast<void*>(bindGroupDesc.layout));
  for (size_t i = 0; i < entries.size(); i++) {
    emscripten_console_logf(
        "[WebGPU BindGroup] Entry[%zu]: binding=%u buf=%p tv=%p samp=%p off=%zu sz=%zu", i,
        entries[i].binding, static_cast<void*>(entries[i].buffer),
        static_cast<void*>(entries[i].textureView), static_cast<void*>(entries[i].sampler),
        static_cast<size_t>(entries[i].offset), static_cast<size_t>(entries[i].size));
  }
  auto bindGroup = wgpuDeviceCreateBindGroup(_gpu->device(), &bindGroupDesc);
  emscripten_console_logf("[WebGPU BindGroup] Result: %p", static_cast<void*>(bindGroup));
  if (bindGroup != nullptr) {
    wgpuRenderPassEncoderSetBindGroup(passEncoder, 0, bindGroup, 0, nullptr);
    wgpuBindGroupRelease(bindGroup);
  }
}

void WebGPURenderPass::draw(PrimitiveType, uint32_t vertexCount, uint32_t instanceCount,
                            uint32_t firstVertex, uint32_t firstInstance) {
  if (passEncoder == nullptr || currentPipeline == nullptr) {
    return;
  }
  emscripten_console_logf("[WebGPU Draw] vertexCount=%u instanceCount=%u", vertexCount,
                          instanceCount);
  updateBindGroup();
  wgpuRenderPassEncoderDraw(passEncoder, vertexCount, instanceCount, firstVertex, firstInstance);
  emscripten_console_logf("[WebGPU Draw] Done");
}

void WebGPURenderPass::drawIndexed(PrimitiveType, uint32_t indexCount, uint32_t instanceCount,
                                   uint32_t firstIndex, int32_t baseVertex,
                                   uint32_t firstInstance) {
  if (passEncoder == nullptr || currentPipeline == nullptr) {
    return;
  }
  updateBindGroup();
  wgpuRenderPassEncoderDrawIndexed(passEncoder, indexCount, instanceCount, firstIndex, baseVertex,
                                   firstInstance);
}

void WebGPURenderPass::onEnd() {
  if (passEncoder != nullptr) {
    wgpuRenderPassEncoderEnd(passEncoder);
    wgpuRenderPassEncoderRelease(passEncoder);
    passEncoder = nullptr;
  }
}

}  // namespace tgfx

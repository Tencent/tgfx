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

#include "WebGPURenderPass.h"
#include "WebGPUGPU.h"
#include "core/utils/Log.h"

namespace tgfx {
WebGPURenderPass::WebGPURenderPass(WebGPUGPU* gpu, RenderPassDescriptor descriptor,
                                   wgpu::RenderPassEncoder passEncoder)
    : RenderPass(std::move(descriptor)), _gpu(gpu), _passEncoder(std::move(passEncoder)) {
}

GPU* WebGPURenderPass::gpu() const {
  return _gpu;
}

void WebGPURenderPass::setViewport(int x, int y, int width, int height) {
  _passEncoder.SetViewport(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width),
                           static_cast<float>(height), 0.0f, 1.0f);
}

void WebGPURenderPass::setScissorRect(int x, int y, int width, int height) {
  _passEncoder.SetScissorRect(static_cast<uint32_t>(x), static_cast<uint32_t>(y),
                              static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

void WebGPURenderPass::setPipeline(std::shared_ptr<RenderPipeline> pipeline) {
  if (renderPipeline == pipeline) {
    return;
  }
  renderPipeline = std::static_pointer_cast<WebGPURenderPipeline>(pipeline);
  if (renderPipeline != nullptr) {
    _passEncoder.SetPipeline(renderPipeline->wgpuPipeline());
  }
}

void WebGPURenderPass::setUniformBuffer(unsigned binding, std::shared_ptr<GPUBuffer> buffer,
                                        size_t offset, size_t size) {
  if (buffer == nullptr || !(buffer->usage() & GPUBufferUsage::UNIFORM)) {
    LOGE("WebGPURenderPass::setUniformBuffer() buffer is null or usage is not UNIFORM!");
    return;
  }
  auto webgpuBuffer = std::static_pointer_cast<WebGPUBuffer>(buffer);
  wgpu::BindGroupEntry entry = {};
  entry.binding = binding;
  entry.buffer = webgpuBuffer->wgpuBuffer();
  entry.offset = offset;
  entry.size = size;
  pendingBindGroupEntries[binding] = entry;
  bindGroupDirty = true;
}

void WebGPURenderPass::setTexture(unsigned binding, std::shared_ptr<Texture> texture,
                                  std::shared_ptr<Sampler> sampler) {
  if (texture == nullptr) {
    LOGE("WebGPURenderPass::setTexture() texture is null!");
    return;
  }
  auto webgpuTexture = std::static_pointer_cast<WebGPUTexture>(texture);
  auto webgpuSampler = std::static_pointer_cast<WebGPUSampler>(sampler);

  // Texture binding
  wgpu::BindGroupEntry textureEntry = {};
  textureEntry.binding = binding;
  textureEntry.textureView = webgpuTexture->createTextureView(
      wgpu::TextureViewDimension::e2D, 0, static_cast<uint32_t>(texture->mipLevelCount()));
  pendingBindGroupEntries[binding] = textureEntry;

  // Sampler binding (use binding + 100 to match the pipeline layout convention)
  if (webgpuSampler != nullptr) {
    wgpu::BindGroupEntry samplerEntry = {};
    samplerEntry.binding = binding + 100;
    samplerEntry.sampler = webgpuSampler->wgpuSampler();
    pendingBindGroupEntries[binding + 100] = samplerEntry;
  }
  bindGroupDirty = true;
}

void WebGPURenderPass::setVertexBuffer(std::shared_ptr<GPUBuffer> buffer, size_t offset) {
  if (buffer == nullptr) {
    return;
  }
  if (!(buffer->usage() & GPUBufferUsage::VERTEX)) {
    LOGE("WebGPURenderPass::setVertexBuffer() buffer usage is not VERTEX!");
    return;
  }
  auto webgpuBuffer = std::static_pointer_cast<WebGPUBuffer>(buffer);
  _passEncoder.SetVertexBuffer(0, webgpuBuffer->wgpuBuffer(), offset);
}

void WebGPURenderPass::setIndexBuffer(std::shared_ptr<GPUBuffer> buffer, IndexFormat format) {
  if (buffer == nullptr) {
    return;
  }
  if (!(buffer->usage() & GPUBufferUsage::INDEX)) {
    LOGE("WebGPURenderPass::setIndexBuffer() buffer usage is not INDEX!");
    return;
  }
  auto webgpuBuffer = std::static_pointer_cast<WebGPUBuffer>(buffer);
  wgpu::IndexFormat wgpuFormat =
      (format == IndexFormat::UInt16) ? wgpu::IndexFormat::Uint16 : wgpu::IndexFormat::Uint32;
  _passEncoder.SetIndexBuffer(webgpuBuffer->wgpuBuffer(), wgpuFormat);
}

void WebGPURenderPass::setStencilReference(uint32_t reference) {
  _passEncoder.SetStencilReference(reference);
}

void WebGPURenderPass::draw(PrimitiveType, size_t baseVertex, size_t vertexCount) {
  flushBindGroup();
  _passEncoder.Draw(static_cast<uint32_t>(vertexCount), 1, static_cast<uint32_t>(baseVertex), 0);
}

void WebGPURenderPass::drawIndexed(PrimitiveType, size_t baseIndex, size_t indexCount) {
  flushBindGroup();
  _passEncoder.DrawIndexed(static_cast<uint32_t>(indexCount), 1, static_cast<uint32_t>(baseIndex),
                           0, 0);
}

void WebGPURenderPass::onEnd() {
  _passEncoder.End();
}

void WebGPURenderPass::flushBindGroup() {
  if (!bindGroupDirty || renderPipeline == nullptr) {
    return;
  }

  std::vector<wgpu::BindGroupEntry> entries;
  entries.reserve(pendingBindGroupEntries.size());
  for (auto& [binding, entry] : pendingBindGroupEntries) {
    entries.push_back(entry);
  }

  wgpu::BindGroupDescriptor bindGroupDesc = {};
  bindGroupDesc.layout = renderPipeline->wgpuBindGroupLayout();
  bindGroupDesc.entryCount = entries.size();
  bindGroupDesc.entries = entries.data();

  wgpu::BindGroup bindGroup = _gpu->wgpuDevice().CreateBindGroup(&bindGroupDesc);
  _passEncoder.SetBindGroup(0, bindGroup);

  bindGroupDirty = false;
}

}  // namespace tgfx

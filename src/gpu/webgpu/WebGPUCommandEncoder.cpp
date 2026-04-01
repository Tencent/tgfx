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
#ifdef __EMSCRIPTEN__
#include <emscripten/console.h>
#endif

// WGSL shader for mipmap generation via fullscreen triangle + bilinear sampling.
static const char* MipmapWGSL = R"(
struct VertexOutput {
  @builtin(position) position : vec4f,
  @location(0) texCoord : vec2f,
};

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex : u32) -> VertexOutput {
  // Fullscreen triangle: 3 vertices cover the entire screen.
  var pos = array<vec2f, 3>(
    vec2f(-1.0, -3.0),
    vec2f(-1.0,  1.0),
    vec2f( 3.0,  1.0)
  );
  var uv = array<vec2f, 3>(
    vec2f(0.0, 2.0),
    vec2f(0.0, 0.0),
    vec2f(2.0, 0.0)
  );
  var output : VertexOutput;
  output.position = vec4f(pos[vertexIndex], 0.0, 1.0);
  output.texCoord = uv[vertexIndex];
  return output;
}

@group(0) @binding(0) var inputTexture : texture_2d<f32>;
@group(0) @binding(1) var inputSampler : sampler;

@fragment
fn fs_main(@location(0) texCoord : vec2f) -> @location(0) vec4f {
  return textureSample(inputTexture, inputSampler, texCoord);
}
)";

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

void WebGPUCommandEncoder::generateMipmapsForTexture(std::shared_ptr<Texture> texture) {
  if (commandEncoder == nullptr || texture == nullptr) {
    return;
  }
  auto webgpuTexture = std::static_pointer_cast<WebGPUTexture>(texture);
  auto mipLevelCount = webgpuTexture->mipLevelCount();
  if (mipLevelCount <= 1) {
    return;
  }
  auto device = _gpu->device();
  auto format = webgpuTexture->webgpuFormat();

  // Create WGSL shader module.
  WGPUShaderModuleWGSLDescriptor wgslDesc = {};
  wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
  wgslDesc.code = MipmapWGSL;
  WGPUShaderModuleDescriptor shaderDesc = {};
  shaderDesc.nextInChain = &wgslDesc.chain;
  auto shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);
  if (shaderModule == nullptr) {
    LOGE("generateMipmapsForTexture: failed to create mipmap shader module.");
    return;
  }

  // Create bind group layout: texture + sampler.
  WGPUBindGroupLayoutEntry bglEntries[2] = {};
  bglEntries[0].binding = 0;
  bglEntries[0].visibility = WGPUShaderStage_Fragment;
  bglEntries[0].texture.sampleType = WGPUTextureSampleType_Float;
  bglEntries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
  bglEntries[1].binding = 1;
  bglEntries[1].visibility = WGPUShaderStage_Fragment;
  bglEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;
  WGPUBindGroupLayoutDescriptor bglDesc = {};
  bglDesc.entryCount = 2;
  bglDesc.entries = bglEntries;
  auto bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);

  // Create pipeline layout.
  WGPUPipelineLayoutDescriptor plDesc = {};
  plDesc.bindGroupLayoutCount = 1;
  plDesc.bindGroupLayouts = &bindGroupLayout;
  auto pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDesc);

  // Create render pipeline.
  WGPUColorTargetState colorTarget = {};
  colorTarget.format = format;
  colorTarget.writeMask = WGPUColorWriteMask_All;
  WGPUFragmentState fragmentState = {};
  fragmentState.module = shaderModule;
  fragmentState.entryPoint = "fs_main";
  fragmentState.targetCount = 1;
  fragmentState.targets = &colorTarget;
  WGPURenderPipelineDescriptor rpDesc = {};
  rpDesc.layout = pipelineLayout;
  rpDesc.vertex.module = shaderModule;
  rpDesc.vertex.entryPoint = "vs_main";
  rpDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
  rpDesc.fragment = &fragmentState;
  rpDesc.multisample.count = 1;
  rpDesc.multisample.mask = 0xFFFFFFFF;
  auto pipeline = wgpuDeviceCreateRenderPipeline(device, &rpDesc);
  if (pipeline == nullptr) {
    LOGE("generateMipmapsForTexture: failed to create mipmap render pipeline.");
    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuBindGroupLayoutRelease(bindGroupLayout);
    wgpuShaderModuleRelease(shaderModule);
    return;
  }

  // Create a linear sampler for downsampling.
  WGPUSamplerDescriptor samplerDesc = {};
  samplerDesc.minFilter = WGPUFilterMode_Linear;
  samplerDesc.magFilter = WGPUFilterMode_Linear;
  samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
  samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
  samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
  auto sampler = wgpuDeviceCreateSampler(device, &samplerDesc);

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

    // Create destination view (current level, single mip).
    WGPUTextureViewDescriptor dstViewDesc = {};
    dstViewDesc.dimension = WGPUTextureViewDimension_2D;
    dstViewDesc.format = format;
    dstViewDesc.baseMipLevel = static_cast<uint32_t>(level);
    dstViewDesc.mipLevelCount = 1;
    dstViewDesc.baseArrayLayer = 0;
    dstViewDesc.arrayLayerCount = 1;
    auto dstView = wgpuTextureCreateView(webgpuTexture->webgpuTexture(), &dstViewDesc);

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

    wgpuRenderPassEncoderSetPipeline(passEncoder, pipeline);
    wgpuRenderPassEncoderSetBindGroup(passEncoder, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(passEncoder, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(passEncoder);

    wgpuRenderPassEncoderRelease(passEncoder);
    wgpuBindGroupRelease(bindGroup);
    wgpuTextureViewRelease(srcView);
    wgpuTextureViewRelease(dstView);
  }

  // Cleanup.
  wgpuSamplerRelease(sampler);
  wgpuRenderPipelineRelease(pipeline);
  wgpuPipelineLayoutRelease(pipelineLayout);
  wgpuBindGroupLayoutRelease(bindGroupLayout);
  wgpuShaderModuleRelease(shaderModule);
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

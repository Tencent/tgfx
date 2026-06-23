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

#include "WebGPUSampler.h"
#include "WebGPUGPU.h"
#include "WebGPUUtil.h"

namespace tgfx {

std::shared_ptr<WebGPUSampler> WebGPUSampler::Make(WebGPUGPU* gpu,
                                                   const SamplerDescriptor& descriptor) {
  if (gpu == nullptr) {
    return nullptr;
  }
  WGPUSamplerDescriptor samplerDesc = {};
  samplerDesc.addressModeU = ToWGPUAddressMode(descriptor.addressModeX);
  samplerDesc.addressModeV = ToWGPUAddressMode(descriptor.addressModeY);
  samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
  samplerDesc.minFilter = ToWGPUFilterMode(descriptor.minFilter);
  samplerDesc.magFilter = ToWGPUFilterMode(descriptor.magFilter);
  samplerDesc.mipmapFilter = ToWGPUMipmapFilterMode(descriptor.mipmapMode);
  samplerDesc.lodMinClamp = 0.0f;
  samplerDesc.lodMaxClamp = 32.0f;
  samplerDesc.maxAnisotropy = 1;
  auto sampler = wgpuDeviceCreateSampler(gpu->device(), &samplerDesc);
  if (sampler == nullptr) {
    return nullptr;
  }
  return gpu->makeResource<WebGPUSampler>(sampler);
}

WebGPUSampler::WebGPUSampler(WGPUSampler sampler) : sampler(sampler) {
}

void WebGPUSampler::onRelease(WebGPUGPU*) {
  if (sampler != nullptr) {
    wgpuSamplerRelease(sampler);
    sampler = nullptr;
  }
}

}  // namespace tgfx

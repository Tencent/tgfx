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

#include "D3D12Sampler.h"
#include "D3D12GPU.h"

namespace tgfx {

std::shared_ptr<D3D12Sampler> D3D12Sampler::Make(D3D12GPU* gpu,
                                                 const SamplerDescriptor& descriptor) {
  if (gpu == nullptr) {
    return nullptr;
  }

  D3D12_SAMPLER_DESC samplerDesc = {};
  samplerDesc.Filter =
      ToD3D12Filter(descriptor.minFilter, descriptor.magFilter, descriptor.mipmapMode);
  samplerDesc.AddressU = ToD3D12AddressMode(descriptor.addressModeX);
  samplerDesc.AddressV = ToD3D12AddressMode(descriptor.addressModeY);
  samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  samplerDesc.MipLODBias = 0.0f;
  samplerDesc.MaxAnisotropy = 1;
  samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  samplerDesc.BorderColor[0] = 0.0f;
  samplerDesc.BorderColor[1] = 0.0f;
  samplerDesc.BorderColor[2] = 0.0f;
  samplerDesc.BorderColor[3] = 0.0f;
  samplerDesc.MinLOD = 0.0f;
  // When mipmap is disabled, clamp MaxLOD to 0 so the hardware always samples mip 0. Picking a
  // D3D12_FILTER_*_MIP_POINT alone is not enough: the driver still walks the mip chain and may
  // pick a smaller level (the "mipmap-disabled" filters in D3D12 only describe the filter shape
  // used between mips, not whether mip selection happens). Mirror VulkanSampler's maxLod clamp
  // so a SamplerDescriptor with mipmapMode=None produces the same result that
  // RenderContext::drawImageRect (and other Strict-constraint paths) intend.
  samplerDesc.MaxLOD = (descriptor.mipmapMode == MipmapMode::None) ? 0.0f : D3D12_FLOAT32_MAX;

  // Permanently reserve a slot in the process-wide shader-visible Sampler heap and write the
  // descriptor there. The slot lives for the rest of the GPU's lifetime, mirroring the cache
  // semantics already enforced by D3D12GPU::createSampler.
  auto gpuHandle = gpu->allocatePermanentSamplerSlot(samplerDesc);
  if (gpuHandle.ptr == 0) {
    return nullptr;
  }

  return gpu->makeResource<D3D12Sampler>(samplerDesc, gpuHandle);
}

D3D12Sampler::D3D12Sampler(const D3D12_SAMPLER_DESC& samplerDesc,
                           D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
    : _samplerDesc(samplerDesc), _gpuHandle(gpuHandle) {
}

void D3D12Sampler::onRelease(D3D12GPU*) {
  // D3D12 samplers are pure descriptors. No GPU resource to release.
}

}  // namespace tgfx

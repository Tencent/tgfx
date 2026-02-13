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

#include "MtlSampler.h"
#include "MtlGPU.h"
#include "MtlDefines.h"

namespace tgfx {

std::shared_ptr<MtlSampler> MtlSampler::Make(MtlGPU* gpu, const SamplerDescriptor& descriptor) {
  if (!gpu) {
    return nullptr;
  }
  
  // Create Metal sampler descriptor
  MTLSamplerDescriptor* mtlDescriptor = [[MTLSamplerDescriptor alloc] init];
  
  // Set filtering
  mtlDescriptor.minFilter = MtlDefines::ToMTLSamplerFilter(descriptor.minFilter);
  mtlDescriptor.magFilter = MtlDefines::ToMTLSamplerFilter(descriptor.magFilter);
  mtlDescriptor.mipFilter = MtlDefines::ToMTLSamplerMipFilter(descriptor.mipmapMode);
  
  // Set address modes
  mtlDescriptor.sAddressMode = MtlDefines::ToMTLSamplerAddressMode(descriptor.addressModeX);
  mtlDescriptor.tAddressMode = MtlDefines::ToMTLSamplerAddressMode(descriptor.addressModeY);
  
  // Set border color (if supported)
  if (descriptor.addressModeX == AddressMode::ClampToBorder ||
      descriptor.addressModeY == AddressMode::ClampToBorder) {
    // Metal supports transparent black and opaque black/white border colors
    mtlDescriptor.borderColor = MTLSamplerBorderColorTransparentBlack;
  }
  
  // Create Metal sampler state
  id<MTLSamplerState> mtlSamplerState = [gpu->device() newSamplerStateWithDescriptor:mtlDescriptor];
  [mtlDescriptor release];
  if (!mtlSamplerState) {
    return nullptr;
  }
  
  return gpu->makeResource<MtlSampler>(mtlSamplerState);
}

MtlSampler::MtlSampler(id<MTLSamplerState> mtlSamplerState) : samplerState(mtlSamplerState) {
}

void MtlSampler::onRelease(MtlGPU*) {
  if (samplerState != nil) {
    [samplerState release];
    samplerState = nil;
  }
}

}  // namespace tgfx
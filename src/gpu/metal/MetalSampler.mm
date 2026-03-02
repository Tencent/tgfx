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

#include "MetalSampler.h"
#include "MetalDefines.h"
#include "MetalGPU.h"

namespace tgfx {

std::shared_ptr<MetalSampler> MetalSampler::Make(MetalGPU* gpu,
                                                 const SamplerDescriptor& descriptor) {
  if (!gpu) {
    return nullptr;
  }

  // Create Metal sampler descriptor
  MTLSamplerDescriptor* metalDescriptor = [[MTLSamplerDescriptor alloc] init];

  // Set filtering
  metalDescriptor.minFilter = MetalDefines::ToMTLSamplerFilter(descriptor.minFilter);
  metalDescriptor.magFilter = MetalDefines::ToMTLSamplerFilter(descriptor.magFilter);
  metalDescriptor.mipFilter = MetalDefines::ToMTLSamplerMipFilter(descriptor.mipmapMode);

  // Set address modes
  metalDescriptor.sAddressMode = MetalDefines::ToMTLSamplerAddressMode(descriptor.addressModeX);
  metalDescriptor.tAddressMode = MetalDefines::ToMTLSamplerAddressMode(descriptor.addressModeY);

  // Set border color (if supported)
  if (descriptor.addressModeX == AddressMode::ClampToBorder ||
      descriptor.addressModeY == AddressMode::ClampToBorder) {
    // Metal supports transparent black and opaque black/white border colors
    metalDescriptor.borderColor = MTLSamplerBorderColorTransparentBlack;
  }

  // Create Metal sampler state
  id<MTLSamplerState> metalSamplerState =
      [gpu->device() newSamplerStateWithDescriptor:metalDescriptor];
  [metalDescriptor release];
  if (!metalSamplerState) {
    return nullptr;
  }

  return gpu->makeResource<MetalSampler>(metalSamplerState);
}

MetalSampler::MetalSampler(id<MTLSamplerState> metalSamplerState)
    : samplerState(metalSamplerState) {
}

void MetalSampler::onRelease(MetalGPU*) {
  if (samplerState != nil) {
    [samplerState release];
    samplerState = nil;
  }
}

}  // namespace tgfx
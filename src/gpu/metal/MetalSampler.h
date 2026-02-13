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

#pragma once

#include <Metal/Metal.h>
#include "tgfx/gpu/Sampler.h"
#include "MetalResource.h"

namespace tgfx {

class MetalGPU;

/**
 * Metal sampler implementation.
 */
class MetalSampler : public Sampler, public MetalResource {
 public:
  static std::shared_ptr<MetalSampler> Make(MetalGPU* gpu, const SamplerDescriptor& descriptor);

  /**
   * Returns the Metal sampler state.
   */
  id<MTLSamplerState> metalSamplerState() const {
    return samplerState;
  }

 protected:
  void onRelease(MetalGPU* gpu) override;

 private:
  explicit MetalSampler(id<MTLSamplerState> metalSamplerState);
  ~MetalSampler() override = default;

  id<MTLSamplerState> samplerState = nil;
  
  friend class MetalGPU;
};

}  // namespace tgfx
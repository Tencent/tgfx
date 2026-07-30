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

#include <webgpu/webgpu.h>
#include "WebGPUResource.h"
#include "tgfx/gpu/Sampler.h"

namespace tgfx {

class WebGPUGPU;

class WebGPUSampler : public Sampler, public WebGPUResource {
 public:
  static std::shared_ptr<WebGPUSampler> Make(WebGPUGPU* gpu, const SamplerDescriptor& descriptor);

  WGPUSampler webgpuSampler() const {
    return sampler;
  }

  void onRelease(WebGPUGPU* gpu) override;

 private:
  explicit WebGPUSampler(WGPUSampler sampler);

  WGPUSampler sampler = nullptr;

  friend class WebGPUGPU;
};

}  // namespace tgfx

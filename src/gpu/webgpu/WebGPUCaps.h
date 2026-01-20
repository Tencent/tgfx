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

#include <emscripten/html5_webgpu.h>
#include <webgpu/webgpu_cpp.h>
#include "tgfx/gpu/GPUFeatures.h"
#include "tgfx/gpu/GPUInfo.h"
#include "tgfx/gpu/GPULimits.h"
#include "tgfx/gpu/PixelFormat.h"
#include "tgfx/gpu/Sampler.h"

namespace tgfx {
class WebGPUCaps {
 public:
  explicit WebGPUCaps(const wgpu::Adapter& adapter, const wgpu::Device& device);

  const GPUInfo* info() const {
    return &_info;
  }

  const GPUFeatures* features() const {
    return &_features;
  }

  const GPULimits* limits() const {
    return &_limits;
  }

  bool isFormatRenderable(PixelFormat pixelFormat) const;

  int getSampleCount(int requestedCount, PixelFormat pixelFormat) const;

  static wgpu::TextureFormat GetTextureFormat(PixelFormat pixelFormat);

  static wgpu::TextureUsage GetTextureUsage(uint32_t usage);

  static wgpu::AddressMode GetAddressMode(AddressMode mode);

  static wgpu::FilterMode GetFilterMode(FilterMode mode);

  static wgpu::MipmapFilterMode GetMipmapFilterMode(MipmapMode mode);

 private:
  GPUInfo _info = {};
  GPUFeatures _features = {};
  GPULimits _limits = {};
};
}  // namespace tgfx

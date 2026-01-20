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

#include "WebGPUCaps.h"

namespace tgfx {
static std::string ToStdString(const char* str) {
  return str ? std::string(str) : "";
}

static std::string FeatureNameToString(wgpu::FeatureName feature) {
  switch (feature) {
    case wgpu::FeatureName::DepthClipControl:
      return "DepthClipControl";
    case wgpu::FeatureName::Depth32FloatStencil8:
      return "Depth32FloatStencil8";
    case wgpu::FeatureName::TextureCompressionBC:
      return "TextureCompressionBC";
    case wgpu::FeatureName::TextureCompressionETC2:
      return "TextureCompressionETC2";
    case wgpu::FeatureName::TextureCompressionASTC:
      return "TextureCompressionASTC";
    case wgpu::FeatureName::IndirectFirstInstance:
      return "IndirectFirstInstance";
    case wgpu::FeatureName::ShaderF16:
      return "ShaderF16";
    case wgpu::FeatureName::RG11B10UfloatRenderable:
      return "RG11B10UfloatRenderable";
    case wgpu::FeatureName::BGRA8UnormStorage:
      return "BGRA8UnormStorage";
    case wgpu::FeatureName::Float32Filterable:
      return "Float32Filterable";
    default:
      return "Unknown";
  }
}

WebGPUCaps::WebGPUCaps(const wgpu::Adapter& adapter, const wgpu::Device& device) {
  // GPUInfo
  _info.backend = Backend::WebGPU;
  wgpu::AdapterInfo adapterInfo = {};
  adapter.GetInfo(&adapterInfo);
  _info.vendor = ToStdString(adapterInfo.vendor);
  // Use architecture as fallback when device is empty (common in browser WebGPU).
  _info.renderer = ToStdString(adapterInfo.device);
  if (_info.renderer.empty()) {
    _info.renderer = ToStdString(adapterInfo.architecture);
  }
  // Construct version string when description is empty (common in browser WebGPU).
  _info.version = ToStdString(adapterInfo.description);
  if (_info.version.empty() && !_info.vendor.empty()) {
    _info.version = "WebGPU (" + _info.vendor + ")";
  }

  auto featureCount = adapter.EnumerateFeatures(nullptr);
  if (featureCount > 0) {
    std::vector<wgpu::FeatureName> features(featureCount);
    adapter.EnumerateFeatures(features.data());
    for (auto feature : features) {
      auto name = FeatureNameToString(feature);
      if (name != "Unknown") {
        _info.extensions.emplace_back(name);
      }
    }
  }

  // GPUFeatures
  _features.semaphore = false;
  _features.textureBarrier = false;
  _features.clampToBorder = false;

  // GPULimits
  wgpu::SupportedLimits supportedLimits = {};
  device.GetLimits(&supportedLimits);
  _limits.maxTextureDimension2D = static_cast<int>(supportedLimits.limits.maxTextureDimension2D);
  _limits.maxSamplersPerShaderStage =
      static_cast<int>(supportedLimits.limits.maxSamplersPerShaderStage);
  _limits.maxUniformBufferBindingSize =
      static_cast<int>(supportedLimits.limits.maxUniformBufferBindingSize);
  _limits.minUniformBufferOffsetAlignment =
      static_cast<int>(supportedLimits.limits.minUniformBufferOffsetAlignment);
}

// WebGPU texture format capabilities:
// https://www.w3.org/TR/webgpu/#texture-format-caps
bool WebGPUCaps::isFormatRenderable(PixelFormat pixelFormat) const {
  switch (pixelFormat) {
    case PixelFormat::RGBA_8888:  // rgba8unorm
    case PixelFormat::BGRA_8888:  // bgra8unorm
    case PixelFormat::ALPHA_8:    // r8unorm
    case PixelFormat::GRAY_8:     // r8unorm
    case PixelFormat::RG_88:      // rg8unorm
      return true;
    default:
      break;
  }
  return false;
}

// WebGPU multisample state:
// https://www.w3.org/TR/webgpu/#multisample-state
// WebGPU only supports sample counts of 1 or 4.
int WebGPUCaps::getSampleCount(int requestedCount, PixelFormat pixelFormat) const {
  if (!isFormatRenderable(pixelFormat)) {
    return 1;
  }
  if (requestedCount <= 1) {
    return 1;
  }
  // WebGPU only supports 1 or 4 samples for multisampling.
  return 4;
}

}  // namespace tgfx

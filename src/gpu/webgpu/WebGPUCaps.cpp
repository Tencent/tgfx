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
#include "WebGPUUtil.h"

namespace tgfx {

WebGPUCaps::WebGPUCaps(WGPUDevice device) {
  initInfo(device);
  initFeatureSet(device);
  initLimits(device);
  initFormatTable(device);
}

void WebGPUCaps::initInfo(WGPUDevice) {
  gpuInfo.backend = Backend::WebGPU;
  gpuInfo.vendor = "WebGPU";
  gpuInfo.renderer = "WebGPU";
  gpuInfo.version = "1.0";
}

void WebGPUCaps::initFeatureSet(WGPUDevice) {
  gpuFeatures.semaphore = false;
  gpuFeatures.clampToBorder = false;
  gpuFeatures.textureBarrier = false;
}

void WebGPUCaps::initLimits(WGPUDevice) {
  // Emscripten's wgpuDeviceGetLimits crashes under ASSERTIONS=2 because Chrome's
  // GPUSupportedLimits may not include all property names that the Emscripten WebGPU
  // binding expects (e.g. maxBindGroupsPlusVertexBuffers), causing 'undefined' to be
  // written into the integer heap. Use conservative defaults instead.
  gpuLimits.maxTextureDimension2D = 8192;
  gpuLimits.maxSamplersPerShaderStage = 16;
  gpuLimits.maxUniformBufferBindingSize = 65536;
  gpuLimits.minUniformBufferOffsetAlignment = 256;
  gpuLimits.minBufferCopyRowAlignment = 256;
}

void WebGPUCaps::initFormatTable(WGPUDevice) {
  auto addFormat = [this](PixelFormat format, bool renderable, bool colorAttachment,
                          std::vector<int> sampleCounts) {
    WebGPUFormatInfo info = {};
    info.renderable = renderable;
    info.colorAttachment = colorAttachment;
    info.sampleCounts = std::move(sampleCounts);
    formatTable[format] = info;
  };
  addFormat(PixelFormat::ALPHA_8, true, true, {1, 4});
  addFormat(PixelFormat::GRAY_8, true, true, {1, 4});
  addFormat(PixelFormat::RG_88, true, true, {1, 4});
  addFormat(PixelFormat::RGBA_8888, true, true, {1, 4});
  addFormat(PixelFormat::BGRA_8888, true, true, {1, 4});
  addFormat(PixelFormat::DEPTH24_STENCIL8, true, false, {1, 4});
}

bool WebGPUCaps::isFormatRenderable(PixelFormat format) const {
  auto it = formatTable.find(format);
  if (it == formatTable.end()) {
    return false;
  }
  return it->second.renderable;
}

bool WebGPUCaps::isFormatAsColorAttachment(PixelFormat format) const {
  auto it = formatTable.find(format);
  if (it == formatTable.end()) {
    return false;
  }
  return it->second.colorAttachment;
}

int WebGPUCaps::getSampleCount(int requestedCount, PixelFormat format) const {
  auto it = formatTable.find(format);
  if (it == formatTable.end()) {
    return 1;
  }
  auto& sampleCounts = it->second.sampleCounts;
  for (auto count : sampleCounts) {
    if (count >= requestedCount) {
      return count;
    }
  }
  return 1;
}

WGPUTextureFormat WebGPUCaps::getWGPUTextureFormat(PixelFormat format) const {
  return ToWGPUTextureFormat(format);
}

}  // namespace tgfx

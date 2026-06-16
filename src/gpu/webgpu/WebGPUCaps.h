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
#include <memory>
#include <unordered_map>
#include "tgfx/gpu/GPUFeatures.h"
#include "tgfx/gpu/GPUInfo.h"
#include "tgfx/gpu/GPULimits.h"
#include "tgfx/gpu/PixelFormat.h"

namespace tgfx {

struct WebGPUFormatInfo {
  bool renderable = false;
  bool colorAttachment = false;
  std::vector<int> sampleCounts = {};
};

/**
 * WebGPU GPU capabilities.
 */
class WebGPUCaps {
 public:
  explicit WebGPUCaps(WGPUDevice device);

  const GPUInfo* info() const {
    return &gpuInfo;
  }

  const GPUFeatures* features() const {
    return &gpuFeatures;
  }

  const GPULimits* limits() const {
    return &gpuLimits;
  }

  bool isFormatRenderable(PixelFormat format) const;

  bool isFormatAsColorAttachment(PixelFormat format) const;

  int getSampleCount(int requestedCount, PixelFormat format) const;

  WGPUTextureFormat getWGPUTextureFormat(PixelFormat format) const;

 private:
  void initInfo(WGPUDevice device);
  void initFeatureSet(WGPUDevice device);
  void initLimits(WGPUDevice device);
  void initFormatTable(WGPUDevice device);
  void addFormat(PixelFormat format, bool renderable, bool colorAttachment,
                 std::vector<int> sampleCounts);

  GPUInfo gpuInfo = {};
  GPUFeatures gpuFeatures = {};
  GPULimits gpuLimits = {};
  std::unordered_map<PixelFormat, WebGPUFormatInfo> formatTable = {};
};

}  // namespace tgfx

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
#include <unordered_map>
#include <vector>
#include "tgfx/gpu/PixelFormat.h"
#include "tgfx/gpu/GPUInfo.h"
#include "tgfx/gpu/GPUFeatures.h"
#include "tgfx/gpu/GPULimits.h"

namespace tgfx {

/**
 * Metal GPU capabilities and limits.
 */
class MtlCaps {
 public:
  explicit MtlCaps(id<MTLDevice> device);

  const GPUInfo* info() const {
    return &_info;
  }

  const GPUFeatures* features() const {
    return &_features;
  }

  const GPULimits* limits() const {
    return &_limits;
  }

  bool isFormatRenderable(PixelFormat format) const;

  bool isFormatAsColorAttachment(PixelFormat format) const;

  int getSampleCount(int requestedCount, PixelFormat format) const;

  MTLPixelFormat getMTLPixelFormat(PixelFormat format) const;

  bool multisampleDisableSupport() const {
    return true;
  }

  bool frameBufferFetchSupport() const {
    return true;
  }

  bool textureRedSupport() const {
    return true;
  }

  bool clampToBorderSupport() const {
    return clampToBorderSupported;
  }

  bool npotTextureTileModeSupport() const {
    return true;
  }

  bool mipMapSupport() const {
    return true;
  }

  bool semaphoreSupport() const {
    return true;
  }

  bool bufferMapSupport() const {
    return true;
  }

  bool computeSupport() const {
    return computeSupported;
  }

 private:
  void initFeatureSet(id<MTLDevice> device);
  void initLimits(id<MTLDevice> device);
  void initFormatTable(id<MTLDevice> device);

  GPUInfo _info = {};
  GPUFeatures _features = {};
  GPULimits _limits = {};

  bool clampToBorderSupported = false;
  bool computeSupported = false;
  
  struct FormatInfo {
    MTLPixelFormat mtlFormat = MTLPixelFormatInvalid;
    bool renderable = false;
    bool colorAttachment = false;
    std::vector<int> sampleCounts = {};
  };
  
  std::unordered_map<PixelFormat, FormatInfo> formatTable = {};
};

}  // namespace tgfx
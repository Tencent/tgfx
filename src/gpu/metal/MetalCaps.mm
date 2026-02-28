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

#include "MetalCaps.h"

namespace tgfx {

MetalCaps::MetalCaps(id<MTLDevice> device) {
  // Initialize GPU info
  _info.backend = Backend::Metal;
  _info.vendor = "Apple";
  _info.renderer = [device.name UTF8String];
  _info.version = "Metal";

  initFeatureSet(device);
  initLimits(device);
  initFormatTable(device);
}

bool MetalCaps::isFormatRenderable(PixelFormat format) const {
  auto it = formatTable.find(format);
  return it != formatTable.end() && it->second.renderable;
}

bool MetalCaps::isFormatAsColorAttachment(PixelFormat format) const {
  auto it = formatTable.find(format);
  return it != formatTable.end() && it->second.colorAttachment;
}

int MetalCaps::getSampleCount(int requestedCount, PixelFormat format) const {
  if (requestedCount <= 1) {
    return 1;
  }
  auto it = formatTable.find(format);
  if (it == formatTable.end() || it->second.sampleCounts.empty()) {
    return 1;
  }
  // Find the smallest supported sample count that is >= requestedCount.
  // sampleCounts is sorted in ascending order (e.g. {1, 2, 4, 8}).
  for (int count : it->second.sampleCounts) {
    if (count >= requestedCount) {
      return count;
    }
  }
  return 1;
}

void MetalCaps::initFeatureSet(id<MTLDevice> device) {
  // Metal always supports semaphore (MTLEvent) synchronization
  _features.semaphore = true;

  // Check for clamp to border support
#if TARGET_OS_OSX
  if (@available(macOS 10.15, *)) {
    clampToBorderSupported = [device supportsFamily:MTLGPUFamilyMac2];
  } else {
    clampToBorderSupported = false;
  }
#else
  if (@available(iOS 13.0, *)) {
    clampToBorderSupported = [device supportsFamily:MTLGPUFamilyApple4];
  } else {
    clampToBorderSupported = false;
  }
#endif

  _features.clampToBorder = clampToBorderSupported;

  // Check for compute support
  computeSupported = true;  // Metal always supports compute shaders
}

void MetalCaps::initLimits(id<MTLDevice> device) {
  // Set texture size limits
#if TARGET_OS_OSX
  (void)device;
  _limits.maxTextureDimension2D = 16384;
#else
  if (@available(iOS 13.0, *)) {
    if ([device supportsFamily:MTLGPUFamilyApple3]) {
      _limits.maxTextureDimension2D = 16384;
    } else {
      _limits.maxTextureDimension2D = 4096;
    }
  } else {
    _limits.maxTextureDimension2D = 4096;
  }
#endif

  // Set other limits
  _limits.maxSamplersPerShaderStage = 16;         // Metal supports up to 16 samplers per stage
  _limits.maxUniformBufferBindingSize = 65536;    // 64KB
  _limits.minUniformBufferOffsetAlignment = 256;  // Metal requirement
}

MetalCaps::FormatInfo MetalCaps::CheckFormat(id<MTLDevice> device, MTLPixelFormat metalFormat) {
  FormatInfo info = {};
  info.metalFormat = metalFormat;
  switch (metalFormat) {
    case MTLPixelFormatRGBA8Unorm:
    case MTLPixelFormatBGRA8Unorm:
    case MTLPixelFormatR8Unorm:
    case MTLPixelFormatRG8Unorm:
      info.renderable = true;
      info.colorAttachment = true;
      break;
#if TARGET_OS_OSX
    case MTLPixelFormatDepth24Unorm_Stencil8:
#endif
    case MTLPixelFormatDepth32Float_Stencil8:
      info.renderable = true;
      info.colorAttachment = false;
      break;
    default:
      info.renderable = false;
      info.colorAttachment = false;
      break;
  }
  if (info.renderable) {
    for (int count : {1, 2, 4, 8}) {
      if ([device supportsTextureSampleCount:static_cast<NSUInteger>(count)]) {
        info.sampleCounts.push_back(count);
      }
    }
  } else {
    info.sampleCounts = {1};
  }
  return info;
}

void MetalCaps::initFormatTable(id<MTLDevice> device) {
  // Initialize color formats.
  formatTable[PixelFormat::ALPHA_8] = CheckFormat(device, MTLPixelFormatR8Unorm);
  formatTable[PixelFormat::GRAY_8] = CheckFormat(device, MTLPixelFormatR8Unorm);
  formatTable[PixelFormat::RG_88] = CheckFormat(device, MTLPixelFormatRG8Unorm);
  formatTable[PixelFormat::RGBA_8888] = CheckFormat(device, MTLPixelFormatRGBA8Unorm);
  formatTable[PixelFormat::BGRA_8888] = CheckFormat(device, MTLPixelFormatBGRA8Unorm);

  // Depth-stencil format depends on device capabilities.
#if TARGET_OS_OSX
  MTLPixelFormat depthStencilFormat = [device isDepth24Stencil8PixelFormatSupported]
                                          ? MTLPixelFormatDepth24Unorm_Stencil8
                                          : MTLPixelFormatDepth32Float_Stencil8;
#else
  MTLPixelFormat depthStencilFormat = MTLPixelFormatDepth32Float_Stencil8;
#endif
  formatTable[PixelFormat::DEPTH24_STENCIL8] = CheckFormat(device, depthStencilFormat);
}

MTLPixelFormat MetalCaps::getMTLPixelFormat(PixelFormat format) const {
  auto it = formatTable.find(format);
  if (it != formatTable.end()) {
    return it->second.metalFormat;
  }
  return MTLPixelFormatInvalid;
}

}  // namespace tgfx
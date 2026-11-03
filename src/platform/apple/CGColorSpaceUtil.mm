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

#include "CGColorSpaceUtil.h"
#include "core/utils/Log.h"

namespace tgfx {

// Returns the named system color space for the well-known gamuts, or nullptr if the color space
// is a custom one that has no system equivalent.
static CGColorSpaceRef CreateNamedColorSpace(const ColorSpace* colorSpace) {
  if (colorSpace == nullptr) {
    return nullptr;
  }
  if (ColorSpace::Equals(colorSpace, ColorSpace::SRGB().get())) {
    return CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  }
  if (ColorSpace::Equals(colorSpace, ColorSpace::SRGBLinear().get())) {
    if (@available(macOS 10.14, iOS 12.0, *)) {
      return CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB);
    }
    return nullptr;
  }
  if (ColorSpace::Equals(colorSpace, ColorSpace::DisplayP3().get())) {
    return CGColorSpaceCreateWithName(kCGColorSpaceDisplayP3);
  }
  return nullptr;
}

CGColorSpaceRef CreateCGColorSpace(const std::shared_ptr<ColorSpace>& colorSpace) {
  // Prefer the named system color spaces. Core Animation hands a layer's color space over to the
  // window server by name, and an unnamed ICC-based space does not survive that handover, so the
  // compositor falls back to the default space and interprets wide-gamut pixels as sRGB.
  if (auto namedColorSpace = CreateNamedColorSpace(colorSpace.get())) {
    return namedColorSpace;
  }
  if (colorSpace != nullptr) {
    if (auto iccData = colorSpace->toICCProfile()) {
      if (CFDataRef cfData =
              CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(iccData->data()),
                           static_cast<CFIndex>(iccData->size()))) {
        CGColorSpaceRef cgColorSpace = CGColorSpaceCreateWithICCData(cfData);
        CFRelease(cfData);
        if (cgColorSpace != nullptr) {
          return cgColorSpace;
        }
      }
    }
  }
  // ColorSpace::toICCProfile() only writes the TRC tags for sRGB-like transfer functions, so color
  // spaces with a linear or gamma 2.2 curve produce an invalid profile and end up here.
  LOGE("CreateCGColorSpace() failed to build a color space, falling back to sRGB. Gamuts other "
       "than sRGB, sRGB Linear and Display P3 are not supported on this platform.");
  return CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
}
}  // namespace tgfx

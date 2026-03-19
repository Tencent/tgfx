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

namespace tgfx {
CGColorSpaceRef CreateCGColorSpace(const std::shared_ptr<ColorSpace>& colorSpace) {
  if (colorSpace != nullptr) {
    if (auto iccData = colorSpace->toICCProfile()) {
      if (CFDataRef cfData =
              CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(iccData->data()),
                           static_cast<CFIndex>(iccData->size()))) {
        CGColorSpaceRef cgColorSpace = nullptr;
        if (@available(iOS 10.0, macOS 10.12, *)) {
          cgColorSpace = CGColorSpaceCreateWithICCData(cfData);
        } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
          cgColorSpace = CGColorSpaceCreateWithICCProfile(cfData);
#pragma clang diagnostic pop
        }
        CFRelease(cfData);
        if (cgColorSpace != nullptr) {
          return cgColorSpace;
        }
      }
    }
  }
  return CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
}
}  // namespace tgfx

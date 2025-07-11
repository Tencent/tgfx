/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "ExtendedTexture.h"
#include "core/utils/PixelFormatUtil.h"

namespace tgfx {
ExtendedTexture::ExtendedTexture(std::unique_ptr<TextureSampler> sampler, int width, int height,
                                 int extendedWidth, int extendedHeight, ImageOrigin origin)
    : DefaultTexture(std::move(sampler), width, height, origin), extendedWidth(extendedWidth),
      extendedHeight(extendedHeight) {
}

size_t ExtendedTexture::memoryUsage() const {
  if (auto hardwareBuffer = _sampler->getHardwareBuffer()) {
    return HardwareBufferGetInfo(hardwareBuffer).byteSize();
  }
  auto colorSize = static_cast<size_t>(extendedWidth) * static_cast<size_t>(extendedHeight) *
                   PixelFormatBytesPerPixel(_sampler->format());
  return _sampler->hasMipmaps() ? colorSize * 4 / 3 : colorSize;
}

}  // namespace tgfx

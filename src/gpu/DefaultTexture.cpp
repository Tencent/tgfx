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

#include "DefaultTexture.h"
#include "core/utils/PixelFormatUtil.h"

namespace tgfx {
DefaultTexture::DefaultTexture(std::unique_ptr<TextureSampler> sampler, int width, int height,
                               ImageOrigin origin)
    : Texture(width, height, origin), _sampler(std::move(sampler)) {
}

size_t DefaultTexture::memoryUsage() const {
  if (auto hardwareBuffer = _sampler->getHardwareBuffer()) {
    return HardwareBufferGetInfo(hardwareBuffer).byteSize();
  }
  auto colorSize = static_cast<size_t>(_width) * static_cast<size_t>(_height) *
                   PixelFormatBytesPerPixel(_sampler->format());
  return _sampler->hasMipmaps() ? colorSize * 4 / 3 : colorSize;
}
}  // namespace tgfx

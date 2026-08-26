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

#include "HardwareBufferUtil.h"
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace tgfx {
ImageInfo GetImageInfo(HardwareBufferRef hardwareBuffer, std::shared_ptr<ColorSpace> colorSpace) {
  auto info = HardwareBufferGetInfo(hardwareBuffer);
  ColorType colorType = ColorType::Unknown;
  switch (info.format) {
    case HardwareBufferFormat::ALPHA_8:
      colorType = ColorType::ALPHA_8;
      break;
    case HardwareBufferFormat::RGBA_8888:
      colorType = ColorType::RGBA_8888;
      break;
    case HardwareBufferFormat::BGRA_8888:
      colorType = ColorType::BGRA_8888;
      break;
    default:
      return {};
  }
  return ImageInfo::Make(info.width, info.height, colorType, AlphaType::Premultiplied,
                         info.rowBytes, std::move(colorSpace));
}

PixelFormat GetRenderableFormat(HardwareBufferFormat hardwareBufferFormat, Backend backend) {
  switch (hardwareBufferFormat) {
    case HardwareBufferFormat::ALPHA_8:
      return PixelFormat::ALPHA_8;
    case HardwareBufferFormat::RGBA_8888:
      return PixelFormat::RGBA_8888;
    case HardwareBufferFormat::BGRA_8888:
#if TARGET_OS_MAC && !TARGET_OS_IPHONE
      // On macOS OpenGL imports BGRA hardware buffers as RGBA_8888 textures, while Metal imports
      // them as BGRA_8888. Keep the renderable format consistent with the backend to avoid a
      // proxy/texture mismatch that swaps red and blue in advanced blend modes.
      return backend == Backend::Metal ? PixelFormat::BGRA_8888 : PixelFormat::RGBA_8888;
#else
      return PixelFormat::BGRA_8888;
#endif
    default:
      break;
  }
  return PixelFormat::Unknown;
}
}  // namespace tgfx

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

#pragma once

#include "tgfx/core/ImageInfo.h"
#include "tgfx/gpu/PixelFormat.h"
#include "tgfx/platform/HardwareBuffer.h"

namespace tgfx {
/**
 * Returns an ImageInfo describing the specified HardwareBufferRef if recognized; otherwise returns
 * an empty ImageInfo.
 */
ImageInfo GetImageInfo(HardwareBufferRef hardwareBuffer,
                       std::shared_ptr<ColorSpace> colorSpace = nullptr);

/**
 * Returns the corresponding PixelFormat for the given HardwareBufferFormat if renderable; otherwise
 * returns PixelFormat::Unknown.
 */
PixelFormat GetRenderableFormat(HardwareBufferFormat hardwareBufferFormat);
}  // namespace tgfx

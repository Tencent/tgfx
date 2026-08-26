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
#include "tgfx/gpu/Backend.h"
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
 * returns PixelFormat::Unknown. The backend is required because a BGRA hardware buffer maps to
 * different renderable formats per backend on macOS: the OpenGL (CGL) backend imports it as an
 * RGBA_8888 texture, while the Metal backend imports it as a genuine BGRA_8888 texture. Passing the
 * wrong backend leads to a proxy/texture format mismatch that swaps red and blue in the
 * destination-texture copy used by advanced blend modes.
 */
PixelFormat GetRenderableFormat(HardwareBufferFormat hardwareBufferFormat, Backend backend);
}  // namespace tgfx

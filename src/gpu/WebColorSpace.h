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

#include <memory>
#include "tgfx/core/ColorSpace.h"

namespace tgfx {
/**
 * Mirrors the WindowColorSpace enum defined in the web binding layer (see web/src/types.ts). It is
 * used to communicate the target color space to the JavaScript side when configuring a WebGL or
 * WebGPU drawing buffer. The integer values must stay in sync with the TypeScript definition.
 */
enum class WebNamedColorSpace { None = 0, SRGB = 1, DisplayP3 = 2, Others = 3 };

/**
 * Maps a tgfx ColorSpace to its corresponding WebNamedColorSpace. Returns None when colorSpace is
 * nullptr (the default sRGB), SRGB or DisplayP3 for the matching named color spaces, and Others for
 * any color space that is neither sRGB nor Display P3.
 */
WebNamedColorSpace ToWebNamedColorSpace(const std::shared_ptr<ColorSpace>& colorSpace);
}  // namespace tgfx

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

#include "tgfx/core/ColorSpace.h"

namespace tgfx {
/**
 * Mirrors the WindowColorSpace enum defined in the web binding layer (web/src/types.ts). This is
 * the single authoritative definition on the C++ side; the integer values must stay in sync with
 * the TypeScript definition, which cannot share this header across the language boundary.
 */
enum class WebNamedColorSpace { None = 0, SRGB = 1, DisplayP3 = 2, Others = 3 };

/**
 * Maps a ColorSpace to its WebNamedColorSpace counterpart. A null colorSpace maps to None (the
 * default sRGB drawing buffer), sRGB and Display P3 map to their named values, and any other color
 * space maps to Others.
 */
inline WebNamedColorSpace ToWebNamedColorSpace(const std::shared_ptr<ColorSpace>& colorSpace) {
  if (colorSpace == nullptr) {
    return WebNamedColorSpace::None;
  }
  if (ColorSpace::Equals(colorSpace.get(), ColorSpace::SRGB().get())) {
    return WebNamedColorSpace::SRGB;
  }
  if (ColorSpace::Equals(colorSpace.get(), ColorSpace::DisplayP3().get())) {
    return WebNamedColorSpace::DisplayP3;
  }
  return WebNamedColorSpace::Others;
}
}  // namespace tgfx

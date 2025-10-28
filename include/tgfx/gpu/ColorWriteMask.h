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

#include <cinttypes>

namespace tgfx {
/**
 * Values used to specify a mask to permit or restrict writing to color channels of a color value.
 */
class ColorWriteMask {
 public:
  /**
   * The red color channel is enabled.
   */
  static constexpr uint32_t RED = 0x1;

  /**
   * The green color channel is enabled.
   */
  static constexpr uint32_t GREEN = 0x2;

  /**
   * The blue color channel is enabled.
   */
  static constexpr uint32_t BLUE = 0x4;

  /**
   * The alpha color channel is enabled.
   */
  static constexpr uint32_t ALPHA = 0x8;

  /**
   * All color channels are enabled.
   */
  static constexpr uint32_t All = 0xF;
};
}  // namespace tgfx

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

namespace tgfx {
/**
 * Defines the horizontal alignment of a text.
 */
enum class TextAlign {
  /**
   * The same as left if direction is left-to-right and right if direction is right-to-left.
   */
  Start,

  /**
   * The same as right if direction is left-to-right and left if direction is right-to-left.
   */
  End,

  /**
   * Text is centered within the available region.
   */
  Center,

  /**
   * Text is justified. Text is spaced to line up its left and right edges to the left and right
   * edges of the available region.
   */
  Justify
};
}  // namespace tgfx

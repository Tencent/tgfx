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
   * Text is aligned to the start of the available region.
   */
  Start,

  /**
   * Text is aligned to the end of the available region.
   */
  End,

  /**
   * Text is visually center aligned.
   */
  Center,

  /**
   * Text is justified.
   */
  Justify
};
}  // namespace tgfx

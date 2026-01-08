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

namespace tgfx {
/**
 * SrcRectConstraint controls the behavior at the edge of source rect, provided to drawImageRect()
 * when there is any filtering. If Strict is set, then extra code is used to ensure it never samples
 * outside the src-rect. Strict disables the use of mipmaps.
 */
enum class SrcRectConstraint {
  /**
   * sample only inside bounds; slower
   */
  Strict,
  /**
   * sample outside bounds; faster
   */
  Fast,
};
}  // namespace tgfx

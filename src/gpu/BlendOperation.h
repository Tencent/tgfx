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

namespace tgfx {
/**
 * BlendOperation defines how each pixel's source fragment values are combined and weighted with the
 * destination values.
 */
enum class BlendOperation {
  /**
   * Add portions of both source and destination pixel values:
   * Cs*S + Cd*D.
   */
  Add,

  /**
   * Subtract a portion of the destination pixel values from a portion of the source:
   * Cs*S - Cd*D.
   */
  Subtract,

  /**
   * Subtract a portion of the source values from a portion of the destination pixel values:
   * Cd*D - Cs*S.
   */
  ReverseSubtract,

  /**
   * Take the minimum of the source and destination pixel values:
   * min(Cs*S, Cd*D).
   */
  Min,

  /**
   * Take the maximum of the source and destination pixel values:
   * max(Cs*S, Cd*D).
   */
  Max
};
}  // namespace tgfx

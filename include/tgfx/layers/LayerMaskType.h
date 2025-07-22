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
 * LayerMaskType defines the type of mask to be used for a layer.
 */
enum class LayerMaskType {
  /**
   * Uses the target layer's transparency as a mask.
   */
  Alpha,
  /**
   * Uses the target layer's contour as a mask.
   */
  Contour,
  /**
   * Uses the target layer's luminance as a mask.
   */
  Luminance
};
}  // namespace tgfx

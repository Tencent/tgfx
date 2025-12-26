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
 * Defines the transform style of a layer.
 */
enum class TransformStyle {
  /**
   * Does not preserve the 3D state of the layer's content and child layers. Content and child
   * layers with 3D transformations are directly projected onto the current layer plane. In this
   * mode, later added layers will cover earlier added layers.
   */
  Flat,

  /**
   * Preserves the 3D state of the layer's content and child layers. In this mode, content and child
   * layers will be occluded based on the actual depth of pixels, meaning opaque pixels closer to
   * the observer will cover those farther away.
   */
  Preserve3D
};
}  // namespace tgfx

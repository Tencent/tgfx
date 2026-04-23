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
 * FillSpace specifies the coordinate space used by a ColorSource when applied to a geometry. It
 * controls whether the fill parameters (such as gradient stops or pattern size) are interpreted
 * in the layer's coordinate space or in a normalized 0-1 space that maps to each geometry's
 * bounding box.
 */
enum class FillSpace {
  /**
   * Fill parameters are interpreted in the layer's coordinate space using absolute values.
   * Multiple geometries using the same ColorSource will share a single continuous fill across the
   * layer.
   */
  Absolute,

  /**
   * Fill parameters are interpreted relative to each geometry's bounding box, where (0, 0) is the
   * top-left corner and (1, 1) is the bottom-right. The fill automatically adapts to each
   * geometry's size and position. This is the default value.
   */
  Relative
};

}  // namespace tgfx

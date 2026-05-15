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
 * ScaleMode specifies how an ImagePattern image is fitted into each geometry's bounding box.
 */
enum class ScaleMode {
  /**
   * The image is not fitted to each geometry's bounding box. It is placed in the parent
   * container's (VectorLayer or VectorGroup) coordinate space and tiled according to the
   * pattern's tile modes. Use this mode when multiple geometries should share a single
   * continuous image layout rather than each one receiving its own fitted copy.
   */
  None,

  /**
   * The image is stretched to fill the bounding box, ignoring its original aspect ratio.
   */
  Stretch,

  /**
   * The image is scaled uniformly to fit entirely inside the bounding box while preserving its
   * aspect ratio. The image is centered and empty areas may appear along one axis. This is the
   * default value.
   */
  LetterBox,

  /**
   * The image is scaled uniformly to cover the entire bounding box while preserving its aspect
   * ratio. The image is centered and may be cropped along one axis.
   */
  Zoom
};

}  // namespace tgfx

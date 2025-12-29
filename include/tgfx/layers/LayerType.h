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
 * Defines the types of a layer.
 */
enum class LayerType {
  /**
   * The type for a generic layer. May be used as a container for other child layers.
   */
  Layer,
  /**
   * A layer displaying a simple image.
   */
  Image,
  /**
   * A layer displaying a simple shape.
   */
  Shape,
  /**
   * A layer displaying a simple text.
   */
  Text,
  /**
   * A layer that fills its bounds with a solid color.
   */
  Solid
};
}  // namespace tgfx

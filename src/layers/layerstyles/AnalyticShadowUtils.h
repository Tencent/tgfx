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

#include <optional>
#include "tgfx/core/RRect.h"
#include "tgfx/layers/layerstyles/LayerStyleInput.h"

namespace tgfx {

class AnalyticShadowUtils {
 public:
  /**
   * Returns the geometry used to evaluate a closed-form shadow, or nullopt when a closed-form
   * shadow is not possible for the given input. The returned shape already has the spread applied:
   * positive values expand it, negative values shrink it. A shrinking spread can collapse the shape
   * to nothing; that case returns an empty RRect rather than nullopt.
   *
   * The returned RRect is never complex, so all four corners share one radius pair and either entry
   * of radii() describes the shape. It is expressed in the content image's coordinate system:
   * scaled by contentScale, with the origin at the image's top-left corner.
   */
  static std::optional<RRect> MakeShadowShape(const LayerStyleInput& input, float spread);
};

}  // namespace tgfx

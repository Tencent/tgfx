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

#include <memory>
#include "tgfx/core/Image.h"

namespace tgfx {

/**
 * Generates highlight and inner shadow maps for the glass lighting effect. The highlight map
 * encodes specular reflection intensity at each pixel based on surface normal alignment with the
 * light direction. The inner shadow map encodes directional shadow/highlight for edges facing
 * toward or away from the light.
 */
class GlassHighlightMap {
 public:
  struct Result {
    std::shared_ptr<Image> highlight;    // White specular highlight (alpha varies)
    std::shared_ptr<Image> innerShadow;  // Inner shadow/highlight (RGBA with direction)
  };

  /**
   * Generates highlight and inner shadow maps.
   * @param width The width of the output images in pixels.
   * @param height The height of the output images in pixels.
   * @param cornerRadius The corner radius of the rounded rectangle shape in pixels.
   * @param depth The inward extent of the lighting region from edges in pixels.
   * @param splay Controls highlight spread. Range [0, 100]. Higher = broader, softer highlights.
   * @param lightAngle The light direction in degrees. Range [0, 360].
   * @param intensity The highlight opacity. Range [0, 1].
   * @return A Result containing highlight and innerShadow images, or nullptrs if generation fails.
   */
  static Result Generate(int width, int height, float cornerRadius, float depth, float splay,
                         float lightAngle, float intensity);
};

}  // namespace tgfx

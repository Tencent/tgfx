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
 * Generates a displacement map image for the glass refraction effect. The displacement map encodes
 * per-pixel UV offset values derived from a rounded-rectangle SDF, a squircle surface function,
 * and Snell's Law of refraction.
 *
 * Encoding: R channel = horizontal offset, G channel = vertical offset.
 * A value of 128 (0.5 normalized) means zero displacement. Values above/below 128 indicate
 * positive/negative displacement respectively.
 */
class GlassDisplacementMap {
 public:
  /**
   * Generates a displacement map image.
   * @param width The width of the output image in pixels.
   * @param height The height of the output image in pixels.
   * @param cornerRadius The corner radius of the rounded rectangle shape in pixels.
   * @param depth The inward extent of the refraction region from edges in pixels.
   * @param ior The index of refraction (1.0 = no refraction, higher = more distortion).
   * @return A displacement map image, or nullptr if generation fails.
   */
  static std::shared_ptr<Image> Generate(int width, int height, float cornerRadius, float depth,
                                         float ior, float* outMaxDisp = nullptr);
};

}  // namespace tgfx

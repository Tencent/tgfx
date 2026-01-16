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

#include <memory>
#include "tgfx/core/Path.h"
#include "tgfx/core/Shape.h"
namespace tgfx {

class ShapeUtils {
 public:
  /**
   * Returns the Shape adjusted for the current resolution scale.
   * Used during rendering to decide whether to simplify the Path or apply hairline stroking,
   * depending on the resolution scale.
   */
  static Path GetShapeRenderingPath(std::shared_ptr<Shape> shape, float resolutionScale);

  static float CalculateAlphaReduceFactorIfHairline(std::shared_ptr<Shape> shape);
};
}  // namespace tgfx

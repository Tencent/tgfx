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
#include "tgfx/core/Paint.h"
#include "tgfx/core/Shape.h"
#include "tgfx/layers/StrokeAlign.h"

namespace tgfx {

/**
 * StyledShape describes a vector shape along with its style attributes (e.g. fill or stroke, stroke
 * width).
 */
struct StyledShape {
  /**
   * Builds a StyledShape from the given shape and fill/stroke information.
   */
  static StyledShape Make(std::shared_ptr<Shape> shape, bool hasFill, bool hasStroke,
                          float strokeWidth, StrokeAlign strokeAlign);

  /**
   * The original vector shape.
   */
  std::shared_ptr<Shape> shape = nullptr;

  /**
   * Whether the shape represents a fill or a stroke.
   */
  PaintStyle style = PaintStyle::Fill;

  /**
   * The stroke width. Ignored when style is Fill.
   */
  float strokeWidth = 0.0f;

  /**
   * The stroke alignment. Ignored when style is Fill.
   */
  StrokeAlign strokeAlign = StrokeAlign::Center;

  /**
   * The outset distance from the shape path when style is Fill. The actual visible boundary is the
   * shape path expanded outward by this amount. Ignored when style is Stroke.
   */
  float fillOutset = 0.0f;
};

}  // namespace tgfx

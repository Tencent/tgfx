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
#include "tgfx/core/Shape.h"
#include "tgfx/layers/StrokeAlign.h"

namespace tgfx {

/**
 * The drawing type of a StyledShape.
 */
enum class StyledShapeType {
  /**
   * Fill only.
   */
  Fill,
  /**
   * Stroke only.
   */
  Stroke,
  /**
   * Both fill and stroke.
   */
  FillStroke
};

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
   * The drawing type of this shape.
   */
  StyledShapeType type = StyledShapeType::Fill;

  /**
   * The stroke width. Valid when type is Stroke or FillStroke.
   */
  float strokeWidth = 0.0f;

  /**
   * The stroke alignment. Valid when type is Stroke or FillStroke.
   */
  StrokeAlign strokeAlign = StrokeAlign::Center;
};

}  // namespace tgfx

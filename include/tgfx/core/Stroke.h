/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "tgfx/core/Path.h"

namespace tgfx {
/**
 * LineCap draws at the beginning and end of an open path contour.
 */
enum class LineCap {
  /**
   * No stroke extension.
   */
  Butt,
  /**
   * Adds circle
   */
  Round,
  /**
   * Adds square
   */
  Square
};

/**
 * LineJoin specifies how corners are drawn when a shape is stroked. Join affects the four corners
 * of a stroked rectangle, and the connected segments in a stroked path. Choose miter join to draw
 * sharp corners. Choose round join to draw a circle with a radius equal to the stroke width on
 * top of the corner. Choose bevel join to minimally connect the thick strokes.
 */
enum class LineJoin {
  /**
   * Extends to miter limit.
   */
  Miter,
  /**
   * Adds circle.
   */
  Round,
  /**
   * Connects outside edges.
   */
  Bevel
};

/**
 * Stroke controls options applied when stroking geometries (paths, glyphs).
 */
class Stroke {
 public:
  Stroke() = default;

  /**
   * Creates a new Stroke width specified options.
   */
  explicit Stroke(float width, LineCap cap = LineCap::Butt, LineJoin join = LineJoin::Miter,
                  float miterLimit = 4.0f)
      : width(width), cap(cap), join(join), miterLimit(miterLimit) {
  }

  /**
   * Applies the stroke options to the given path.
   * @param path The path to which the stroke options will be applied.
   * @param resolutionScale The intended resolution for the output. The default value is 1.0.
   * Higher values (res > 1) mean the result should be more precise, as it will be zoomed up and
   * small errors will be magnified. Lower values (0 < res < 1) mean the result can be less precise,
   * as it will be zoomed down and small errors may be invisible.
   * @return false if the stroke cannot be applied, leaving the path unchanged.
   */
  bool applyToPath(Path* path, float resolutionScale = 1.0f) const;

  /**
   * The thickness of the pen used to outline the paths or glyphs.
   */
  float width = 1.0f;

  /**
   *  The geometry drawn at the beginning and end of strokes.
   */
  LineCap cap = LineCap::Butt;

  /**
   * The geometry drawn at the corners of strokes.
   */
  LineJoin join = LineJoin::Miter;

  /**
   * The limit at which a sharp corner is drawn beveled.
   */
  float miterLimit = 4.0f;

  bool operator==(const Stroke& other) const {
    return width == other.width && cap == other.cap && join == other.join &&
           miterLimit == other.miterLimit;
  }

  bool operator!=(const Stroke& other) const {
    return !(*this == other);
  }
};
}  // namespace tgfx

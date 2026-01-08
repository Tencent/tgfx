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
 * PartialStroke defines a subset of stroke parameters for a specific vertex on the path.
 * Unlike the complete Stroke class which includes width, PartialStroke only specifies
 * the cap, join, and miterLimit for per-vertex styling.
 */
struct PartialStroke {
  PartialStroke() = default;

  PartialStroke(LineCap lineCap, LineJoin lineJoin, float inputMiterLimit = 4.0f)
      : cap(lineCap), join(lineJoin), miterLimit(inputMiterLimit) {
  }

  explicit PartialStroke(LineCap lineCap) : cap(lineCap) {
  }

  explicit PartialStroke(LineJoin lineJoin) : join(lineJoin) {
  }

  /**
   * The geometry drawn at the beginning and end of strokes.
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
   * Applies stroke with per-vertex parameters to a path. Each vertex can have different stroke
   * parameters (cap, join, miter limit). If params are fewer than path vertices, they will be
   * cycled through.
   *
   * Notes:
   * 1. Vertices at the same position are treated as separate vertices. For example:
   *      path.moveTo(25, 100);
   *      path.cubicTo(50, 80, 100, 80, 125, 100);
   *      path.cubicTo(100, 120, 50, 120, 25, 100);
   *    This path requires three PartialStrokes.
   *
   * 2. For closed contours, the last vertex connects to the first vertex, forming a corner.
   *    The join style at this corner is determined by the PartialStroke of the first vertex.
   *    For example:
   *      path.moveTo(25, 100);                       // Round join applies here
   *      path.cubicTo(50, 80, 100, 80, 125, 100);
   *      path.cubicTo(100, 120, 50, 120, 25, 100);   // Bevel join
   *      path.close();
   *    The corner where the end meets the start will use the Round join from the first vertex.
   */
  static bool StrokePathPerVertex(Path* path, float width,
                                  const std::vector<PartialStroke>& params,
                                  float resolutionScale = 1.0f);

  /**
   * Applies dash effect and stroke with per-vertex parameters to a path. This method combines
   * dash pattern application with per-vertex stroke parameter control.
   *
   * Notes:
   * 1. The dash effect is applied first, creating new path segments
   * 2. Original stroke parameters are intelligently mapped to dash segments based on position
   * 3. Segments that fall between original vertices use the defaultParam
   * 4. The final stroke uses the mapped parameters for each dash segment
   */
  static bool StrokeDashPathPerVertex(Path* path, float width,
                                      const std::vector<PartialStroke>& params,
                                      const PartialStroke& defaultParam,
                                      const float intervals[], int count, float phase,
                                      float resolutionScale = 1.0f);

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

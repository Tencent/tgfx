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

#include "tgfx/core/Stroke.h"
namespace tgfx {

class PathStroker {
 public:
  struct PointParam {
    PointParam() = default;

    /**
     * Creates a new PointParam with specified options.
     */
    PointParam(LineCap lineCap, LineJoin lineJoin, float inputMiterLimit = 4.0f)
        : cap(lineCap), join(lineJoin), miterLimit(inputMiterLimit) {
    }

    explicit PointParam(LineCap lineCap) : cap(lineCap) {
    }

    explicit PointParam(LineJoin lineJoin) : join(lineJoin) {
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
   * Applies stroke with multiple parameters to a path. Each point can have different stroke
   * parameters (cap, join, miter limit). If params are fewer than path points, they will be cycled
   * through.
   *
   * Notes:
   * 1. Points at the same position are treated as separate points. For example:
   *      path.moveTo(25, 100);
   *      path.cubicTo(50, 80, 100, 80, 125, 100);
   *      path.cubicTo(100, 120, 50, 120, 25, 100);
   *    This path requires three PointParams.
   *
   * 2. For closed contours, the last point connects to the first point, forming a corner. The join
   *    style at this corner is determined by the PointParam of the first point. For example:
   *      path.moveTo(25, 100);                       // Round join applies here
   *      path.cubicTo(50, 80, 100, 80, 125, 100);
   *      path.cubicTo(100, 120, 50, 120, 25, 100);   // Bevel join
   *      path.close();
   *    The corner where the end meets the start will use the Round join from the first point.
   */
  static bool StrokePathWithMultiParams(Path* path, float width,
                                        const std::vector<PointParam>& params,
                                        float resolutionScale = 1.0f);
};

}  // namespace tgfx
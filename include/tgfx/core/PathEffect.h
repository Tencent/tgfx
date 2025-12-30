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
#include "tgfx/core/Stroke.h"

namespace tgfx {
/**
 * PathEffect applies transformation to a Path.
 */
class PathEffect {
 public:
  /**
   * Creates a dash path effect.
   * @param intervals array containing an even number of entries (>=2), with the even indices
   * specifying the length of "on" intervals, and the odd indices specifying the length of "off"
   * intervals.
   * @param count number of elements in the interval array
   * @param phase offset into the interval array (mod the sum of all intervals).
   * @param adaptive decides whether to scale the dash intervals so that the dash segments have
   * the same length.
   */
  static std::shared_ptr<PathEffect> MakeDash(const float intervals[], int count, float phase,
                                              bool adaptive = false);

  /**
   * Create a corner path effect.
   * @param radius  must be > 0 to have an effect. It specifies the distance from each corner that
   * should be "rounded".
   */
  static std::shared_ptr<PathEffect> MakeCorner(float radius);

  /**
   * Creates a path effect that returns a subset of the input path based on the given startT and
   * stopT values. The trim values apply to the entire path, so if it contains several contours,
   * all of them are included in the calculation. The startT and stopT values must be in the range
   * of 0 to 1 (inclusive). If they are outside this range, they will be clamped to the nearest
   * valid value. Returns nullptr if either value is NaN, or if the result would be the full path
   * (no trimming needed).
   * @param startT The starting "t" value for the segment.
   * @param stopT The ending "t" value for the segment.
   * @param inverted If false (default), returns the subset [startT, stopT]. If true, returns the
   * complement [0, startT] + [stopT, 1].
   */
  static std::shared_ptr<PathEffect> MakeTrim(float startT, float stopT, bool inverted = false);

  virtual ~PathEffect() = default;

  /**
   * Applies this effect to the given path. Returns false if this effect cannot be applied, and
   * leaves this path unchanged.
   */
  virtual bool filterPath(Path* path) const = 0;

  /**
   * Returns the bounds of the path after applying this effect.
   */
  virtual Rect filterBounds(const Rect& rect) const {
    return rect;
  }
};
}  // namespace tgfx
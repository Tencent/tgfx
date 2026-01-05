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
   * Creates a path effect that extracts a segment from the input path. The segment is defined by
   * start and end positions, where 0 represents the beginning and 1 represents the end of the path.
   * Values outside [0, 1] are allowed and will wrap around cyclically. If the path contains multiple
   * contours, all of them are included in the length calculation. When end < start, the resulting
   * path will be reversed. If the range crosses the start point of a closed contour, the result
   * remains seamlessly connected without a gap.
   * @param start The starting position of the segment.
   * @param end The ending position of the segment.
   * @return Returns nullptr if start or end is NaN, or if the result would be the full path (no
   * trimming needed).
   */
  static std::shared_ptr<PathEffect> MakeTrim(float start, float end);

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
/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
   * @param phase  offset into the interval array (mod the sum of all of the intervals).
   */
  static std::shared_ptr<PathEffect> MakeDash(const float intervals[], int count, float phase);

  /**
   * Create a corner path effect.
   * @param radius  must be > 0 to have an effect. It specifies the distance from each corner that
   * should be "rounded".
   */
  static std::shared_ptr<PathEffect> MakeCorner(float radius);

  /**
   * Creates a path effect that returns a segment of the input path based on the given start and
   * stop "t" values. The startT and stopT values must be between 0 and 1, inclusive. If they are
   * outside this range, they will be clamped to the nearest valid value. If either value is NaN,
   * nullptr will be returned.
   * @param startT The starting point of the path segment to be returned.
   * @param stopT The ending point of the path segment to be returned.
   */
  static std::shared_ptr<PathEffect> MakeTrim(float startT, float stopT);

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
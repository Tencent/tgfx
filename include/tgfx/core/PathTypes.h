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

#include "tgfx/core/Point.h"

namespace tgfx {
/**
 * PathFillType selects the rule used to fill Path.
 */
enum class PathFillType {
  /**
   * Enclosed by a non-zero sum of contour directions.
   */
  Winding,
  /**
   * Enclosed by an odd number of contours.
   */
  EvenOdd,
  /**
   * Enclosed by a zero sum of contour directions.
   */
  InverseWinding,
  /**
   * Enclosed by an even number of contours.
   */
  InverseEvenOdd,
};

/**
 * The logical operations that can be performed when combining two paths.
 */
enum class PathOp {
  /**
   * Appended to destination unaltered.
   */
  Append,
  /**
   * Appended with a line connecting the end of the destination to the start of the source.
   */
  Extend,
  /**
   * Subtract the op path from the destination path.
   */
  Difference,
  /**
   * Intersect the two paths.
   */
  Intersect,
  /**
   * Union (inclusive-or) the two paths.
   */
  Union,
  /**
   * Exclusive-or the two paths.
   */
  XOR,
};

/**
 * PathVerb instructs Path how to interpret one or more Point, manage contour, and terminate Path.
 */
enum class PathVerb {
  /**
   * PathIterator returns 1 point.
   */
  Move,
  /**
   * PathIterator returns 2 points.
   */
  Line,
  /**
   * PathIterator returns 3 points.
   */
  Quad,
  /**
   * PathIterator returns 3 points and a weight.
   */
  Conic,
  /**
   * PathIterator returns 4 points.
   */
  Cubic,
  /**
   * PathIterator returns 0 points.
   */
  Close,
  /**
   * Iteration is complete, no more verbs to return.
   */
  Done
};

/**
 * Specify whether the arc is greater than 180 degrees pair or less than 180 degrees pair.
 */
enum class PathArcSize {
  /**
   * smaller of arc pair
   */
  Small,
  /**
   * larger of arc pair
   */
  Large,
};

}  // namespace tgfx

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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
 * Measures and manipulates path segments (linear or curved connections between points).
 * Provides capabilities to:
 * - Iterate through individual segments of a path
 * - Measure segment lengths
 * - Extract sub-segments by distance ranges
 */
class PathSegmentMeasure {
 public:
  /**
   * Initialize the PathSegmentMeasure with the specified path.
   */
  static std::shared_ptr<PathSegmentMeasure> MakeFrom(const Path& path);

  /**
   * Gets the contour is closed.
   */
  virtual bool isClosed() = 0;

  /**
   * Resets the contour's iterator to the beginning of the path.
   */
  virtual void resetContour() = 0;

  /**
   * Moves to the next contour in iteration sequence.
   * return true if next contour exists, false at end of path
   */
  virtual bool nextContour() = 0;

  /**
   * Resets the segment's iterator to the beginning of the contour.
   */
  virtual void resetSegement() = 0;

  /**
   * Moves to the next segment in iteration sequence.
   * return true if next segment exists, false at end of contour
   */
  virtual bool nextSegment() = 0;

  /**
   * Gets the length of the current active segment.
   */
  virtual float getSegmentLength() = 0;

  /**
   * Extracts a sub-segment between specified distances.
   * Returns false if startD > stopD without modifying output path
   * @param startD Start distance from segment origin (clamped to valid range)
   * @param stopD End distance from segment origin (clamped to valid range)
   * @param forceMoveTo If true, the sub-segment will always start with a moveTo
   * @param[out] path Receives the extracted sub-segment
   */
  virtual bool getSegment(float startD, float stopD, bool forceMoveTo, Path* path) = 0;

  virtual ~PathSegmentMeasure() = default;
};
}  // namespace tgfx

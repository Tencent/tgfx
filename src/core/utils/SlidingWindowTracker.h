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

#include <cstddef>
#include <deque>

namespace tgfx {
/**
 * SlidingWindowTracker is a utility class that tracks the maximum and average values of a sliding
 * window of a specified size. It is useful for monitoring performance metrics over time, such as
 * memory usage, frame rates, or other resource consumption metrics.
 */
class SlidingWindowTracker {
 public:
  SlidingWindowTracker(size_t windowSize);

  void addValue(size_t value);

  /**
   * Returns the maximum value in the sliding window. If the window is empty, returns 0.
   */
  size_t getMaxValue() const;

  /**
   * Returns the average value in the sliding window. If the window is empty, returns 0.
   */
  size_t getAverageValue() const;

 private:
  size_t windowSize = 0;
  std::deque<size_t> values = {};
};

}  // namespace tgfx

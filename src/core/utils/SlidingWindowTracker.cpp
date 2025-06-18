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

#include "SlidingWindowTracker.h"

namespace tgfx {

SlidingWindowTracker::SlidingWindowTracker(size_t windowSize) : windowSize(windowSize) {
}

void SlidingWindowTracker::addValue(size_t value) {
  values.push_back(value);
  if (values.size() > windowSize) {
    values.pop_front();
  }
}

size_t SlidingWindowTracker::getMaxValue() const {
  size_t maxValue = 0;
  for (auto value : values) {
    if (value > maxValue) {
      maxValue = value;
    }
  }
  return maxValue;
}

size_t SlidingWindowTracker::getAverageValue() const {
  if (values.empty()) {
    return 0;
  }
  size_t sum = 0;
  for (auto value : values) {
    sum += value;
  }
  return sum / values.size();
}

}  // namespace tgfx

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

#include "MaxValueTracker.h"

namespace tgfx {

MaxValueTracker::MaxValueTracker(size_t maxSize) : _maxSize(maxSize) {
}

void MaxValueTracker::addValue(size_t value) {
  _values.push_back(value);
  if (_values.size() > _maxSize) {
    _values.pop_front();
  }
}

size_t MaxValueTracker::getMaxValue() const {
  size_t maxValue = 0;
  for (size_t value : _values) {
    if (value > maxValue) {
      maxValue = value;
    }
  }
  return maxValue;
}

}  // namespace tgfx

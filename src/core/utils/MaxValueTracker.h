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
 * A tracker that keeps track of the maximum value of the last N values added.
 */
class MaxValueTracker {
 public:
  MaxValueTracker(size_t maxSize);

  void addValue(size_t value);

  size_t getMaxValue() const;

 private:
  size_t _maxSize;
  std::deque<size_t> _values;
};

}  // namespace tgfx

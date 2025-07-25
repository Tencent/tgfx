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

#include <chrono>
#include <string>
#include <unordered_map>

namespace tgfx {
/**
 * A utility class used to compute relative time.
 */
class Clock {
 public:
  /**
   * Returns the number of microseconds since the TGFX runtime was initialized.
   */
  static int64_t Now();

  /**
   * Creates a new Clock object.
   */
  Clock();

  /**
   * Resets the start time of the Clock to Now() and clear all markers.
   */
  void reset();

  /**
   * Returns the number of microseconds elapsed since the Clock was initialized or reset.
   */
  int64_t elapsedTime();

  /**
   * Creates a timestamp in the Clock with the given name. If the specified name already exists,
   * throws an error and leaves the markers unchanged.
   */
  void mark(const std::string& name);

  /**
   * Returns the number of microseconds from the 'makerFrom' to the 'makerTo'.
   * - If the 'makerTo' is empty, returns the number of microseconds from the 'makerFrom' to now.
   * - If the 'makerFrom' is empty, returns the number of microseconds from the time when the Clock
   * was initialized to the 'makerTo'.
   * - If both of the markers are empty, returns the number of microseconds since the Clock was
   * initialized.
   * - If either of the markers does not exist, throws an error and returns 0.
   */
  int64_t measure(const std::string& makerFrom = "", const std::string& markerTo = "") const;

 private:
  int64_t startTime = 0;
  std::unordered_map<std::string, int64_t> markers = {};
};
}  // namespace tgfx

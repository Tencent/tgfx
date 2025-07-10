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

#include <atomic>
#include "core/utils/UniqueID.h"

namespace tgfx {
class UniqueDomain {
 public:
  UniqueDomain();

  /**
   * Returns a global unique ID for the UniqueDomain.
   */
  uint32_t uniqueID() const {
    return _uniqueID;
  }

  /**
   * Returns the total number of times the UniqueDomain has been referenced.
   */
  long useCount() const;

  /**
   * Returns the number of times the UniqueDomain has been referenced strongly.
   */
  long strongCount() const;

  /**
   * Increments the number of times the UniqueDomain has been referenced.
   */
  void addReference();

  /**
   * Decrements the number of times the UniqueDomain has been referenced.
   */
  void releaseReference();

  /**
   * Increments the number of times the UniqueDomain has been referenced strongly.
   */
  void addStrong();

  /**
   * Decrements the number of times the UniqueDomain has been referenced strongly.
   */
  void releaseStrong();

 private:
  uint32_t _uniqueID = 0;
  std::atomic<long> _useCount = {1};
  std::atomic<long> _strongCount = {0};
};
}  // namespace tgfx

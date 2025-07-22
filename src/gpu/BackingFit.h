/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

namespace tgfx {
/**
 * Indicates whether a backing store needs to be an exact match or can be larger than is strictly
 * necessary.
*/
enum class BackingFit {
  /**
   * The backing store must be an exact match for the requested size.
   */
  Exact = 0,

  /**
   * The backing store can be larger than the requested size, but should not be smaller.
   */
  Approx = 1,
};
}  // namespace tgfx

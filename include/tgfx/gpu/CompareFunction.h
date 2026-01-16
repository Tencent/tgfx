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
 * Options used to specify how a sample compare operation should be performed on a depth or stencil
 * texture.
 */
enum class CompareFunction {
  /**
   * A new value never passes the comparison test.
   */
  Never,

  /**
   * A new value passes the comparison test if it is less than the existing value.
   */
  Less,

  /**
   * A new value passes the comparison test if it is equal to the existing value.
   */
  Equal,

  /**
   * A new value passes the comparison test if it is less than or equal to the existing value.
   */
  LessEqual,

  /**
   * A new value passes the comparison test if it is greater than the existing value.
   */
  Greater,

  /**
   * A new value passes the comparison test if it is not equal to the existing value.
   */
  NotEqual,

  /**
   * A new value passes the comparison test if it is greater than or equal to the existing value.
   */
  GreaterEqual,

  /**
   * A new value always passes the comparison test.
   */
  Always
};
}  // namespace tgfx

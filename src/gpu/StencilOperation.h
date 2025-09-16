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
 * The operation performed on a currently stored stencil value when a comparison test passes or
 * fails.
 */
enum class StencilOperation {
  /**
   * Keep the current stencil value.
   */
  Keep,

  /**
   * Set the stencil value to zero.
   */
  Zero,

  /**
   * Replace the stencil value with the stencil reference value, which is set by the
   * RenderPass::setStencilReference() method.
   */
  Replace,

  /**
   * Perform a logical bitwise invert operation on the current stencil value.
   */
  Invert,

  /**
   * If the current stencil value is not the maximum representable value, increase the stencil value
   * by one. Otherwise, if the current stencil value is the maximum representable value, do not
   * change the stencil value.
   */
  IncrementClamp,

  /**
   * If the current stencil value is not zero, decrease the stencil value by one. Otherwise, if the
   * current stencil value is zero, do not change the stencil value.
   */
  DecrementClamp,

  /**
   * If the current stencil value is not the maximum representable value, increase the stencil value
   * by one. Otherwise, if the current stencil value is the maximum representable value, set the
   * stencil value to zero.
   */
  IncrementWrap,

  /**
   * If the current stencil value is not zero, decrease the stencil value by one. Otherwise, if the
   * current stencil value is zero, set the stencil value to the maximum representable value.
   */
  DecrementWrap
};
}  // namespace tgfx

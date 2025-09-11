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
 * The source and destination blend factors are often needed to complete specification of a blend
 * operation.
 */
enum class BlendFactor {
  /**
   * Blend factor of zero.
   */
  Zero,

  /**
   * Blend factor of one.
   */
  One,

  /**
   * Blend factor of source values.
   */
  Src,

  /**
   * Blend factor of one minus source values.
   */
  OneMinusSrc,

  /**
   * Blend factor of destination values.
   */
  Dst,

  /**
   * Blend factor of one minus destination values.
   */
  OneMinusDst,

  /**
   * Blend factor of source alpha.
   */
  SrcAlpha,

  /**
   * Blend factor of one minus source alpha.
   */
  OneMinusSrcAlpha,

  /**
   * Blend factor of destination alpha.
   */
  DstAlpha,

  /**
   * Blend factor of one minus destination alpha.
   */
  OneMinusDstAlpha,

  /**
   * Blend factor of source values. This option supports dual-source blending and reads from the
   * second color output of the fragment function.
   */
  Src1,

  /**
   * Blend factor of one minus source values. This option supports dual-source blending and reads
   * from the second color output of the fragment function.
   */
  OneMinusSrc1,

  /**
   * Blend factor of source alpha. This option supports dual-source blending and reads from the
   * second color output of the fragment function.
   */
  Src1Alpha,

  /**
   * Blend factor of one minus source alpha. This option supports dual-source blending and reads
   * from the second color output of the fragment function.
   */
  OneMinusSrc1Alpha
};
}  // namespace tgfx

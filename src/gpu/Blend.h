/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/BlendMode.h"

namespace tgfx {
enum class BlendModeCoeff {
  /**
   * 0
   */
  Zero,
  /**
   * 1
   */
  One,
  /**
   * src color
   */
  SC,
  /**
   * inverse src color (i.e. 1 - sc)
   */
  ISC,
  /**
   * dst color
   */
  DC,
  /**
   * inverse dst color (i.e. 1 - dc)
   */
  IDC,
  /**
   * src alpha
   */
  SA,
  /**
   * inverse src alpha (i.e. 1 - sa)
   */
  ISA,
  /**
   * dst alpha
   */
  DA,
  /**
   * inverse dst alpha (i.e. 1 - da)
   */
  IDA,
};

struct BlendInfo {
  BlendModeCoeff srcBlend;
  BlendModeCoeff dstBlend;
};

/**
 * Returns true if 'mode' is a coefficient-based blend mode. If true is returned, the mode's src and
 * dst coefficient functions are set in 'src' and 'dst'.
 */
bool BlendModeAsCoeff(BlendMode mode, BlendInfo* blendInfo = nullptr);
}  // namespace tgfx

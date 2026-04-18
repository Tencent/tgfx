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
#include "UniformData.h"
#include "core/ColorSpaceXformSteps.h"

namespace tgfx {
class ColorSpaceXformHelper {
 public:
  ColorSpaceXformHelper() = default;

  void setData(UniformData* uniformData, const ColorSpaceXformSteps* colorSpaceXform);

 private:
  bool applyUnpremul() const {
    return flags.unPremul;
  }
  bool applySrcTF() const {
    return flags.linearize;
  }
  bool applySrcOOTF() const {
    return flags.srcOOTF;
  }
  bool applyGamutXform() const {
    return flags.gamutTransform;
  }
  bool applyDstOOTF() const {
    return flags.dstOOTF;
  }
  bool applyDstTF() const {
    return flags.encode;
  }
  bool applyPremul() const {
    return flags.premul;
  }

  ColorSpaceXformSteps::Flags flags;
};
}  // namespace tgfx

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

#include <cstdint>
#include "tgfx/core/ColorSpace.h"

namespace tgfx {

enum class AlphaType;

struct ColorSpaceXformSteps {

  struct Flags {
    bool unPremul = false;
    bool linearize = false;
    bool srcOOTF = false;
    bool gamutTransform = false;
    bool dstOOTF = false;
    bool encode = false;
    bool premul = false;

    constexpr uint32_t mask() const {
      return (unPremul ? 1 : 0) | (linearize ? 2 : 0) | (gamutTransform ? 4 : 0) |
             (encode ? 8 : 0) | (premul ? 16 : 0) | (srcOOTF ? 32 : 0) | (dstOOTF ? 64 : 0);
    }
  };

  ColorSpaceXformSteps() {
  }
  ColorSpaceXformSteps(const ColorSpace* src, AlphaType srcAT, const ColorSpace* dst,
                       AlphaType dstAT);

  template <typename S, typename D>
  ColorSpaceXformSteps(const S& src, const D& dst)
      : ColorSpaceXformSteps(src.colorSpace(), src.alphaType(), dst.colorSpace(), dst.alphaType()) {
  }

  void apply(float rgba[4]) const;

  static uint32_t xformKey(const ColorSpaceXformSteps* xform);

  Flags fFlags;

  TransferFunction fSrcTF,   // Apply for linearize.
      fDstTFInv;             // Apply for encode.
  float fSrcToDstMatrix[9];  // Apply this 3x3 *column*-major matrix for gamut_transform.
  float fSrcOotf[4];         // Apply ootf with these r,g,b coefficients and gamma before
  // gamut_transform.
  float fDstOotf[4];  // Apply ootf with these r,g,b coefficients and gamma after
  // gamut_transform.
};

}  // namespace tgfx
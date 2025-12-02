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
#include "tgfx/core/Color.h"
#include "tgfx/core/ColorSpace.h"

namespace tgfx {

enum class AlphaType;

struct ColorSpaceXformSteps {

  struct Flags {
    uint8_t unPremul : 1;
    uint8_t linearize : 1;
    uint8_t srcOOTF : 1;
    uint8_t gamutTransform : 1;
    uint8_t dstOOTF : 1;
    uint8_t encode : 1;
    uint8_t premul : 1;

    constexpr uint32_t mask() const {
      return static_cast<uint32_t>((unPremul) | (linearize << 1) | (gamutTransform << 2) |
                                   (encode << 3) | (premul << 4) | (srcOOTF << 5) | (dstOOTF << 6));
    }
    constexpr Flags()
        : unPremul(0), linearize(0), srcOOTF(0), gamutTransform(0), dstOOTF(0), encode(0),
          premul(0) {
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

  static uint32_t XFormKey(const ColorSpaceXformSteps* xform);

  Flags flags;

  /**
   * Apply for linearize.
   */
  TransferFunction srcTransferFunction;

  /**
   * Apply for encode.
   */
  TransferFunction dstTransferFunctionInverse;

  /**
   * Apply this 3x3 matrix for gamut_transform.
   */
  ColorMatrix33 srcToDstMatrix;

  /**
   * Apply ootf with these r,g,b coefficients and gamma before gamut_transform..
   */
  float srcOOTF[4];

  /**
   * Apply ootf with these r,g,b coefficients and gamma after gamut_transform.
   */
  float dstOOTF[4];

  uint64_t srcHash;
  uint64_t dstHash;
};

}  // namespace tgfx
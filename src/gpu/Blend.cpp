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

#include "Blend.h"
#include <utility>

namespace tgfx {
// clang-format off
static constexpr std::pair<BlendModeCoeff, BlendModeCoeff> Coeffs[] = {
  // For Porter-Duff blend functions, color = src * src coeff + dst * dst coeff
  // src coeff               dst coeff                  blend func
  // -------------------     --------------------       ----------
  { BlendModeCoeff::Zero,    BlendModeCoeff::Zero }, // clear
  { BlendModeCoeff::One,     BlendModeCoeff::Zero }, // src
  { BlendModeCoeff::Zero,    BlendModeCoeff::One  }, // dst
  { BlendModeCoeff::One,     BlendModeCoeff::ISA  }, // src-over
  { BlendModeCoeff::IDA,     BlendModeCoeff::One  }, // dst-over
  { BlendModeCoeff::DA,      BlendModeCoeff::Zero }, // src-in
  { BlendModeCoeff::Zero,    BlendModeCoeff::SA   }, // dst-in
  { BlendModeCoeff::IDA,     BlendModeCoeff::Zero }, // src-out
  { BlendModeCoeff::Zero,    BlendModeCoeff::ISA  }, // dst-out
  { BlendModeCoeff::DA,      BlendModeCoeff::ISA  }, // src-atop
  { BlendModeCoeff::IDA,     BlendModeCoeff::SA   }, // dst-atop
  { BlendModeCoeff::IDA,     BlendModeCoeff::ISA  }, // xor
  { BlendModeCoeff::One,     BlendModeCoeff::One  }, // plus
  { BlendModeCoeff::Zero,    BlendModeCoeff::SC   }, // modulate
  { BlendModeCoeff::One,     BlendModeCoeff::ISC  }, // screen
};
// clang-format on

bool BlendModeAsCoeff(BlendMode mode, BlendInfo* blendInfo) {
  if (mode > BlendMode::Screen) {
    return false;
  }
  if (blendInfo != nullptr) {
    blendInfo->srcBlend = Coeffs[static_cast<int>(mode)].first;
    blendInfo->dstBlend = Coeffs[static_cast<int>(mode)].second;
  }
  return true;
}

bool BlendModeIsOpaque(BlendMode mode, OpacityType srcColorOpacity) {
  BlendInfo blendInfo = {};
  if (!BlendModeAsCoeff(mode, &blendInfo)) {
    return false;
  }
  switch (blendInfo.srcBlend) {
    case BlendModeCoeff::DA:
    case BlendModeCoeff::DC:
    case BlendModeCoeff::IDA:
    case BlendModeCoeff::IDC:
      return false;
    default:
      break;
  }
  switch (blendInfo.dstBlend) {
    case BlendModeCoeff::Zero:
      return true;
    case BlendModeCoeff::ISA:
      return srcColorOpacity == OpacityType::Opaque;
    case BlendModeCoeff::SA:
      return srcColorOpacity == OpacityType::TransparentBlack ||
             srcColorOpacity == OpacityType::TransparentAlpha;
    case BlendModeCoeff::SC:
      return srcColorOpacity == OpacityType::TransparentBlack;
    default:
      return false;
  }
}
}  // namespace tgfx

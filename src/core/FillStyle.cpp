/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "FillStyle.h"
#include "gpu/Blend.h"

namespace tgfx {
enum class OpacityType {
  // The opacity is unknown
  Unknown,
  // The src color is known to be opaque (alpha == 255)
  Opaque,
  // The src color is known to be fully transparent (color == 0)
  TransparentBlack,
  // The src alpha is known to be fully transparent (alpha == 0)
  TransparentAlpha,
};

static OpacityType GetOpacityType(const Color& color, const Shader* shader) {
  auto alpha = color.alpha;
  if (alpha == 1.0f && (!shader || shader->isOpaque())) {
    return OpacityType::Opaque;
  }
  if (alpha == 0.0f) {
    if (shader || color.red != 0.0f || color.green != 0.0f || color.blue != 0.0f) {
      return OpacityType::TransparentAlpha;
    }
    return OpacityType::TransparentBlack;
  }
  return OpacityType::Unknown;
}

static bool BlendModeIsOpaque(BlendMode mode, OpacityType srcColorOpacity) {
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

bool FillStyle::isOpaque() const {
  if (maskFilter) {
    return false;
  }
  if (colorFilter && !colorFilter->isAlphaUnchanged()) {
    return false;
  }
  return BlendModeIsOpaque(blendMode, GetOpacityType(color, shader.get()));
}

}  // namespace tgfx
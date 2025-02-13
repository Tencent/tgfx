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
#include "core/utils/Caster.h"
#include "gpu/Blend.h"

namespace tgfx {
static OpacityType GetOpacityType(const Color& color, const Shader* shader,
                                  bool hasExtraImageFill) {
  auto alpha = color.alpha;
  if (alpha == 1.0f && !hasExtraImageFill && (!shader || shader->isOpaque())) {
    return OpacityType::Opaque;
  }
  if (alpha == 0.0f) {
    if (hasExtraImageFill || shader || color.red != 0.0f || color.green != 0.0f ||
        color.blue != 0.0f) {
      return OpacityType::TransparentAlpha;
    }
    return OpacityType::TransparentBlack;
  }
  return OpacityType::Unknown;
}

bool FillStyle::isOpaque(bool hasExtraImageFill) const {
  if (maskFilter) {
    return false;
  }
  if (colorFilter && !colorFilter->isAlphaUnchanged()) {
    return false;
  }
  auto opacityType = GetOpacityType(color, shader.get(), hasExtraImageFill);
  return BlendModeIsOpaque(blendMode, opacityType);
}

bool FillStyle::isEqual(const FillStyle& style, bool ignoreColor) const {
  if (antiAlias != style.antiAlias || blendMode != style.blendMode ||
      (!ignoreColor && color != style.color)) {
    return false;
  }
  if (shader) {
    if (!style.shader || !Caster::Compare(shader.get(), style.shader.get())) {
      return false;
    }
  } else if (style.shader) {
    return false;
  }
  if (maskFilter) {
    if (!style.maskFilter || !Caster::Compare(maskFilter.get(), style.maskFilter.get())) {
      return false;
    }
  } else if (style.maskFilter) {
    return false;
  }
  if (colorFilter) {
    if (!style.colorFilter || !Caster::Compare(colorFilter.get(), style.colorFilter.get())) {
      return false;
    }
  } else if (style.colorFilter) {
    return false;
  }
  return true;
}

FillStyle FillStyle::makeWithMatrix(const Matrix& matrix) const {
  auto fillStyle = *this;
  if (fillStyle.shader) {
    fillStyle.shader = fillStyle.shader->makeWithMatrix(matrix);
  }
  if (fillStyle.maskFilter) {
    fillStyle.maskFilter = fillStyle.maskFilter->makeWithMatrix(matrix);
  }
  return fillStyle;
}
}  // namespace tgfx
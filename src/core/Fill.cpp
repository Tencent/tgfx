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

#include "tgfx/core/Fill.h"
#include "core/utils/Caster.h"
#include "gpu/Blend.h"

namespace tgfx {
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

bool Fill::hasOnlyColor() const {
  return !shader && !maskFilter && !colorFilter;
}

bool Fill::isOpaque() const {
  if (maskFilter) {
    return false;
  }
  if (colorFilter && !colorFilter->isAlphaUnchanged()) {
    return false;
  }
  return BlendModeIsOpaque(blendMode, GetOpacityType(color, shader.get()));
}

bool Fill::isEqual(const Fill& fill, bool ignoreColor) const {
  if (antiAlias != fill.antiAlias || blendMode != fill.blendMode ||
      (!ignoreColor && color != fill.color)) {
    return false;
  }
  if (shader) {
    if (!fill.shader || !Caster::Compare(shader.get(), fill.shader.get())) {
      return false;
    }
  } else if (fill.shader) {
    return false;
  }
  if (maskFilter) {
    if (!fill.maskFilter || !Caster::Compare(maskFilter.get(), fill.maskFilter.get())) {
      return false;
    }
  } else if (fill.maskFilter) {
    return false;
  }
  if (colorFilter) {
    if (!fill.colorFilter || !Caster::Compare(colorFilter.get(), fill.colorFilter.get())) {
      return false;
    }
  } else if (fill.colorFilter) {
    return false;
  }
  return true;
}

Fill Fill::makeWithMatrix(const Matrix& matrix) const {
  auto fill = *this;
  if (fill.shader) {
    fill.shader = fill.shader->makeWithMatrix(matrix);
  }
  if (fill.maskFilter) {
    fill.maskFilter = fill.maskFilter->makeWithMatrix(matrix);
  }
  return fill;
}
}  // namespace tgfx
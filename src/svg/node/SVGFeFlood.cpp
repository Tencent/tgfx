/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "tgfx/svg/node/SVGFeFlood.h"
#include <algorithm>
#include "svg/SVGRenderContext.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Shader.h"

namespace tgfx {

std::shared_ptr<ImageFilter> SVGFeFlood::onMakeImageFilter(const SVGRenderContext& context,
                                                           const SVGFilterContext&) const {
  Color color = Color::Black();
  const auto& floodColor = this->getFloodColor();
  if (floodColor.isValue()) {
    color = context.resolveSVGColor(*floodColor);
  }
  float opacity = 1.0f;
  const auto& floodOpacity = this->getFloodOpacity();
  if (floodOpacity.isValue()) {
    opacity = *floodOpacity;
  }
  color.alpha *= std::clamp(opacity, 0.0f, 1.0f);
  // feFlood fills its region with a constant color and ignores the input; BlendMode::Src makes
  // the blend output equal to the shader regardless of the source image.
  return ImageFilter::Blend(BlendMode::Src, Shader::MakeColorShader(color));
}

}  // namespace tgfx

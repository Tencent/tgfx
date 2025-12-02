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

#include "ToPMColor.h"
#include "ColorSpaceHelper.h"
#include "core/ColorSpaceXformSteps.h"

namespace tgfx {

PMColor ToPMColor(const Color& color, const std::shared_ptr<ColorSpace>& dstColorSpace) {
  if (dstColorSpace == nullptr) {
    return PMColor{color.red * color.alpha, color.green * color.alpha, color.blue * color.alpha,
                   color.alpha, color.colorSpace};
  }

  if (!NeedConvertColorSpace(color.colorSpace.get(), dstColorSpace.get())) {
    return PMColor{color.red * color.alpha, color.green * color.alpha, color.blue * color.alpha,
                   color.alpha, color.colorSpace};
    ;
  }
  ColorSpaceXformSteps steps(color.colorSpace.get(), AlphaType::Premultiplied, dstColorSpace.get(),
                             AlphaType::Premultiplied);
  auto dstColor = color.premultiply();
  steps.apply(dstColor.array());
  dstColor.colorSpace = std::move(dstColorSpace);
  return dstColor;
}
}  // namespace tgfx

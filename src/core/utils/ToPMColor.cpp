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

Color ConvertColor(const Color& color, const std::shared_ptr<ColorSpace>& dstColorSpace) {
  if (dstColorSpace == nullptr) {
    return color;
  }

  if (!NeedConvertColorSpace(ColorSpace::SRGB().get(), dstColorSpace.get())) {
    return color;
  }
  ColorSpaceXformSteps steps(ColorSpace::SRGB().get(), AlphaType::Unpremultiplied, dstColorSpace.get(),
                             AlphaType::Unpremultiplied);
  auto dstColor = color;
  steps.apply(dstColor.array());
  return dstColor;
}

PMColor ConvertPMColor(const PMColor& color, const std::shared_ptr<ColorSpace>& dstColorSpace) {
  if (dstColorSpace == nullptr) {
    return color;
  }
  if (!NeedConvertColorSpace(ColorSpace::SRGB().get(), dstColorSpace.get())) {
    return color;
  }
  ColorSpaceXformSteps steps(ColorSpace::SRGB().get(), AlphaType::Premultiplied, dstColorSpace.get(),
                             AlphaType::Premultiplied);
  auto dstColor = color;
  steps.apply(dstColor.array());
  return dstColor;
}
}  // namespace tgfx

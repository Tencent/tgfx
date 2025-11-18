/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "tgfx/core/Color.h"
#include "core/ColorSpaceXformSteps.h"
#include "tgfx/core/AlphaType.h"
#include "utils/ColorSpaceHelper.h"

namespace tgfx {

template <>
void Color::applyColorSpace(std::shared_ptr<ColorSpace> dstColorSpace) {
  if (!NeedConvertColorSpace(colorSpace, dstColorSpace)) {
    return;
  }
  ColorSpaceXformSteps steps(colorSpace.get(), AlphaType::Unpremultiplied, dstColorSpace.get(),
                             AlphaType::Unpremultiplied);
  steps.apply(&red);
  colorSpace = std::move(dstColorSpace);
}

template <>
void PMColor::applyColorSpace(std::shared_ptr<ColorSpace> dstColorSpace) {
  if (!NeedConvertColorSpace(colorSpace, dstColorSpace)) {
    return;
  }
  ColorSpaceXformSteps steps(colorSpace.get(), AlphaType::Premultiplied, dstColorSpace.get(),
                             AlphaType::Premultiplied);
  steps.apply(&red);
  colorSpace = std::move(dstColorSpace);
}

}  // namespace tgfx

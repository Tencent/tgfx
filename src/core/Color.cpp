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
RGBA4f<AlphaType::Premultiplied> RGBA4f<AlphaType::Premultiplied>::FromRGBA(
    uint8_t r, uint8_t g, uint8_t b, uint8_t a, const std::shared_ptr<ColorSpace>& colorSpace) {
  float srcColor[4] = {static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
                       static_cast<float>(b) / 255.0f,
                       a == 255 ? 1.0f : static_cast<float>(a) / 255.0f};
  if (NeedConvertColorSpace(colorSpace, ColorSpace::SRGB())) {
    ColorSpaceXformSteps steps{colorSpace.get(), AlphaType::Premultiplied, ColorSpace::SRGB().get(),
                               AlphaType::Premultiplied};
    steps.apply(srcColor);
  }
  return {srcColor[0], srcColor[1], srcColor[2], srcColor[3]};
}

template <>
RGBA4f<AlphaType::Unpremultiplied> RGBA4f<AlphaType::Unpremultiplied>::FromRGBA(
    uint8_t r, uint8_t g, uint8_t b, uint8_t a, const std::shared_ptr<ColorSpace>& colorSpace) {
  float srcColor[4] = {static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
                       static_cast<float>(b) / 255.0f,
                       a == 255 ? 1.0f : static_cast<float>(a) / 255.0f};
  if (NeedConvertColorSpace(colorSpace, ColorSpace::SRGB())) {
    ColorSpaceXformSteps steps{colorSpace.get(), AlphaType::Unpremultiplied,
                               ColorSpace::SRGB().get(), AlphaType::Unpremultiplied};
    steps.apply(srcColor);
  }
  return {srcColor[0], srcColor[1], srcColor[2], srcColor[3]};
}

}  // namespace tgfx

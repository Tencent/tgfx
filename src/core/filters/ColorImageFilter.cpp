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

#include "ColorImageFilter.h"
#include "core/filters/ImageFilterBase.h"
#include "gpu/processors/ComposeFragmentProcessor.h"
#include "gpu/processors/FragmentProcessor.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/ColorFilter.h"

namespace tgfx {

std::shared_ptr<ImageFilter> ImageFilter::ColorFilter(
    std::shared_ptr<class ColorFilter> colorFilter) {
  return std::make_shared<ColorImageFilter>(std::move(colorFilter));
}

ColorImageFilter::ColorImageFilter(std::shared_ptr<tgfx::ColorFilter> filter)
    : filter(std::move(filter)) {
}

ImageFilterType ColorImageFilter::asImageFilterInfo(ImageFilterInfo* info) const {
  if (info) {
    Color color;
    BlendMode blendMode;
    if (filter->asColorMode(&color, &blendMode)) {
      info->color = color;
      info->blendMode = blendMode;
    }
  }
  return ImageFilterType::Color;
}

std::unique_ptr<FragmentProcessor> ColorImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    const Matrix* uvMatrix) const {
  auto imageProcessor = FragmentProcessor::Make(std::move(source), args, sampling, uvMatrix);
  if (imageProcessor == nullptr) {
    return nullptr;
  }
  return ComposeFragmentProcessor::Make(std::move(imageProcessor), filter->asFragmentProcessor());
}
}  // namespace tgfx

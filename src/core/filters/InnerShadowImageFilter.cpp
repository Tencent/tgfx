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

#include "InnerShadowImageFilter.h"
#include "core/images/TextureImage.h"
#include "core/utils/NeedMipmaps.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/XfermodeFragmentProcessor.h"
namespace tgfx {
std::shared_ptr<ImageFilter> ImageFilter::InnerShadow(float dx, float dy, float blurrinessX,
                                                      float blurrinessY, const Color& color) {

  auto dropShadowFilter = DropShadowOnly(dx, dy, blurrinessX, blurrinessY, Color::Black());
  if (!dropShadowFilter) {
    return nullptr;
  }

  return std::make_shared<InnerShadowImageFilter>(dropShadowFilter, color, false);
}

std::shared_ptr<ImageFilter> ImageFilter::InnerShadowOnly(float dx, float dy, float blurrinessX,
                                                          float blurrinessY, const Color& color) {
  auto dropShadowFilter = DropShadowOnly(dx, dy, blurrinessX, blurrinessY, Color::Black());
  if (!dropShadowFilter) {
    return nullptr;
  }

  return std::make_shared<InnerShadowImageFilter>(dropShadowFilter, color, true);
}

InnerShadowImageFilter::InnerShadowImageFilter(std::shared_ptr<ImageFilter> dropShadowFilter,
                                               const Color& color, bool shadowOnly)
    : dropShadowFilter(std::move(dropShadowFilter)), color(color), shadowOnly(shadowOnly) {
}

std::unique_ptr<FragmentProcessor> InnerShadowImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    const Matrix* uvMatrix) const {
  if (source->isComplex()) {
    auto needMipmaps = NeedMipmaps(sampling, args.viewMatrix, uvMatrix);
    source = source->makeRasterized(needMipmaps, sampling);
  }

  // get drop shadow mask
  std::unique_ptr<FragmentProcessor> invertShadowMask =
      dropShadowFilter->asFragmentProcessor(source, args, sampling, uvMatrix);

  auto colorProcessor = ConstColorProcessor::Make(color, InputMode::Ignore);

  // get inverted drop shadow mask and fill it with color
  auto colorShadowProcessor = XfermodeFragmentProcessor::MakeFromTwoProcessors(
      std::move(colorProcessor), std::move(invertShadowMask), BlendMode::SrcOut);

  auto imageProcessor = FragmentProcessor::Make(std::move(source), args, TileMode::Decal,
                                                TileMode::Decal, sampling, uvMatrix);

  if (shadowOnly) {
    // mask the image with origin image
    return XfermodeFragmentProcessor::MakeFromTwoProcessors(
        std::move(colorShadowProcessor), std::move(imageProcessor), BlendMode::SrcIn);
  } else {
    // mask the image with origin image and draw the drop shadow mask on top
    return XfermodeFragmentProcessor::MakeFromTwoProcessors(
        std::move(colorShadowProcessor), std::move(imageProcessor), BlendMode::SrcATop);
  }
}

}  // namespace tgfx

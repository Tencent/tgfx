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

#include "DropShadowImageFilter.h"
#include "core/images/TextureImage.h"
#include "gpu/processors/ColorSpaceXformEffect.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/XfermodeFragmentProcessor.h"

namespace tgfx {
std::shared_ptr<ImageFilter> ImageFilter::DropShadow(float dx, float dy, float blurrinessX,
                                                     float blurrinessY, const Color& color) {
  if (color.alpha <= 0) {
    return nullptr;
  }
  return std::make_shared<DropShadowImageFilter>(dx, dy, blurrinessX, blurrinessY, color, false);
}

std::shared_ptr<ImageFilter> ImageFilter::DropShadowOnly(float dx, float dy, float blurrinessX,
                                                         float blurrinessY, const Color& color) {
  // If color is transparent, the image after applying the filter will be transparent.
  // So we should not return nullptr when color is transparent.
  return std::make_shared<DropShadowImageFilter>(dx, dy, blurrinessX, blurrinessY, color, true);
}

DropShadowImageFilter::DropShadowImageFilter(float dx, float dy, float blurrinessX,
                                             float blurrinessY, const Color& color, bool shadowOnly)
    : dx(dx), dy(dy), blurFilter(ImageFilter::Blur(blurrinessX, blurrinessY)), color(color),
      shadowOnly(shadowOnly) {
}

Rect DropShadowImageFilter::onFilterBounds(const Rect& srcRect) const {
  auto bounds = srcRect;
  bounds.offset(dx, dy);
  if (blurFilter != nullptr) {
    bounds = blurFilter->filterBounds(bounds);
  }
  if (!shadowOnly) {
    bounds.join(srcRect);
  }
  return bounds;
}

PlacementPtr<FragmentProcessor> DropShadowImageFilter::getSourceFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix, const std::shared_ptr<ColorSpace>& dstColorSpace) const {
  auto imgFP = FragmentProcessor::Make(source, args, TileMode::Decal, TileMode::Decal,
                                        sampling, constraint, uvMatrix);
  auto buffer = args.context->drawingBuffer();
  auto result = ColorSpaceXformEffect::Make(buffer, std::move(imgFP), source->colorSpace().get(), AlphaType::Premultiplied, dstColorSpace.get(), AlphaType::Premultiplied);
  if (result) {
    return result;
  }
  return ConstColorProcessor::Make(buffer, Color::Transparent(),
                                   InputMode::Ignore);
}

PlacementPtr<FragmentProcessor> DropShadowImageFilter::getShadowFragmentProcessor(
    const std::shared_ptr<Image>& source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix, std::shared_ptr<ColorSpace> dstColorSpace) const {
  auto shadowMatrix = Matrix::MakeTrans(-dx, -dy);
  if (uvMatrix) {
    shadowMatrix.preConcat(*uvMatrix);
  }
  auto buffer = args.context->drawingBuffer();
  PlacementPtr<FragmentProcessor> shadowProcessor;
  if (blurFilter != nullptr) {
    shadowProcessor =
        blurFilter->asFragmentProcessor(source, args, sampling, constraint, &shadowMatrix, dstColorSpace);
  } else {
    auto imgFP = FragmentProcessor::Make(source, args, TileMode::Decal, TileMode::Decal,
                                              sampling, constraint, &shadowMatrix);
    shadowProcessor = ColorSpaceXformEffect::Make(buffer, std::move(imgFP), source->colorSpace().get(), AlphaType::Premultiplied, dstColorSpace.get(), AlphaType::Premultiplied);
  }
  if (shadowProcessor == nullptr) {
    return nullptr;
  }
  auto dstColor = color;
  ColorSpaceXformSteps steps(ColorSpace::MakeSRGB().get(), AlphaType::Unpremultiplied,
                             dstColorSpace.get(), AlphaType::Premultiplied);
  steps.apply(dstColor.array());
  auto colorProcessor = ConstColorProcessor::Make(buffer, dstColor, InputMode::Ignore);
  return XfermodeFragmentProcessor::MakeFromTwoProcessors(
      buffer, std::move(colorProcessor), std::move(shadowProcessor), BlendMode::SrcIn);
}

PlacementPtr<FragmentProcessor> DropShadowImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix, std::shared_ptr<ColorSpace> dstColorSpace) const {
  if (color.alpha <= 0 && shadowOnly) {
    return nullptr;
  }
  auto shadowFragment = getShadowFragmentProcessor(source, args, sampling, constraint, uvMatrix, dstColorSpace);
  if (shadowOnly) {
    return shadowFragment;
  }
  if (!shadowFragment) {
    return getSourceFragmentProcessor(source, args, sampling, constraint, uvMatrix, dstColorSpace);
  }
  return XfermodeFragmentProcessor::MakeFromTwoProcessors(
      args.context->drawingBuffer(),
      getSourceFragmentProcessor(source, args, sampling, constraint, uvMatrix, dstColorSpace),
      std::move(shadowFragment), BlendMode::SrcOver);
}

}  // namespace tgfx

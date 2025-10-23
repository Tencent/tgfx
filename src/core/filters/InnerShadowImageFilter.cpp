/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/XfermodeFragmentProcessor.h"
namespace tgfx {
std::shared_ptr<ImageFilter> ImageFilter::InnerShadow(float dx, float dy, float blurrinessX,
                                                      float blurrinessY, const Color& color) {
  if (color.alpha <= 0) {
    return nullptr;
  }
  return std::make_shared<InnerShadowImageFilter>(dx, dy, blurrinessX, blurrinessY, color, false);
}

std::shared_ptr<ImageFilter> ImageFilter::InnerShadowOnly(float dx, float dy, float blurrinessX,
                                                          float blurrinessY, const Color& color) {
  // If color is transparent, the image after applying the filter will be transparent.
  // So we should not return nullptr when color is transparent.
  return std::make_shared<InnerShadowImageFilter>(dx, dy, blurrinessX, blurrinessY, color, true);
}

InnerShadowImageFilter::InnerShadowImageFilter(float dx, float dy, float blurrinessX,
                                               float blurrinessY, const Color& color,
                                               bool shadowOnly)

    : dx(dx), dy(dy), blurFilter(ImageFilter::Blur(blurrinessX, blurrinessY)), color(color),
      shadowOnly(shadowOnly) {
}

PlacementPtr<FragmentProcessor> InnerShadowImageFilter::getShadowFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  auto shadowMatrix = Matrix::MakeTrans(-dx, -dy);
  if (uvMatrix) {
    shadowMatrix.preConcat(*uvMatrix);
  }

  PlacementPtr<FragmentProcessor> invertShadowMask;
  if (blurFilter != nullptr) {
    invertShadowMask =
        blurFilter->asFragmentProcessor(source, args, sampling, constraint, &shadowMatrix);
  } else {
    invertShadowMask = FragmentProcessor::Make(source, args, TileMode::Decal, TileMode::Decal,
                                               sampling, constraint, &shadowMatrix);
  }

  auto buffer = args.context->drawingBuffer();
  if (invertShadowMask == nullptr) {
    invertShadowMask =
        ConstColorProcessor::Make(buffer, Color::Transparent().premultiply(), InputMode::Ignore);
  }
  auto dstColor = color;
  ColorSpaceXformSteps steps(ColorSpace::MakeSRGB().get(), AlphaType::Unpremultiplied,
                             source->colorSpace().get(), AlphaType::Premultiplied);
  steps.apply(dstColor.array());
  auto colorProcessor = ConstColorProcessor::Make(buffer, dstColor, InputMode::Ignore);

  // get shadow mask and fill it with color
  auto colorShadowProcessor = XfermodeFragmentProcessor::MakeFromTwoProcessors(
      buffer, std::move(colorProcessor), std::move(invertShadowMask), BlendMode::SrcOut);
  return colorShadowProcessor;
}

PlacementPtr<FragmentProcessor> InnerShadowImageFilter::getSourceFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  return FragmentProcessor::Make(source, args, TileMode::Decal, TileMode::Decal, sampling,
                                 constraint, uvMatrix);
}

PlacementPtr<FragmentProcessor> InnerShadowImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  if (color.alpha <= 0 && shadowOnly) {
    return nullptr;
  }
  auto imageProcessor = getSourceFragmentProcessor(source, args, sampling, constraint, uvMatrix);
  if (imageProcessor == nullptr) {
    return nullptr;
  }
  auto buffer = args.context->drawingBuffer();
  auto blendMode = shadowOnly ? BlendMode::SrcIn : BlendMode::SrcATop;

  return XfermodeFragmentProcessor::MakeFromTwoProcessors(
      buffer, getShadowFragmentProcessor(source, args, sampling, constraint, uvMatrix),
      std::move(imageProcessor), blendMode);
}

}  // namespace tgfx

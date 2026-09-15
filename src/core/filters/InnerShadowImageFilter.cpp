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
#include "core/utils/ColorHelper.h"
#include "gpu/FPFlattenHelper.h"
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

  auto allocator = args.context->drawingAllocator();
  if (invertShadowMask == nullptr) {
    invertShadowMask =
        ConstColorProcessor::Make(allocator, PMColor::Transparent(), InputMode::Ignore);
  }
  // P4 group three: no construction-time materialization by default; the in-plan retry (or the
  // legacy switch) sets MaterializeBlendChildren when the chain route refuses the original tree.
  if (BlendChildMaterializationIsLegacy() ||
      (args.renderFlags & InternalRenderFlags::MaterializeBlendChildren) != 0) {
    invertShadowMask = EnsureSimpleBlendChild(args, std::move(invertShadowMask), 1);
    if (invertShadowMask == nullptr) {
      return nullptr;
    }
  }
  auto dstColor = ToPMColor(color, source->colorSpace());
  auto colorProcessor = ConstColorProcessor::Make(allocator, dstColor, InputMode::Ignore);

  // get shadow mask and fill it with color
  auto colorShadowProcessor = XfermodeFragmentProcessor::MakeFromTwoProcessors(
      allocator, std::move(colorProcessor), std::move(invertShadowMask), BlendMode::SrcOut);
  return colorShadowProcessor;
}

PlacementPtr<FragmentProcessor> InnerShadowImageFilter::getSourceFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  return FragmentProcessor::Make(std::move(source), args, TileMode::Decal, TileMode::Decal,
                                 sampling, constraint, uvMatrix);
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
  auto allocator = args.context->drawingAllocator();
  auto blendMode = shadowOnly ? BlendMode::SrcIn : BlendMode::SrcATop;

  auto shadowFP = getShadowFragmentProcessor(source, args, sampling, constraint, uvMatrix);
  // P4 group three: same planned-materialization gate as getShadowFragmentProcessor above. The
  // shadow FP is itself a two-child blend (complex), so it is exactly the operand the retry
  // rewrites before the final SrcIn/SrcATop tree.
  if (BlendChildMaterializationIsLegacy() ||
      (args.renderFlags & InternalRenderFlags::MaterializeBlendChildren) != 0) {
    shadowFP = EnsureSimpleBlendChild(args, std::move(shadowFP));
    if (shadowFP == nullptr) {
      return nullptr;
    }
    imageProcessor = EnsureSimpleBlendChild(args, std::move(imageProcessor), 1);
    if (imageProcessor == nullptr) {
      return nullptr;
    }
  }
  return XfermodeFragmentProcessor::MakeFromTwoProcessors(allocator, std::move(shadowFP),
                                                          std::move(imageProcessor), blendMode);
}

}  // namespace tgfx

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
#include "core/utils/Log.h"
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

PlacementPtr<FragmentProcessor> DropShadowImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  if (color.alpha <= 0) {
    // The filer will not be created if filter is not drop shadow only and alpha < 0.So if color is
    // transparent, the image after applying the filter will be transparent.
    return nullptr;
  }
  PlacementPtr<FragmentProcessor> shadowProcessor;

  auto drawBounds = args.drawRect;
  auto fpMatrix = Matrix::I();
  if (uvMatrix != nullptr) {
    drawBounds = uvMatrix->mapRect(drawBounds);
    fpMatrix = *uvMatrix;
  }

  auto clipBounds = drawBounds;
  clipBounds.offset(-dx, -dy);
  if (!shadowOnly) {
    // if shadowOnly is false, we need to include the original image bounds
    clipBounds.join(drawBounds);
  }
  if (blurFilter) {
    // outset the bounds to include the blur radius
    clipBounds = blurFilter->filterBounds(clipBounds);
  }
  auto sourceRect = Rect::MakeXYWH(0, 0, source->width(), source->height());
  if (!clipBounds.intersect(sourceRect)) {
    return nullptr;
  }
  source = source->makeSubset(clipBounds);
  if (!source) {
    return nullptr;
  }
  source = source->makeRasterized();

  // add the subset offset to the matrix
  fpMatrix.postConcat(Matrix::MakeTrans(-clipBounds.left, -clipBounds.top));

  auto shadowMatrix = Matrix::MakeTrans(-dx, -dy);
  shadowMatrix.preConcat(fpMatrix);

  if (blurFilter != nullptr) {
    shadowProcessor =
        blurFilter->asFragmentProcessor(source, args, sampling, constraint, &shadowMatrix);
  } else {
    shadowProcessor = FragmentProcessor::Make(source, args, TileMode::Decal, TileMode::Decal,
                                              sampling, constraint, &shadowMatrix);
  }
  if (shadowProcessor == nullptr) {
    return nullptr;
  }
  auto buffer = args.context->drawingBuffer();
  auto colorProcessor = ConstColorProcessor::Make(buffer, color.premultiply(), InputMode::Ignore);
  auto colorShadowProcessor = XfermodeFragmentProcessor::MakeFromTwoProcessors(
      buffer, std::move(colorProcessor), std::move(shadowProcessor), BlendMode::SrcIn);
  if (shadowOnly) {
    return colorShadowProcessor;
  }
  auto imageProcessor = FragmentProcessor::Make(std::move(source), args, TileMode::Decal,
                                                TileMode::Decal, sampling, constraint, &fpMatrix);
  return XfermodeFragmentProcessor::MakeFromTwoProcessors(
      buffer, std::move(imageProcessor), std::move(colorShadowProcessor), BlendMode::SrcOver);
}

}  // namespace tgfx

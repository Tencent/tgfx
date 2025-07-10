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

PlacementPtr<FragmentProcessor> InnerShadowImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  if (color.alpha <= 0) {
    // The filer will not be created if filter is not drop shadow only and alpha < 0.So if color is
    // transparent, the image after applying the filter will be transparent.
    return nullptr;
  }

  auto drawBounds = args.drawRect;
  auto fpMatrix = Matrix::I();
  if (uvMatrix != nullptr) {
    drawBounds = uvMatrix->mapRect(drawBounds);
    fpMatrix = *uvMatrix;
  }
  auto clipBounds = drawBounds;
  clipBounds.offset(-dx, -dy);
  clipBounds.join(drawBounds);

  auto sourceRect = Rect::MakeXYWH(0, 0, source->width(), source->height());
  if (blurFilter) {
    // outset the bounds to include the blur radius
    clipBounds = blurFilter->filterBounds(clipBounds);
  }

  clipBounds.roundOut();
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
  PlacementPtr<FragmentProcessor> invertShadowMask;

  auto buffer = args.context->drawingBuffer();
  if (clipBounds.width() <= fabsf(dx) || clipBounds.height() <= fabsf(dy)) {
    // If the clip bounds's width or height is less than offset, that means the mask will be
    // transparent.
    invertShadowMask =
        ConstColorProcessor::Make(buffer, Color::Transparent().premultiply(), InputMode::Ignore);
  } else if (blurFilter != nullptr) {
    invertShadowMask =
        blurFilter->asFragmentProcessor(source, args, sampling, constraint, &shadowMatrix);
  } else {
    invertShadowMask = FragmentProcessor::Make(source, args, TileMode::Decal, TileMode::Decal,
                                               sampling, constraint, &shadowMatrix);
  }

  if (invertShadowMask == nullptr) {
    invertShadowMask =
        ConstColorProcessor::Make(buffer, Color::Transparent().premultiply(), InputMode::Ignore);
  }
  auto colorProcessor = ConstColorProcessor::Make(buffer, color.premultiply(), InputMode::Ignore);

  // get shadow mask and fill it with color
  auto colorShadowProcessor = XfermodeFragmentProcessor::MakeFromTwoProcessors(
      buffer, std::move(colorProcessor), std::move(invertShadowMask), BlendMode::SrcOut);

  auto imageProcessor = FragmentProcessor::Make(std::move(source), args, TileMode::Decal,
                                                TileMode::Decal, sampling, constraint, &fpMatrix);

  if (shadowOnly) {
    // mask the image with origin image
    return XfermodeFragmentProcessor::MakeFromTwoProcessors(
        buffer, std::move(colorShadowProcessor), std::move(imageProcessor), BlendMode::SrcIn);
  } else {
    // mask the image with origin image and draw the inner shadow mask on top
    return XfermodeFragmentProcessor::MakeFromTwoProcessors(
        buffer, std::move(colorShadowProcessor), std::move(imageProcessor), BlendMode::SrcATop);
  }
}

}  // namespace tgfx

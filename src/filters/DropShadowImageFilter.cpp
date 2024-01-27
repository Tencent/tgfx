/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "gpu/Texture.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/processors/XfermodeFragmentProcessor.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/core/ColorFilter.h"

namespace tgfx {
std::shared_ptr<ImageFilter> ImageFilter::DropShadow(float dx, float dy, float blurrinessX,
                                                     float blurrinessY, const Color& color,
                                                     const Rect* cropRect) {
  if (cropRect && cropRect->isEmpty()) {
    return nullptr;
  }
  return std::make_shared<DropShadowImageFilter>(dx, dy, blurrinessX, blurrinessY, color, false,
                                                 cropRect);
}

std::shared_ptr<ImageFilter> ImageFilter::DropShadowOnly(float dx, float dy, float blurrinessX,
                                                         float blurrinessY, const Color& color,
                                                         const Rect* cropRect) {
  if (cropRect && cropRect->isEmpty()) {
    return nullptr;
  }
  return std::make_shared<DropShadowImageFilter>(dx, dy, blurrinessX, blurrinessY, color, true,
                                                 cropRect);
}

DropShadowImageFilter::DropShadowImageFilter(float dx, float dy, float blurrinessX,
                                             float blurrinessY, const Color& color, bool shadowOnly,
                                             const Rect* cropRect)
    : ImageFilter(cropRect), dx(dx), dy(dy),
      blurFilter(ImageFilter::Blur(blurrinessX, blurrinessY)), color(color),
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

static bool CanDirectlyDraw(TileMode tileMode) {
  return tileMode == TileMode::Clamp || tileMode == TileMode::Decal;
}

std::unique_ptr<FragmentProcessor> DropShadowImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const ImageFPArgs& args, const Matrix* localMatrix,
    const Rect* clipBounds) const {
  auto inputBounds = Rect::MakeWH(source->width(), source->height());
  Rect dstBounds = Rect::MakeEmpty();
  if (!applyCropRect(inputBounds, &dstBounds, clipBounds)) {
    return nullptr;
  }
  if (CanDirectlyDraw(args.tileModeX) && CanDirectlyDraw(args.tileModeY)) {
    ImageFPArgs shadowArgs(args.context, args.sampling, args.renderFlags, TileMode::Decal,
                           TileMode::Decal);
    return getFragmentProcessor(std::move(source), shadowArgs, dstBounds, localMatrix);
  }
  auto mipMapped = source->hasMipmaps() && args.sampling.mipMapMode != MipMapMode::None;
  auto renderTarget = RenderTargetProxy::Make(args.context, static_cast<int>(dstBounds.width()),
                                              static_cast<int>(dstBounds.height()),
                                              PixelFormat::RGBA_8888, 1, mipMapped);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  ImageFPArgs shadowArgs(args.context, {}, args.renderFlags, TileMode::Decal, TileMode::Decal);
  auto processor = getFragmentProcessor(std::move(source), shadowArgs, dstBounds);
  if (processor == nullptr) {
    return nullptr;
  }
  fillRenderTargetWithFP(renderTarget, std::move(processor),
                         Matrix::MakeTrans(dstBounds.x(), dstBounds.y()));
  auto matrix = Matrix::MakeTrans(-dstBounds.x(), -dstBounds.y());
  if (localMatrix != nullptr) {
    matrix.preConcat(*localMatrix);
  }
  return TiledTextureEffect::Make(renderTarget->getTextureProxy(), args.tileModeX, args.tileModeY,
                                  args.sampling, &matrix);
}

std::unique_ptr<FragmentProcessor> DropShadowImageFilter::getFragmentProcessor(
    std::shared_ptr<Image> source, const ImageFPArgs& args, const Rect& dstBounds,
    const Matrix* localMatrix) const {
  std::unique_ptr<FragmentProcessor> shadowProcessor;
  auto shadowMatrix = Matrix::MakeTrans(-dx, -dy);
  if (localMatrix != nullptr) {
    shadowMatrix.preConcat(*localMatrix);
  }
  auto shadowBounds = dstBounds.makeOffset(-dx, -dy);
  if (blurFilter != nullptr) {
    shadowProcessor = blurFilter->asFragmentProcessor(source, args, &shadowMatrix, &shadowBounds);
  } else {
    shadowProcessor = FragmentProcessor::MakeFromImage(source, args, &shadowMatrix, &shadowBounds);
  }
  if (shadowProcessor == nullptr) {
    return nullptr;
  }
  auto colorProcessor = ConstColorProcessor::Make(color, InputMode::Ignore);
  auto colorShadowProcessor = XfermodeFragmentProcessor::MakeFromTwoProcessors(
      std::move(colorProcessor), std::move(shadowProcessor), BlendMode::SrcIn);
  if (shadowOnly) {
    return colorShadowProcessor;
  }
  auto imageProcessor = FragmentProcessor::MakeFromImage(source, args, localMatrix, &dstBounds);
  return XfermodeFragmentProcessor::MakeFromTwoProcessors(
      std::move(imageProcessor), std::move(colorShadowProcessor), BlendMode::SrcOver);
}

}  // namespace tgfx

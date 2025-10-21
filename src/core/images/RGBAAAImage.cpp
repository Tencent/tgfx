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

#include "RGBAAAImage.h"
#include "ScaledImage.h"
#include "core/utils/AddressOf.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "gpu/DrawingManager.h"
#include "gpu/TPArgs.h"
#include "gpu/ops/RectDrawOp.h"
#include "gpu/processors/ColorSpaceXFormEffect.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {
std::shared_ptr<Image> RGBAAAImage::MakeFrom(std::shared_ptr<Image> source, int displayWidth,
                                             int displayHeight, int alphaStartX, int alphaStartY) {
  if (source == nullptr || source->isAlphaOnly() || alphaStartX + displayWidth > source->width() ||
      alphaStartY + displayHeight > source->height()) {
    return nullptr;
  }
  auto bounds = Rect::MakeWH(displayWidth, displayHeight);
  auto alphaStart = Point::Make(alphaStartX, alphaStartY);
  auto image = std::shared_ptr<RGBAAAImage>(new RGBAAAImage(std::move(source), bounds, alphaStart));
  image->weakThis = image;
  return image;
}

RGBAAAImage::RGBAAAImage(std::shared_ptr<Image> source, const Rect& bounds, const Point& alphaStart)
    : SubsetImage(std::move(source), bounds), alphaStart(alphaStart) {
}

std::shared_ptr<Image> RGBAAAImage::onCloneWith(std::shared_ptr<Image> newSource) const {
  auto image =
      std::shared_ptr<RGBAAAImage>(new RGBAAAImage(std::move(newSource), bounds, alphaStart));
  image->weakThis = image;
  return image;
}

PlacementPtr<FragmentProcessor> RGBAAAImage::asFragmentProcessor(
    const FPArgs& args, const SamplingArgs& samplingArgs, const Matrix* uvMatrix,
    std::shared_ptr<ColorSpace> dstColorSpace) const {
  DEBUG_ASSERT(!source->isAlphaOnly());
  auto matrix = concatUVMatrix(uvMatrix);
  auto drawBounds = args.drawRect;
  if (matrix) {
    matrix->mapRect(&drawBounds);
  }
  auto newSamplingArgs = samplingArgs;
  auto mipmapped = source->hasMipmaps() && samplingArgs.sampling.mipmapMode != MipmapMode::None;
  if (bounds.contains(drawBounds)) {
    if (samplingArgs.constraint != SrcRectConstraint::Strict && !newSamplingArgs.sampleArea) {
      // if samplingArgs has sampleArea, means the area is already subsetted
      newSamplingArgs.sampleArea = getSubset(drawBounds);
    }
    TPArgs tpArgs(args.context, args.renderFlags, mipmapped, 1.0f, {});
    auto proxy = source->lockTextureProxy(tpArgs);
    auto fp =
        TextureEffect::MakeRGBAAA(std::move(proxy), newSamplingArgs, alphaStart, AddressOf(matrix));
    if (!isAlphaOnly()) {
      return ColorSpaceXformEffect::Make(args.context->drawingBuffer(), std::move(fp),
                                         colorSpace().get(), AlphaType::Premultiplied,
                                         dstColorSpace.get(), AlphaType::Premultiplied);
    }
    return fp;
  }
  TPArgs tpArgs(args.context, args.renderFlags, mipmapped, 1.0f, {});
  auto textureProxy = lockTextureProxy(tpArgs);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  newSamplingArgs.sampleArea = std::nullopt;
  auto fp = TiledTextureEffect::Make(textureProxy, newSamplingArgs, uvMatrix, false);
  if (!isAlphaOnly()) {
    return ColorSpaceXformEffect::Make(args.context->drawingBuffer(), std::move(fp),
                                       colorSpace().get(), AlphaType::Premultiplied,
                                       dstColorSpace.get(), AlphaType::Premultiplied);
  }
  return fp;
}

std::shared_ptr<TextureProxy> RGBAAAImage::lockTextureProxy(const TPArgs& args) const {
  auto textureWidth = width();
  auto textureHeight = height();
  auto renderTarget = RenderTargetProxy::MakeFallback(
      args.context, textureWidth, textureHeight, isAlphaOnly(), 1, args.mipmapped,
      ImageOrigin::TopLeft, colorSpace(), args.backingFit);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto drawRect = Rect::MakeWH(textureWidth, textureHeight);
  FPArgs fpArgs(args.context, args.renderFlags, drawRect, 1.0f);
  auto processor = asFragmentProcessor(fpArgs, {}, nullptr, renderTarget->colorSpace());
  auto drawingManager = args.context->drawingManager();
  if (!drawingManager->fillRTWithFP(renderTarget, std::move(processor), args.renderFlags)) {
    return nullptr;
  }
  return renderTarget->asTextureProxy();
}

std::shared_ptr<Image> RGBAAAImage::onMakeSubset(const Rect& subset) const {
  auto newBounds = subset.makeOffset(bounds.x(), bounds.y());
  auto image = std::shared_ptr<Image>(new RGBAAAImage(source, newBounds, alphaStart));
  image->weakThis = image;
  return image;
}

std::shared_ptr<Image> RGBAAAImage::onMakeScaled(int newWidth, int newHeight,
                                                 const SamplingOptions& sampling) const {
  float scaleX = static_cast<float>(newWidth) / static_cast<float>(width());
  float scaleY = static_cast<float>(newHeight) / static_cast<float>(height());
  auto sourceScaledWidth = scaleX * static_cast<float>(source->width());
  auto sourceScaledHeight = scaleY * static_cast<float>(source->height());
  if (!IsInteger(sourceScaledWidth) || !IsInteger(sourceScaledHeight)) {
    return Image::onMakeScaled(newWidth, newHeight, sampling);
  }
  Point newAlphaStart = Point::Make(alphaStart.x * scaleX, alphaStart.y * scaleY);
  if (!IsInteger(newAlphaStart.x) || !IsInteger(newAlphaStart.y)) {
    return Image::onMakeScaled(newWidth, newHeight, sampling);
  }
  auto newSource = source->makeScaled(static_cast<int>(sourceScaledWidth),
                                      static_cast<int>(sourceScaledHeight), sampling);
  if (newSource == nullptr) {
    return nullptr;
  }
  auto newBounds = Rect::MakeXYWH(bounds.x() * scaleX, bounds.y() * scaleY,
                                  static_cast<float>(newWidth), static_cast<float>(newHeight));
  auto image =
      std::shared_ptr<RGBAAAImage>(new RGBAAAImage(std::move(newSource), newBounds, newAlphaStart));
  image->weakThis = image;
  return image;
}

}  // namespace tgfx

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "GlassUDFImage.h"

#include "layers/processors/GlassUDFTentBlurFragmentProcessor.h"
#include "tgfx/gpu/Context.h"

#include "core/images/TextureImage.h"
#include "gpu/DrawingManager.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/TPArgs.h"

namespace tgfx {

static constexpr int MaxTentRadius = 64;

// Blurs a window of the coverage image with two tent radii and returns one RGBA8 image. RGB holds
// the refraction field and A holds the edge-light field. Coordinates remain in the full UDF space.
static std::shared_ptr<Image> GenerateGlassUDFImage(Context* context,
                                                    const std::shared_ptr<Image>& source,
                                                    int coreWidth, int coreHeight,
                                                    const Rect& textureRect,
                                                    const Point& fineRadius,
                                                    const Point& coarseRadius) {
  if (context == nullptr || source == nullptr || coreWidth <= 0 || coreHeight <= 0 ||
      textureRect.isEmpty()) {
    return nullptr;
  }
  if (fineRadius.x <= 0.0f || fineRadius.y <= 0.0f || coarseRadius.x <= 0.0f ||
      coarseRadius.y <= 0.0f) {
    return nullptr;
  }
  auto textureWidth = static_cast<int>(std::round(textureRect.width()));
  auto textureHeight = static_cast<int>(std::round(textureRect.height()));
  if (textureWidth <= 0 || textureHeight <= 0) {
    return nullptr;
  }
  SamplingArgs samplingArgs = {TileMode::Decal, TileMode::Decal,
                               SamplingOptions(FilterMode::Linear), SrcRectConstraint::Fast};
  auto allocator = context->drawingAllocator();
  float verticalHalo = std::ceil(std::max(fineRadius.y, coarseRadius.y)) + 1.0f;
  auto horizontalRect = textureRect.makeOutset(0.0f, verticalHalo);
  horizontalRect.roundOut();
  auto horizontalHeight = static_cast<int>(std::round(horizontalRect.height()));
  auto horizontalTarget = RenderTargetProxy::Make(context, textureWidth, horizontalHeight, false, 1,
                                                  false, ImageOrigin::TopLeft, BackingFit::Exact);
  if (horizontalTarget == nullptr) {
    return nullptr;
  }
  float horizontalHalo = std::ceil(std::max(fineRadius.x, coarseRadius.x)) + 1.0f;
  auto sourceDrawRect =
      Rect::MakeLTRB(-horizontalHalo, -1.0f, static_cast<float>(textureWidth) + horizontalHalo,
                     static_cast<float>(horizontalHeight) + 1.0f);
  // Scale the source to UDF core density and rasterize uniformly for all Image subclasses,
  // without probing their internal rasterization behavior.
  auto coreSource = source->makeScaled(coreWidth, coreHeight, SamplingOptions(FilterMode::Linear))
                        ->makeRasterized();
  if (coreSource == nullptr) {
    return nullptr;
  }
  auto horizontalMatrix = Matrix::MakeTrans(textureRect.left, horizontalRect.top);
  FPArgs sourceArgs = FPArgs(context, 0, sourceDrawRect);
  // Each tent loop needs its own child because emitting one child twice redeclares its uniforms.
  auto fineSource =
      FragmentProcessor::Make(coreSource, sourceArgs, samplingArgs, &horizontalMatrix);
  auto coarseSource =
      FragmentProcessor::Make(coreSource, sourceArgs, samplingArgs, &horizontalMatrix);
  auto horizontalProcessor = GlassUDFTentBlurFragmentProcessor::Make(
      allocator, std::move(fineSource), std::move(coarseSource), fineRadius.x, coarseRadius.x,
      GlassUDFBlurDirection::Horizontal, MaxTentRadius, false);
  if (horizontalProcessor == nullptr || !context->drawingManager()->fillRTWithFP(
                                            horizontalTarget, std::move(horizontalProcessor), 0)) {
    return nullptr;
  }
  auto verticalMatrix = Matrix::MakeTrans(0.0f, textureRect.top - horizontalRect.top);
  auto horizontalProxy = horizontalTarget->asTextureProxy();
  auto verticalFineSource =
      TiledTextureEffect::Make(allocator, horizontalProxy, samplingArgs, &verticalMatrix);
  auto verticalCoarseSource =
      TiledTextureEffect::Make(allocator, horizontalProxy, samplingArgs, &verticalMatrix);
  auto verticalTarget = RenderTargetProxy::Make(context, textureWidth, textureHeight, false, 1,
                                                false, ImageOrigin::TopLeft, BackingFit::Exact);
  if (verticalTarget == nullptr) {
    return nullptr;
  }
  auto verticalProcessor = GlassUDFTentBlurFragmentProcessor::Make(
      allocator, std::move(verticalFineSource), std::move(verticalCoarseSource), fineRadius.y,
      coarseRadius.y, GlassUDFBlurDirection::Vertical, MaxTentRadius, true);
  if (verticalProcessor == nullptr ||
      !context->drawingManager()->fillRTWithFP(verticalTarget, std::move(verticalProcessor), 0)) {
    return nullptr;
  }
  return TextureImage::Wrap(verticalTarget->asTextureProxy(), nullptr);
}

std::shared_ptr<Image> GlassUDFImage::Make(std::shared_ptr<Image> source, int coreWidth,
                                           int coreHeight, const Rect& textureRect,
                                           const Point& fineRadius, const Point& coarseRadius) {
  if (source == nullptr || coreWidth <= 0 || coreHeight <= 0 || textureRect.isEmpty() ||
      fineRadius.x <= 0.0f || fineRadius.y <= 0.0f || coarseRadius.x <= 0.0f ||
      coarseRadius.y <= 0.0f) {
    return nullptr;
  }
  auto image = std::shared_ptr<GlassUDFImage>(new GlassUDFImage(
      std::move(source), coreWidth, coreHeight, textureRect, fineRadius, coarseRadius));
  image->weakThis = image;
  return image;
}

GlassUDFImage::GlassUDFImage(std::shared_ptr<Image> source, int coreWidth, int coreHeight,
                             const Rect& textureRect, const Point& fineRadius,
                             const Point& coarseRadius)
    : source(std::move(source)), coreWidth(coreWidth), coreHeight(coreHeight),
      textureRect(textureRect), fineRadius(fineRadius), coarseRadius(coarseRadius),
      _width(static_cast<int>(std::round(textureRect.width()))),
      _height(static_cast<int>(std::round(textureRect.height()))) {
}

std::shared_ptr<TextureProxy> GlassUDFImage::lockTextureProxy(const TPArgs& args) const {
  if (args.context == nullptr) {
    return nullptr;
  }
  auto textureImage = std::static_pointer_cast<TextureImage>(cachedTexture.lock());
  if (textureImage == nullptr || textureImage->makeTextureImage(args.context) == nullptr) {
    auto image = GenerateGlassUDFImage(args.context, source, coreWidth, coreHeight, textureRect,
                                       fineRadius, coarseRadius);
    if (image == nullptr) {
      return nullptr;
    }
    textureImage = std::static_pointer_cast<TextureImage>(image);
    cachedTexture = image;
  }
  return textureImage->getTextureProxy();
}

PlacementPtr<FragmentProcessor> GlassUDFImage::asFragmentProcessor(
    const FPArgs& args, const SamplingArgs& samplingArgs, const Matrix* uvMatrix) const {
  auto textureProxy = lockTextureProxy(TPArgs(args.context, args.renderFlags, false, args.drawScale,
                                              BackingFit::Exact));
  if (textureProxy == nullptr) {
    return nullptr;
  }
  return TiledTextureEffect::Make(args.context->drawingAllocator(), std::move(textureProxy),
                                  samplingArgs, uvMatrix);
}

}  // namespace tgfx

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

#include "GlassRefractionImageFilter.h"
#include "gpu/DrawingManager.h"
#include "gpu/TPArgs.h"
#include "gpu/processors/GlassRefractionFragmentProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {

GlassRefractionImageFilter::GlassRefractionImageFilter(const GlassRefractionParams& params,
                                                       std::shared_ptr<Image> fineMask,
                                                       std::shared_ptr<Image> coarseMask)
    : params(params), fineMask(std::move(fineMask)), coarseMask(std::move(coarseMask)) {
}

std::shared_ptr<TextureProxy> GlassRefractionImageFilter::lockTextureProxy(
    std::shared_ptr<Image> source, const Rect& /*renderBounds*/, const TPArgs& args) const {
  // Always render the full source extent. Glass refraction samples source pixels at offsets that
  // can reach any part of the texture, so a clipped sub-region would miss data. The render target
  // matches source dimensions and the returned texture covers the full source.
  auto sourceWidth = source->width();
  auto sourceHeight = source->height();
  auto renderTarget =
      RenderTargetProxy::Make(args.context, sourceWidth, sourceHeight, source->isAlphaOnly(), 1,
                              args.mipmapped, ImageOrigin::TopLeft, BackingFit::Exact);
  if (renderTarget == nullptr) {
    return nullptr;
  }

  // Lock source texture.
  TPArgs tpArgs(args.context, args.renderFlags, false, 1.0f, BackingFit::Exact);
  auto sourceProxy = source->lockTextureProxy(tpArgs);
  if (sourceProxy == nullptr) {
    return nullptr;
  }

  // Lock mask textures if present.
  std::shared_ptr<TextureProxy> fineMaskProxy = nullptr;
  std::shared_ptr<TextureProxy> coarseMaskProxy = nullptr;
  if (fineMask != nullptr) {
    fineMaskProxy = fineMask->lockTextureProxy(tpArgs);
  }
  if (coarseMask != nullptr) {
    coarseMaskProxy = coarseMask->lockTextureProxy(tpArgs);
  }

  auto allocator = args.context->drawingAllocator();
  auto processor = GlassRefractionFragmentProcessor::Make(allocator, std::move(sourceProxy),
                                                          std::move(fineMaskProxy),
                                                          std::move(coarseMaskProxy), params);
  if (processor == nullptr) {
    return nullptr;
  }

  auto drawingManager = args.context->drawingManager();
  if (!drawingManager->fillRTWithFP(renderTarget, std::move(processor), args.renderFlags)) {
    return nullptr;
  }

  return renderTarget->asTextureProxy();
}

PlacementPtr<FragmentProcessor> GlassRefractionImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  // Lock a full-size refraction texture (covers entire source, not a sub-region).
  auto mipmapped = source->hasMipmaps() && sampling.mipmapMode != MipmapMode::None;
  TPArgs tpArgs(args.context, args.renderFlags, mipmapped, 1.0f, BackingFit::Exact);
  auto inputBounds = Rect::MakeWH(source->width(), source->height());
  auto textureProxy = lockTextureProxy(std::move(source), inputBounds, tpArgs);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  // The returned texture covers the full source. Build a TextureEffect with identity mapping
  // (scaled to normalized coords), applying any incoming uvMatrix from the caller.
  auto fpMatrix = Matrix::MakeScale(
      static_cast<float>(textureProxy->width()) / inputBounds.width(),
      static_cast<float>(textureProxy->height()) / inputBounds.height());
  if (uvMatrix != nullptr) {
    fpMatrix.preConcat(*uvMatrix);
  }
  SamplingArgs samplingArgs = {TileMode::Clamp, TileMode::Clamp, sampling, constraint};
  auto allocator = args.context->drawingAllocator();
  return TextureEffect::Make(allocator, std::move(textureProxy), samplingArgs, &fpMatrix,
                             false);
}

}  // namespace tgfx

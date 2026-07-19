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
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {

GlassRefractionImageFilter::GlassRefractionImageFilter(const GlassRefractionParams& params,
                                                       std::shared_ptr<Image> fineMask,
                                                       std::shared_ptr<Image> coarseMask)
    : params(params), fineMask(std::move(fineMask)), coarseMask(std::move(coarseMask)) {
}

std::shared_ptr<TextureProxy> GlassRefractionImageFilter::lockTextureProxy(
    std::shared_ptr<Image> source, const Rect& renderBounds, const TPArgs& args) const {
  // Render target covers only the visible sub-region (renderBounds), but source texture remains
  // full-size so refraction offsets can sample any pixel. The render offset is passed to the
  // shader to map render target pixel coords back to source global coords.
  auto width = static_cast<int>(renderBounds.width());
  auto height = static_cast<int>(renderBounds.height());
  auto renderTarget =
      RenderTargetProxy::Make(args.context, width, height, source->isAlphaOnly(), 1, args.mipmapped,
                              ImageOrigin::TopLeft, BackingFit::Exact);
  if (renderTarget == nullptr) {
    return nullptr;
  }

  // Lock source texture at full size.
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

  // Pass render offset so the shader can map render target coords to source coords.
  auto localParams = params;
  localParams.renderOffsetX = renderBounds.left;
  localParams.renderOffsetY = renderBounds.top;

  auto allocator = args.context->drawingAllocator();
  auto processor = GlassRefractionFragmentProcessor::Make(allocator, std::move(sourceProxy),
                                                          std::move(fineMaskProxy),
                                                          std::move(coarseMaskProxy), localParams);
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
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& /*sampling*/,
    SrcRectConstraint /*constraint*/, const Matrix* uvMatrix) const {
  if (source == nullptr || args.context == nullptr) {
    return nullptr;
  }

  TPArgs tpArgs(args.context, args.renderFlags, false, 1.0f, BackingFit::Exact);
  auto sourceProxy = source->lockTextureProxy(tpArgs);
  if (sourceProxy == nullptr) {
    return nullptr;
  }

  std::shared_ptr<TextureProxy> fineMaskProxy = nullptr;
  std::shared_ptr<TextureProxy> coarseMaskProxy = nullptr;
  if (fineMask != nullptr) {
    fineMaskProxy = fineMask->lockTextureProxy(tpArgs);
  }
  if (coarseMask != nullptr) {
    coarseMaskProxy = coarseMask->lockTextureProxy(tpArgs);
  }

  auto coordMatrix = uvMatrix != nullptr ? *uvMatrix : Matrix::I();
  auto localParams = params;
  localParams.renderOffsetX = 0.0f;
  localParams.renderOffsetY = 0.0f;
  return GlassRefractionFragmentProcessor::Make(
      args.context->drawingAllocator(), std::move(sourceProxy), std::move(fineMaskProxy),
      std::move(coarseMaskProxy), localParams, coordMatrix);
}

}  // namespace tgfx

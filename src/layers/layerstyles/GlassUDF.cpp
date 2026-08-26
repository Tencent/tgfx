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

#include "GlassUDF.h"
#include "gpu/DrawingManager.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "layers/processors/GlassUDFTentBlurFragmentProcessor.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

bool GlassUDFRequest::isValid() const {
  if (source == nullptr || coreWidth <= 0 || coreHeight <= 0 || textureRect.isEmpty()) {
    return false;
  }
  auto& radius = field == GlassUDFField::Refraction ? fineRadius : coarseRadius;
  return radius.x > 0.0f && radius.y > 0.0f;
}

std::shared_ptr<TextureProxy> GenerateGlassUDFTexture(Context* context,
                                                      const GlassUDFRequest& request) {
  if (context == nullptr || !request.isValid()) {
    return nullptr;
  }
  auto& textureRect = request.textureRect;
  auto& fineRadius = request.fineRadius;
  auto& coarseRadius = request.coarseRadius;
  auto field = request.field;
  bool usesFine = field == GlassUDFField::Refraction;
  bool usesCoarse = field == GlassUDFField::EdgeLight;
  auto textureWidth = static_cast<int>(std::round(textureRect.width()));
  auto textureHeight = static_cast<int>(std::round(textureRect.height()));
  if (textureWidth <= 0 || textureHeight <= 0) {
    return nullptr;
  }
  SamplingArgs samplingArgs = {TileMode::Decal, TileMode::Decal,
                               SamplingOptions(FilterMode::Linear), SrcRectConstraint::Fast};
  auto allocator = context->drawingAllocator();
  float verticalHaloY = usesFine ? fineRadius.y : 0.0f;
  if (usesCoarse) {
    verticalHaloY = std::max(verticalHaloY, coarseRadius.y);
  }
  float verticalHalo = std::ceil(verticalHaloY) + 1.0f;
  auto horizontalRect = textureRect.makeOutset(0.0f, verticalHalo);
  horizontalRect.roundOut();
  auto horizontalHeight = static_cast<int>(std::round(horizontalRect.height()));
  auto horizontalTarget = RenderTargetProxy::Make(context, textureWidth, horizontalHeight, false, 1,
                                                  false, ImageOrigin::TopLeft, BackingFit::Exact);
  if (horizontalTarget == nullptr) {
    return nullptr;
  }
  float horizontalHaloX = usesFine ? fineRadius.x : 0.0f;
  if (usesCoarse) {
    horizontalHaloX = std::max(horizontalHaloX, coarseRadius.x);
  }
  float horizontalHalo = std::ceil(horizontalHaloX) + 1.0f;
  auto sourceDrawRect =
      Rect::MakeLTRB(-horizontalHalo, -1.0f, static_cast<float>(textureWidth) + horizontalHalo,
                     static_cast<float>(horizontalHeight) + 1.0f);
  // Resample the source to the core density first, then take the sampling window (textureRect plus
  // blur halos) out of it. Only the window is rasterized, because the full core can exceed the
  // texture limit at high zoom. Doing the resample before the window matters: its grid is then a
  // layer-wide constant, so every window lands on the same grid with the same phase. Resampling a
  // window instead ties the grid phase to the window's origin and size, and neighbouring cells
  // along one straight edge would then disagree on the field, which shows up as the edge light
  // changing width from one cell to the next.
  auto coreImage = request.source->makeScaled(request.coreWidth, request.coreHeight,
                                              SamplingOptions(FilterMode::Linear));
  if (coreImage == nullptr) {
    return nullptr;
  }
  auto coreWindowUDF =
      Rect::MakeLTRB(textureRect.left - horizontalHalo, horizontalRect.top - 1.0f,
                     textureRect.left + static_cast<float>(textureWidth) + horizontalHalo,
                     horizontalRect.top + static_cast<float>(horizontalHeight) + 1.0f);
  coreWindowUDF.roundOut();
  if (!coreWindowUDF.intersect(Rect::MakeWH(request.coreWidth, request.coreHeight))) {
    return nullptr;
  }
  auto coreSource = coreImage->makeSubset(coreWindowUDF);
  if (coreSource == nullptr) {
    return nullptr;
  }
  coreSource = coreSource->makeRasterized();
  if (coreSource == nullptr) {
    return nullptr;
  }
  // The window and textureRect are both integral in core-density space, so mapping the pass's local
  // coordinates onto the window is a plain integer translation: no fractional offset can differ
  // between windows.
  auto horizontalMatrix = Matrix::MakeTrans(textureRect.left - coreWindowUDF.left,
                                            horizontalRect.top - coreWindowUDF.top);
  FPArgs sourceArgs = FPArgs(context, 0, sourceDrawRect);
  // A pass reads a single field, so only the matching source is built and the other stays null.
  PlacementPtr<FragmentProcessor> horizontalFine = nullptr;
  PlacementPtr<FragmentProcessor> horizontalCoarse = nullptr;
  auto horizontalSource =
      FragmentProcessor::Make(coreSource, sourceArgs, samplingArgs, &horizontalMatrix);
  if (usesFine) {
    horizontalFine = std::move(horizontalSource);
  } else {
    horizontalCoarse = std::move(horizontalSource);
  }
  auto horizontalProcessor = GlassUDFTentBlurFragmentProcessor::Make(
      allocator, std::move(horizontalFine), std::move(horizontalCoarse), fineRadius.x,
      coarseRadius.x, GlassUDFBlurDirection::Horizontal, GlassUDFMaxTentRadius, false, field);
  if (horizontalProcessor == nullptr || !context->drawingManager()->fillRTWithFP(
                                            horizontalTarget, std::move(horizontalProcessor), 0)) {
    return nullptr;
  }
  auto verticalMatrix = Matrix::MakeTrans(0.0f, textureRect.top - horizontalRect.top);
  auto horizontalProxy = horizontalTarget->asTextureProxy();
  PlacementPtr<FragmentProcessor> verticalFineSource = nullptr;
  PlacementPtr<FragmentProcessor> verticalCoarseSource = nullptr;
  auto verticalSource =
      TiledTextureEffect::Make(allocator, horizontalProxy, samplingArgs, &verticalMatrix);
  if (usesFine) {
    verticalFineSource = std::move(verticalSource);
  } else {
    verticalCoarseSource = std::move(verticalSource);
  }
  auto verticalTarget = RenderTargetProxy::Make(context, textureWidth, textureHeight, false, 1,
                                                false, ImageOrigin::TopLeft, BackingFit::Exact);
  if (verticalTarget == nullptr) {
    return nullptr;
  }
  auto verticalProcessor = GlassUDFTentBlurFragmentProcessor::Make(
      allocator, std::move(verticalFineSource), std::move(verticalCoarseSource), fineRadius.y,
      coarseRadius.y, GlassUDFBlurDirection::Vertical, GlassUDFMaxTentRadius, true, field);
  if (verticalProcessor == nullptr ||
      !context->drawingManager()->fillRTWithFP(verticalTarget, std::move(verticalProcessor), 0)) {
    return nullptr;
  }
  return verticalTarget->asTextureProxy();
}

}  // namespace tgfx

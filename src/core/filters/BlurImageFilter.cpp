/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "BlurImageFilter.h"
#include "gpu/DrawingManager.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {
PlacementPtr<FragmentProcessor> BlurImageFilter::getSourceFragmentProcessor(
    std::shared_ptr<Image> source, Context* context, uint32_t renderFlags, const Rect& drawRect,
    const Point& scales) const {
  Matrix uvMatrix = Matrix::MakeScale(1 / scales.x, 1 / scales.y);
  uvMatrix.postTranslate(drawRect.left, drawRect.top);
  auto scaledDrawRect = drawRect;
  scaledDrawRect.scale(scales.x, scales.y);
  scaledDrawRect.round();
  FPArgs args =
      FPArgs(context, renderFlags, Rect::MakeWH(scaledDrawRect.width(), scaledDrawRect.height()),
             std::max(scales.x, scales.y));

  SamplingArgs samplingArgs = {};
  samplingArgs.tileModeX = tileMode;
  samplingArgs.tileModeY = tileMode;
  auto fp = FragmentProcessor::Make(source, args, samplingArgs, &uvMatrix);
  if (fp == nullptr) {
    return nullptr;
  }
  if (fp->numCoordTransforms() == 1) {
    return fp;
  }
  auto renderTarget = RenderTargetProxy::MakeFallback(
      context, static_cast<int>(scaledDrawRect.width()), static_cast<int>(scaledDrawRect.height()),
      source->isAlphaOnly(), 1);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  context->drawingManager()->fillRTWithFP(renderTarget, std::move(fp), renderFlags);
  return TiledTextureEffect::Make(renderTarget->asTextureProxy(), samplingArgs);
}

}  // namespace tgfx
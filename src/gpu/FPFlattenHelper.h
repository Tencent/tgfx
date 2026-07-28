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

#pragma once

#include "gpu/AOTMaterializationPolicy.h"
#include "gpu/DrawingManager.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

/**
 * Returns true if the given FragmentProcessor is simple enough to be used directly as a child of
 * XfermodeFragmentProcessor without causing a permutation miss. The childIndex parameter specifies
 * the position (0 = src, 1 = dst). ConstColorProcessor and TiledTextureEffect are only supported
 * as child[0]; child[1] must be TextureEffect or nullptr.
 */
static inline bool IsSimpleBlendChild(const FragmentProcessor* fp, size_t childIndex = 0) {
  if (fp == nullptr) {
    return true;
  }
  auto fpName = fp->name();
  if (fpName == "TextureEffect") {
    auto* te = static_cast<const TextureEffect*>(fp);
    return !te->isYUV() && te->numTextureSamplers() > 0;
  }
  if (childIndex == 0) {
    if (fpName == "ConstColorProcessor") {
      return true;
    }
    if (fpName == "TiledTextureEffect") {
      return true;
    }
  }
  return false;
}

/**
 * Renders a complex FragmentProcessor into an offscreen texture and returns a simple TextureEffect
 * that samples from it. The FP's coordTransform expects to receive coordinates in the range
 * [drawRect.x..right, drawRect.y..bottom], so a coordOffset is applied during offscreen rendering
 * to ensure the FP sees the correct coordinate space. Returns nullptr on failure.
 *
 * The offscreen render target uses BackingFit::Exact: the returned TextureEffect samples with a
 * plain translate-only uvMatrix, which is only correct when the backing texture is exactly
 * width x height. An Approx (rounded-up) backing would make the normalized texture coordinates
 * sample a scaled/offset region (reading uninitialized padding), producing corrupted output.
 */
static inline PlacementPtr<FragmentProcessor> FlattenToTexture(const FPArgs& args,
                                                               PlacementPtr<FragmentProcessor> fp) {
  auto context = args.context;
  auto drawRect = args.drawRect;
  drawRect.roundOut();
  int width = static_cast<int>(drawRect.width());
  int height = static_cast<int>(drawRect.height());
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  auto renderTarget = RenderTargetProxy::Make(context, width, height, false, 1, false,
                                              ImageOrigin::TopLeft, BackingFit::Exact);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto drawingManager = context->drawingManager();
  auto coordOffset = Point::Make(drawRect.x(), drawRect.y());
  if (!drawingManager->fillRTWithFP(renderTarget, std::move(fp), args.renderFlags, coordOffset)) {
    return nullptr;
  }
  auto textureProxy = renderTarget->asTextureProxy();
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto uvMatrix = Matrix::MakeTrans(-drawRect.x(), -drawRect.y());
  auto allocator = context->drawingAllocator();
  return TextureEffect::Make(allocator, std::move(textureProxy), {}, &uvMatrix);
}

/**
 * Ensures the given FragmentProcessor is simple for use as a blend child at the specified position.
 * If it's already simple for that position, returns it unchanged; otherwise flattens it to a
 * texture. Returns nullptr on failure.
 *
 * Two independent reasons trigger flattening:
 *  - Correctness: the child is too complex to be a valid XfermodeFragmentProcessor child
 *    (IsSimpleBlendChild returns false). This always flattens, regardless of any AOT setting.
 *  - AOT matchability: the child is valid inline but its permutation has no precompiled artifact
 *    (e.g. a TiledTextureEffect src). This flattens only when the decomposition route is enabled,
 *    so the default path is byte-for-byte identical to before and no baseline shifts.
 */
static inline PlacementPtr<FragmentProcessor> EnsureSimpleBlendChild(
    const FPArgs& args, PlacementPtr<FragmentProcessor> fp, size_t childIndex = 0) {
  if (!IsSimpleBlendChild(fp.get(), childIndex)) {
    return FlattenToTexture(args, std::move(fp));
  }
  auto cache = args.context->precompiledShaderCache();
  if (cache != nullptr && cache->decompositionEnabled()) {
    auto decision = AOTMaterializationPolicy::Evaluate(
        fp.get(), MaterializationConsumer::PointwiseBlend, childIndex);
    if (decision.shouldFlatten) {
      return FlattenToTexture(args, std::move(fp));
    }
  }
  return fp;
}

}  // namespace tgfx

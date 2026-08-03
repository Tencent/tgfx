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
 * Renders a complex FragmentProcessor into an offscreen texture and returns a simple TextureEffect
 * that samples from it. The FP's coordTransform expects to receive coordinates in the range
 * [drawRect.x..right, drawRect.y..bottom], so a coordOffset is applied during offscreen rendering
 * to ensure the FP sees the correct coordinate space. apronRadius expands the captured area on every
 * side; pass the value the materialization policy produced for this consumer. Returns nullptr on
 * failure.
 */
static inline PlacementPtr<FragmentProcessor> FlattenToTexture(const FPArgs& args,
                                                               PlacementPtr<FragmentProcessor> fp,
                                                               float apronRadius = 0.0f) {
  auto context = args.context;
  auto target = AOTMaterializationPolicy::PrepareTarget(context, args.drawRect, apronRadius);
  if (target.renderTarget == nullptr) {
    return nullptr;
  }
  auto drawingManager = context->drawingManager();
  if (!drawingManager->fillRTWithFP(target.renderTarget, std::move(fp), args.renderFlags,
                                    target.coordOffset)) {
    return nullptr;
  }
  auto textureProxy = target.renderTarget->asTextureProxy();
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto uvMatrix = Matrix::MakeTrans(-target.bounds.left, -target.bounds.top);
  auto cache = context->precompiledShaderCache();
  if (cache != nullptr && cache->diagnosticRecordingEnabled()) {
    cache->recordMaterializedEdge(target.byteSize);
  }
  auto allocator = context->drawingAllocator();
  return TextureEffect::Make(allocator, std::move(textureProxy), {}, &uvMatrix);
}

/**
 * Ensures the given FragmentProcessor is simple for use as a blend child at the specified position.
 * If it's already simple for that position, returns it unchanged; otherwise flattens it to a
 * texture. Returns nullptr on failure.
 *
 * Two independent reasons trigger flattening, both decided by AOTMaterializationPolicy:
 *  - Correctness: the child is too complex to be a valid XfermodeFragmentProcessor child. This
 *    always flattens, regardless of any AOT setting.
 *  - AOT matchability: the child is valid inline but its permutation has no precompiled artifact
 *    (e.g. a TiledTextureEffect src). This flattens only when the decomposition route is enabled and
 *    the draw is not already inside a nested rasterization, so the default path is byte-for-byte
 *    identical to before and no baseline shifts.
 */
static inline PlacementPtr<FragmentProcessor> EnsureSimpleBlendChild(
    const FPArgs& args, PlacementPtr<FragmentProcessor> fp, size_t childIndex = 0) {
  auto decision = AOTMaterializationPolicy::Evaluate(
      fp.get(), MaterializationConsumer::PointwiseBlend, childIndex);
  if (decision.requiredForCorrectness) {
    return FlattenToTexture(args, std::move(fp), decision.apronRadius);
  }
  if ((args.renderFlags & InternalRenderFlags::NestedRasterization) != 0) {
    return fp;
  }
  auto cache = args.context->precompiledShaderCache();
  if (cache != nullptr && cache->decompositionEnabled() && decision.shouldFlatten) {
    return FlattenToTexture(args, std::move(fp), decision.apronRadius);
  }
  return fp;
}

}  // namespace tgfx

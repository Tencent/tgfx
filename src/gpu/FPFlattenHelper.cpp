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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/FPFlattenHelper.h"
#include <cstdlib>
#include "gpu/DrawingManager.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

// TGFX_AOT_DISABLE runs the pure runtime route, which must include the blend-child
// materialization: main renders these children inline, so the rewrite is itself part of the
// AOT-era design. Gating it keeps runtime-only runs (whole-suite A/B comparisons and the
// main-baseline three-way diff) from comparing two paths that share the same rewrite. The flag
// only ever changes diagnostic runs; the default route is untouched. Read once per process.
static bool RuntimeRouteOnly() {
  static const bool runtimeOnly = std::getenv("TGFX_AOT_DISABLE") != nullptr;
  return runtimeOnly;
}

// P4: legacy switch for the first migration group. Default off: BlendShader keeps its original
// tree and the in-plan retry in OpsCompositor materializes only when the retry is warranted.
static bool LegacyBlendMaterialization() {
  static const bool legacy = std::getenv("TGFX_AOT_LEGACY_BLEND_MATERIALIZATION") != nullptr;
  return legacy;
}

bool BlendChildMaterializationIsLegacy() {
  return LegacyBlendMaterialization();
}

PlacementPtr<FragmentProcessor> FlattenToTexture(const FPArgs& args,
                                                 PlacementPtr<FragmentProcessor> fp,
                                                 float apronRadius) {
  auto context = args.context;
  auto target = AOTMaterializationPolicy::PrepareTarget(context, args.drawRect, apronRadius);
  if (target.renderTarget == nullptr) {
    return nullptr;
  }
  auto drawingManager = context->drawingManager();
  if (!drawingManager->fillRTWithFP(target.renderTarget, std::move(fp), args.renderFlags,
                                    target.geometry.coordOffset, OffscreenFillSource::FPFlatten)) {
    return nullptr;
  }
  auto textureProxy = target.renderTarget->asTextureProxy();
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto uvMatrix = Matrix::MakeTrans(-target.geometry.bounds.left, -target.geometry.bounds.top);
  auto cache = context->precompiledShaderCache();
  if (cache != nullptr && cache->diagnosticRecordingEnabled()) {
    cache->recordMaterializedEdge(target.geometry.byteSize());
  }
  auto allocator = context->drawingAllocator();
  return TextureEffect::Make(allocator, std::move(textureProxy), {}, &uvMatrix);
}

PlacementPtr<FragmentProcessor> EnsureSimpleBlendChild(const FPArgs& args,
                                                       PlacementPtr<FragmentProcessor> fp,
                                                       size_t childIndex) {
  if (RuntimeRouteOnly()) {
    return fp;
  }
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

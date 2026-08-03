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

#include "gpu/AOTMaterializationPolicy.h"
#include "gpu/BackingFit.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {

static bool IsPointwiseBlendLeafMatchable(const FragmentProcessor* fp, size_t childIndex) {
  if (fp == nullptr) {
    return true;
  }
  auto name = fp->name();
  if (name == "TextureEffect") {
    auto* te = static_cast<const TextureEffect*>(fp);
    return !te->isYUV() && te->numTextureSamplers() > 0;
  }
  // ConstColorProcessor is a matchable leaf only at the src position; the precompiled blend kernels
  // carry a constant-color src variant but not a constant-color dst.
  if (childIndex == 0 && name == "ConstColorProcessor") {
    return true;
  }
  return false;
}

// Whether XfermodeFragmentProcessor can take this child inline at all. Wider than matchability: a
// TiledTextureEffect src is a valid child even though no precompiled blend kernel covers it, so it
// is legal to keep inline but still worth materializing for AOT.
static bool IsPointwiseBlendLeafLegal(const FragmentProcessor* fp, size_t childIndex) {
  if (IsPointwiseBlendLeafMatchable(fp, childIndex)) {
    return true;
  }
  return childIndex == 0 && fp != nullptr && fp->name() == "TiledTextureEffect";
}

MaterializationDecision AOTMaterializationPolicy::Evaluate(const FragmentProcessor* child,
                                                           MaterializationConsumer consumer,
                                                           size_t childIndex) {
  MaterializationDecision decision = {};
  switch (consumer) {
    case MaterializationConsumer::PointwiseBlend:
      // A pointwise blend consumes the child at the output coordinate, so it needs no sampling
      // radius. It still needs the one-pixel boundary apron: an offset shadow mask reads just past
      // the draw bounds, and without the apron that read clamps to the edge texels and turns the
      // transparent border opaque. Measured on the full Metal suite, a wider apron changes nothing,
      // so one pixel is the exact requirement rather than a safety margin.
      decision.requiredForCorrectness = !IsPointwiseBlendLeafLegal(child, childIndex);
      decision.shouldFlatten = !IsPointwiseBlendLeafMatchable(child, childIndex);
      decision.apronRadius = 1.0f;
      break;
  }
  return decision;
}

MaterializedGeometry AOTMaterializationPolicy::PrepareGeometry(const Rect& drawBounds,
                                                               float apronRadius) {
  auto bounds = drawBounds;
  bounds.outset(apronRadius, apronRadius);
  bounds.roundOut();
  MaterializedGeometry geometry = {};
  geometry.bounds = bounds;
  geometry.coordOffset = Point::Make(bounds.left, bounds.top);
  geometry.width = static_cast<int>(bounds.width());
  geometry.height = static_cast<int>(bounds.height());
  return geometry;
}

MaterializedTarget AOTMaterializationPolicy::AllocateTarget(Context* context,
                                                            const MaterializedGeometry& geometry) {
  if (context == nullptr || geometry.isEmpty()) {
    return {};
  }
  // BackingFit::Exact is required, not a preference: consumers sample this texture with a
  // translate-only matrix, which is only correct when the backing texture is exactly width x height.
  // An Approx (rounded-up) backing would make the normalized coordinates address a scaled region and
  // read uninitialized padding.
  auto renderTarget = RenderTargetProxy::Make(context, geometry.width, geometry.height, false, 1,
                                              false, ImageOrigin::TopLeft, BackingFit::Exact);
  if (renderTarget == nullptr) {
    return {};
  }
  MaterializedTarget target = {};
  target.renderTarget = std::move(renderTarget);
  target.geometry = geometry;
  return target;
}

MaterializedTarget AOTMaterializationPolicy::PrepareTarget(Context* context, const Rect& drawBounds,
                                                           float apronRadius) {
  return AllocateTarget(context, PrepareGeometry(drawBounds, apronRadius));
}

}  // namespace tgfx

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
      decision.shouldFlatten = !IsPointwiseBlendLeafMatchable(child, childIndex);
      decision.apronRadius = 1.0f;
      break;
  }
  return decision;
}

MaterializedTarget AOTMaterializationPolicy::PrepareTarget(Context* context, const Rect& drawBounds,
                                                           float apronRadius) {
  if (context == nullptr) {
    return {};
  }
  auto bounds = drawBounds;
  bounds.outset(apronRadius, apronRadius);
  bounds.roundOut();
  auto width = static_cast<int>(bounds.width());
  auto height = static_cast<int>(bounds.height());
  if (width <= 0 || height <= 0) {
    return {};
  }
  // BackingFit::Exact is required, not a preference: consumers sample this texture with a
  // translate-only matrix, which is only correct when the backing texture is exactly width x height.
  // An Approx (rounded-up) backing would make the normalized coordinates address a scaled region and
  // read uninitialized padding.
  auto renderTarget = RenderTargetProxy::Make(context, width, height, false, 1, false,
                                              ImageOrigin::TopLeft, BackingFit::Exact);
  if (renderTarget == nullptr) {
    return {};
  }
  MaterializedTarget target = {};
  target.renderTarget = std::move(renderTarget);
  target.bounds = bounds;
  target.coordOffset = Point::Make(bounds.left, bounds.top);
  target.byteSize = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4;
  return target;
}

}  // namespace tgfx

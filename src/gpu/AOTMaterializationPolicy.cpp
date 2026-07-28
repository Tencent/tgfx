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
      // A pointwise blend consumes the child at the output coordinate, so no apron is needed. Any
      // child that is not an AOT-leaf-matchable shape (e.g. TiledTextureEffect) is flattened to a
      // plain TextureEffect so the downstream Xfermode kernel matches a precompiled artifact.
      decision.shouldFlatten = !IsPointwiseBlendLeafMatchable(child, childIndex);
      decision.apronRadius = 0.0f;
      break;
  }
  return decision;
}

}  // namespace tgfx

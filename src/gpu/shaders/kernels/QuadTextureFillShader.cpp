/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/shaders/kernels/QuadTextureFillShader.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/shaders/contract/DrawFacts.h"

namespace tgfx {

bool QuadTextureFillShader::Accept(const DrawShape& shape) {
  if (shape.geometry.kind != GeometryKind::QuadPerEdgeAA) {
    return false;
  }
  // Color must be exactly one TextureEffect. Coverage is optional: a device-space mask maps to
  // a runtime uniform, while a local-space texture-alpha mask maps to HAS_LOCAL_MASK.
  if (shape.colorFPCount != 1) {
    return false;
  }
  if (shape.fragmentCount != 1) {
    if (shape.coverage == Coverage::AARect || shape.coverage == Coverage::RRect) {
      // Evaluated through the runtime clip contract uniforms.
    } else if (shape.coverage == Coverage::DeviceMask) {
      // Device masks are a runtime uniform now, so there is nothing to encode.
    } else if (shape.coverage == Coverage::LocalMask) {
      // Encoded by HAS_LOCAL_MASK in Select.
    } else {
      // Unrecognized coverage kinds (including unclassifiable ones) stay on other routes.
      return false;
    }
  }
  if (shape.xpType() < XP_NONE) {
    return false;
  }
  if (shape.ir == nullptr) {
    // A lowering-refused source (a plain gray/RG read, or a view-less proxy with no pending
    // upload) has no graph to match: no family serves it.
    return false;
  }
  // The single color processor is the IR's bare texture leaf (no operators above it; the leaf's
  // existence implies its proxy — the failed-instantiation corner the old sampler count guarded
  // is not a supported state, the uploads run before the draws they feed).
  AOTChainBuilder::AOTTextureTailShape tail = {};
  if (!AOTChainBuilder::CollectTextureTail(shape.ir->color, &tail) || !tail.ops.empty() ||
      tail.leaf->isYUV || tail.leaf->samplingKind != AOTTextureSamplingKind::Plain) {
    return false;
  }
  // The precompiled vTexSubset path transforms the subset corners WITHOUT a perspective divide,
  // while the runtime route's emitTransformedPoint divides by w when the transform is projective.
  // A projective source carrying a GP subset would clamp the texture coordinate to wrong bounds,
  // so it must stay on the runtime route. Projective draws without a GP subset are fine.
  if (shape.geometry.hasSubset && (shape.geometry.hasUVPerspective || tail.leaf->hasPerspective)) {
    return false;
  }
  return true;
}

Variants QuadTextureFillShader::Select(const DrawShape& shape) {
  Variants variants;
  int hasSubset = shape.geometry.hasSubset ? 1 : 0;
  int hasLocalMask = shape.coverage == Coverage::LocalMask ? 1 : 0;
  variants.vert("HAS_UV_COORD", shape.geometry.hasUVMatrix ? 0 : 1)
      .vert("HAS_SUBSET", hasSubset)
      .vert("HAS_LOCAL_MASK", hasLocalMask)
      .frag("HAS_SUBSET", hasSubset)
      .frag(XP_DEFINE_NAME, shape.xpType())
      .frag("HAS_LOCAL_MASK", hasLocalMask);
  return variants;
}

}  // namespace tgfx

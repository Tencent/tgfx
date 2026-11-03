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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/shaders/kernels/QuadTextureFillShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/aot/AOTChainBuilder.h"
#include "gpu/aot/DrawIR.h"
#include "gpu/aot/DrawShape.h"

namespace tgfx {

std::optional<QuadTextureFillShader::Inputs> QuadTextureFillShader::ExtractFacts(
    const DrawShape& facts, PermutationMatchFailure*) {
  if (facts.geometry.kind != GeometryKind::QuadPerEdgeAA) {
    return std::nullopt;
  }
  // Color must be exactly one TextureEffect. Coverage is optional: a device-space mask maps to
  // HAS_MASK_TEXTURE, while a local-space texture-alpha mask maps to HAS_LOCAL_MASK.
  if (facts.colorFPCount != 1) {
    return std::nullopt;
  }
  bool hasLocalMask = false;
  if (facts.fragmentCount != 1) {
    if (facts.coverage == Coverage::AARect || facts.coverage == Coverage::RRect) {
      // Evaluated through the runtime clip contract uniforms.
    } else if (facts.coverage == Coverage::DeviceMask) {
      // Device masks are a runtime uniform now, so there is nothing to encode.
    } else if (facts.coverage == Coverage::LocalMask) {
      hasLocalMask = true;
    } else {
      // Unrecognized coverage kinds (including unclassifiable ones) stay on other routes.
      return std::nullopt;
    }
  }
  int xpType = facts.xpType();
  if (xpType < XP_NONE) {
    return std::nullopt;
  }
  if (facts.ir == nullptr) {
    // A lowering-refused source (a plain gray/RG read, or a view-less proxy with no pending
    // upload) has no graph to match: no family serves it.
    return std::nullopt;
  }
  // The single color processor is the IR's bare texture leaf (no operators above it; the leaf's
  // existence implies its proxy — the failed-instantiation corner the old sampler count guarded
  // is not a supported state, the uploads run before the draws they feed).
  AOTChainBuilder::AOTTextureTailShape tail = {};
  if (!AOTChainBuilder::CollectTextureTail(facts.ir->color, &tail) || !tail.ops.empty() ||
      tail.leaf->isYUV || tail.leaf->samplingKind != AOTTextureSamplingKind::Plain) {
    return std::nullopt;
  }
  // No color-texture guards are needed for the local mask: the coverage texture writes its
  // per-texture uniforms (Subset/AlphaOnly/HasRgbaaa) under a distinct structural ordinal
  // (Subset_1, ...), so it can no longer clobber the color texture's Subset / AlphaOnly slots. The
  // color may therefore carry a subset, be alpha-only, or use RGBAAA freely.
  // Subset clamping logic:
  //   gpSubset=true  -> HAS_SUBSET=1: vertex shader outputs vTexSubset varying, fragment double-
  //                     clamps by the varying and the Subset uniform.
  //   gpSubset=false -> HAS_SUBSET=0: fragment always clamps by the Subset uniform alone. The
  //                     uniform is populated from computeSubsetRect, which yields the full texture
  //                     bounds when the source has no real subset, so the clamp is a no-op. This
  //                     subsumes the former HAS_CLAMP_SUBSET dimension.
  Inputs inputs;
  inputs.hasUVCoord = !facts.geometry.hasUVMatrix;
  inputs.hasSubset = facts.geometry.hasSubset;
  // The precompiled vTexSubset path transforms the subset corners WITHOUT a perspective divide
  // (quad_texture_fill.vert: perspLT = (CoordTransformMatrix_0 * vec3(src, 1)).xy), while the
  // runtime route's emitTransformedPoint divides by w when the transform is projective. A
  // projective source carrying a GP subset would clamp the texture coordinate to wrong bounds
  // (one edge-line class of A/B divergence), so it must stay on the runtime route — the same
  // conservative policy the plain-coordinate kernels apply to projective sources. Projective
  // draws without a GP subset are fine: TransformedCoords_0 is a vec3 varying that the fragment
  // shader divides.
  if (inputs.hasSubset && (facts.geometry.hasUVPerspective || tail.leaf->hasPerspective)) {
    return std::nullopt;
  }
  inputs.hasLocalMask = hasLocalMask;
  inputs.xpType = xpType;
  return inputs;
}

}  // namespace tgfx

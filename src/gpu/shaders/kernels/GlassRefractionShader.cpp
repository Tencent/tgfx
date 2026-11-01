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

#include "gpu/shaders/kernels/GlassRefractionShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/aot/AOTDrawFacts.h"
#include "gpu/processors/PorterDuffXferProcessor.h"

namespace tgfx {

std::optional<GlassRefractionShader::Inputs> GlassRefractionShader::Extract(
    const ProgramInfo* programInfo, PermutationMatchFailure* failure) {
  // The executor-table row and the walk share this one body.
  return ExtractFacts(ExtractAOTDrawFacts(programInfo), failure);
}

std::optional<GlassRefractionShader::Inputs> GlassRefractionShader::ExtractFacts(
    const AOTDrawFacts& facts, PermutationMatchFailure*) {
  if (!ServesAlphaOnly && facts.alphaOnlyTarget) {
    return std::nullopt;
  }
  // The geometry selects the vertex layout and the initial-coverage source. Both forms must be the
  // common-color attribute layout: the kernels read the Color uniform, and a per-vertex color
  // draw would quantize fractional paint colors through the UByte4 attribute.
  int gpKind = -1;
  if (facts.geometry.kind == AOTGeometryKind::Ellipse) {
    if (!facts.geometry.hasCommonColor) {
      return std::nullopt;
    }
    gpKind = 0;
  } else if (facts.geometry.kind == AOTGeometryKind::QuadPerEdgeAA) {
    // The uvMatrix form is required: it declares no uvCoord attribute, so the GP transforms the
    // position for the background coordinate, which is what the quad vertex kernel reads. A
    // subset-carrying quad has a different transform contract and stays on the fallback route.
    if (!facts.geometry.hasCommonColor || !facts.geometry.hasUVMatrix ||
        facts.geometry.hasSubset || facts.geometry.hasUVPerspective) {
      return std::nullopt;
    }
    gpKind = 1;
  } else {
    return std::nullopt;
  }
  if (facts.colorFPCount != 1) {
    return std::nullopt;
  }
  // The only coverage processor the kernel takes is the quad form's local shape mask (a SrcIn
  // ShaderMaskFilter over a hardware-tiled RGBA texture), applied through the HasLocalMask runtime
  // branch. It needs an affine mask transform, like the background coordinate below — the mask
  // leaf's perspective fact is read off the IR, exactly what the coverage processor's child
  // transform reported.
  if (facts.fragmentCount != 1) {
    auto* mask = LocalMaskCoverageLeaf(facts.ir);
    if (gpKind != 1 || facts.fragmentCount != 2 || !facts.coverage ||
        *facts.coverage != CoverageKind::LocalMask || mask == nullptr ||
        mask->hasPerspective) {
      return std::nullopt;
    }
  }
  const int xpType = facts.xpType();
  if (xpType < XP_NONE) {
    return std::nullopt;
  }
  // The color expression reads off the carried IR's opaque-kernel node: the refraction's barrier
  // sits directly on the geometry color, with its variant dimensions on the node. The geometry
  // child selects the compile-time dimension because it changes the sampler layout; dispersion is
  // a compile-time branch too: the runtime emission compiles one path or the other and never
  // both, so the precompiled variant must mirror that structure exactly to keep pixels identical
  // (see glass_refraction.frag). Lighting stays a runtime uniform. The node also carries the
  // background-coordinate perspective rejection: both vertex kernels emit the affine coordinate
  // form ((Matrix * vec3(pos, 1)).xy computed in the vertex stage), matching
  // GeometryProcessor::emitTransforms' non-perspective branch — a projective transform makes the
  // runtime emit a vec3 varying divided in the fragment stage, so it must ride the runtime route.
  auto shape = ReadOpaqueKernelIRShape(facts.ir, /*kernelKind=*/1,
                                       /*requiresTextureChild=*/false);
  if (!shape || shape->kernel->geometryKind < 0 || shape->kernel->hasPerspective) {
    return std::nullopt;
  }
  Inputs inputs;
  inputs.gpKind = gpKind;
  inputs.geometryKind = shape->kernel->geometryKind;
  inputs.dispersionOn = shape->kernel->dispersionOn;
  inputs.xpType = xpType;
  return inputs;
}

}  // namespace tgfx

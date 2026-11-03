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

#include "gpu/shaders/kernels/GlassRefractionShader.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/aot/DrawIR.h"
#include "gpu/aot/DrawShape.h"

namespace tgfx {

bool GlassRefractionShader::Accept(const DrawShape& shape) {
  // The geometry selects the vertex layout and the initial-coverage source. Both forms must be the
  // common-color attribute layout: the kernels read the Color uniform, and a per-vertex color
  // draw would quantize fractional paint colors through the UByte4 attribute.
  if (shape.geometry.kind == GeometryKind::Ellipse) {
    if (!shape.geometry.hasCommonColor) {
      return false;
    }
  } else if (shape.geometry.kind == GeometryKind::QuadPerEdgeAA) {
    // The uvMatrix form is required: it declares no uvCoord attribute, so the GP transforms the
    // position for the background coordinate, which is what the quad vertex kernel reads. A
    // subset-carrying quad has a different transform contract and stays on the fallback route.
    if (!shape.geometry.hasCommonColor || !shape.geometry.hasUVMatrix || shape.geometry.hasSubset ||
        shape.geometry.hasUVPerspective) {
      return false;
    }
  } else {
    return false;
  }
  if (shape.colorFPCount != 1) {
    return false;
  }
  // The only coverage processor the kernel takes is the quad form's local shape mask (a SrcIn
  // ShaderMaskFilter over a hardware-tiled RGBA texture), applied through the HasLocalMask runtime
  // branch. It needs an affine mask transform, like the background coordinate below — the mask
  // leaf's perspective fact is read off the IR, exactly what the coverage processor's child
  // transform reported.
  if (shape.fragmentCount != 1) {
    auto* mask = LocalMaskCoverageLeaf(shape.ir);
    if (shape.geometry.kind != GeometryKind::QuadPerEdgeAA || shape.fragmentCount != 2 ||
        !shape.coverage.has_value() || shape.coverage != Coverage::LocalMask || mask == nullptr ||
        mask->hasPerspective) {
      return false;
    }
  }
  if (shape.xpType() < XP_NONE) {
    return false;
  }
  // The color expression reads off the carried IR's opaque-kernel node: the refraction's barrier
  // sits directly on the geometry color, with its variant dimensions on the node. The geometry
  // child selects the compile-time dimension because it changes the sampler layout. The node
  // also carries the background-coordinate perspective rejection: both vertex kernels emit the
  // affine coordinate form, so a projective transform must ride the runtime route.
  auto irShape =
      ReadOpaqueKernelIRShape(shape.ir, /*kernelKind=*/1, /*requiresTextureChild=*/false);
  return irShape && irShape->kernel->geometryKind >= 0 && !irShape->kernel->hasPerspective;
}

Variants GlassRefractionShader::Select(const DrawShape& shape) {
  // gpKind mirrors the geometry; the geometry child and the dispersion flag re-read the same
  // opaque-kernel node the acceptance verified (the match cache bounds the redundancy: one
  // extraction per distinct program shape).
  int gpKind = shape.geometry.kind == GeometryKind::Ellipse ? 0 : 1;
  auto irShape =
      ReadOpaqueKernelIRShape(shape.ir, /*kernelKind=*/1, /*requiresTextureChild=*/false);
  int geometryKind = irShape ? irShape->kernel->geometryKind : 0;
  int dispersionOn = irShape && irShape->kernel->dispersionOn ? 1 : 0;
  Variants variants;
  variants.vert("GP_KIND", gpKind)
      .frag("GP_KIND", gpKind)
      .frag("GEOMETRY_KIND", geometryKind)
      .frag("DISPERSION_ON", dispersionOn)
      .frag(XP_DEFINE_NAME, shape.xpType());
  return variants;
}

}  // namespace tgfx

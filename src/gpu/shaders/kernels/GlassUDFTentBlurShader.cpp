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

#include "gpu/shaders/kernels/GlassUDFTentBlurShader.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/shaders/contract/DrawFacts.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"

namespace tgfx {

bool GlassUDFTentBlurShader::Accept(const DrawShape& shape) {
  if (shape.geometry.kind != GeometryKind::Default &&
      shape.geometry.kind != GeometryKind::QuadPerEdgeAA) {
    return false;
  }
  if (shape.colorFPCount != 1) {
    return false;
  }
  // The shared device-mask ABI gate (SharedDeviceMaskValue's semantics): an unclassifiable or
  // local-mask coverage stays on other routes.
  if (!shape.coverage.has_value() || shape.coverage == Coverage::LocalMask) {
    return false;
  }
  if (shape.xpType() < XP_NONE) {
    return false;
  }
  // The color expression reads off the carried IR's opaque-kernel node: the tent blur's barrier
  // over a single servable texture leaf (the horizontal pass reads a plain TextureEffect, the
  // vertical a TiledTextureEffect — the node's child leaf carries the shape).
  auto irShape = ReadOpaqueKernelIRShape(shape.ir, AOTOpaqueKernel::GlassUDFTentBlur,
                                       /*requiresTextureChild=*/true);
  if (!irShape) {
    return false;
  }
  // The loop upper bound is the fixed constant 64; a longer max radius exceeds it and falls back.
  return irShape->kernel->maxRadius <= 64;
}

Variants GlassUDFTentBlurShader::Select(const DrawShape& shape) {
  Variants variants;
  variants.frag(XP_DEFINE_NAME, shape.xpType());
  return variants;
}

}  // namespace tgfx

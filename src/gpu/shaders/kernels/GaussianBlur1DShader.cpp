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

#include "gpu/shaders/kernels/GaussianBlur1DShader.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"
#include "gpu/shaders/contract/DrawFacts.h"

namespace tgfx {

bool GaussianBlur1DShader::Accept(const DrawShape& shape) {
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
  // The color expression reads off the carried IR's opaque-kernel node: the blur's barrier over
  // a single servable texture leaf (the node IS the contract; no processor-tree walk).
  auto irShape = ReadOpaqueKernelIRShape(shape.ir, AOTOpaqueKernel::GaussianBlur1D,
                                         /*requiresTextureChild=*/true);
  if (!irShape) {
    return false;
  }
  // The kernel radius is a runtime uniform, not a variant dimension. The precompiled shader uses
  // a fixed maximum kernel (MAX_SIGMA=9 -> radius up to 10) and breaks early at the runtime
  // radius, so any sigma in [1,10] maps to the same variant. Sigma > 10 exceeds the fixed kernel
  // and falls back.
  return irShape->kernel->sigma >= 1 && irShape->kernel->sigma <= 10;
}

Variants GaussianBlur1DShader::Select(const DrawShape& shape) {
  Variants variants;
  variants.frag(XP_DEFINE_NAME, shape.xpType());
  return variants;
}

}  // namespace tgfx

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

#include "gpu/shaders/kernels/TexturedEffectShader.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/plan/AOTChainBuilder.h"
#include "gpu/aot/select/DrawShape.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"

namespace tgfx {

bool TexturedEffectShader::Accept(const DrawShape& shape) {
  if (!shape.positionOnlyRect()) {
    return false;
  }
  // The kernel applies only the vertex coverage: a draw carrying any coverage processor (an
  // analytic clip, a device mask, a coverage subtree) would have it silently dropped, so such
  // draws must stay off this family.
  if (shape.fragmentCount != shape.colorFPCount) {
    return false;
  }
  if (shape.xpType() < XP_NONE) {
    return false;
  }
  // The two structurally-equivalent forms the old FP walk matched (a Compose wrapper and a
  // sequential pair) lower to this ONE graph — texture leaf plus exactly one operator — so the
  // dual-form match is gone with the IR.
  if (shape.ir == nullptr) {
    return false;
  }
  AOTChainBuilder::AOTTextureTailShape tail = {};
  if (!AOTChainBuilder::CollectTextureTail(shape.ir->color, &tail) || tail.ops.size() != 1) {
    return false;
  }
  auto* leaf = tail.leaf;
  if (leaf->samplingKind != AOTTextureSamplingKind::Plain || leaf->isYUV || leaf->isAlphaOnly ||
      leaf->hasRGBAAA) {
    return false;
  }
  // The precompiled vertex template transforms the coordinate affinely; a projective source must
  // stay off this artifact (the plain templates transform coordinates affinely, with no w division).
  if (leaf->hasPerspective) {
    return false;
  }
  // All four pointwise operators (ColorMatrix / Luma / AlphaThreshold / ColorSpaceXform) share the
  // same structural class (1 sampler, no CoverageFP, RGBA) and thus the same precompiled shader.
  // The operator is selected at draw time by the OpType runtime uniform, so it is not a
  // compile-time dimension. The color-space one needs the fill kernel's transfer-function check
  // over the node's steps.
  auto* op = shape.ir->color.nodeAt(tail.ops[0]);
  bool supported = op->kind == AOTEffectKind::ColorMatrix || op->kind == AOTEffectKind::Luma ||
                   op->kind == AOTEffectKind::AlphaThreshold;
  if (!supported && op->kind == AOTEffectKind::ColorSpaceXform) {
    auto steps = std::get_if<AOTColorSpaceXformParameters>(&op->parameters);
    supported = steps != nullptr && SupportedFillKernelSteps(steps->steps.get());
  }
  return supported;
}

Variants TexturedEffectShader::Select(const DrawShape& shape) {
  Variants variants;
  variants.frag(XP_DEFINE_NAME, shape.xpType());
  return variants;
}

}  // namespace tgfx

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

#include "gpu/shaders/kernels/TexturedEffectShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/aot/AOTChainBuilder.h"
#include "gpu/aot/DrawShape.h"
#include "gpu/processors/AlphaThresholdFragmentProcessor.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/ComposeFragmentProcessor.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/LumaFragmentProcessor.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {

std::optional<TexturedEffectShader::Inputs> TexturedEffectShader::ExtractFacts(
    const DrawShape& facts, PermutationMatchFailure*) {
  if (!facts.positionOnlyRect()) {
    return std::nullopt;
  }
  // The kernel applies only the vertex coverage: a draw carrying any coverage processor (an
  // analytic clip, a device mask, a coverage subtree) would have it silently dropped, so such
  // draws must stay off this family.
  if (facts.fragmentCount != facts.colorFPCount) {
    return std::nullopt;
  }
  int xpType = facts.xpType();
  if (xpType < XP_NONE) {
    return std::nullopt;
  }
  // The two structurally-equivalent forms the old FP walk matched (a Compose wrapper and a
  // sequential pair) lower to this ONE graph — texture leaf plus exactly one operator — so the
  // dual-form match is gone with the IR.
  if (facts.ir == nullptr) {
    return std::nullopt;
  }
  AOTChainBuilder::AOTTextureTailShape tail = {};
  if (!AOTChainBuilder::CollectTextureTail(facts.ir->color, &tail) || tail.ops.size() != 1) {
    return std::nullopt;
  }
  auto* leaf = tail.leaf;
  if (leaf->samplingKind != AOTTextureSamplingKind::Plain || leaf->isYUV || leaf->isAlphaOnly ||
      leaf->hasRGBAAA) {
    return std::nullopt;
  }
  // The precompiled vertex template transforms the coordinate affinely; a projective source must
  // stay off this artifact (the plain templates transform coordinates affinely, with no w division).
  if (leaf->hasPerspective) {
    return std::nullopt;
  }
  // All four pointwise operators (ColorMatrix / Luma / AlphaThreshold / ColorSpaceXform) share the
  // same structural class (1 sampler, no CoverageFP, RGBA) and thus the same precompiled shader:
  // TexturedEffectShader. The operator is selected at draw time by the OpType runtime uniform (set
  // by the pointwise FP's onSetData), so it is not a compile-time dimension. The frag index only
  // encodes HAS_XP; subset clamping is a runtime uniform. The color-space one needs the fill
  // kernel's transfer-function check over the node's steps.
  auto* op = facts.ir->color.nodeAt(tail.ops[0]);
  bool supported = op->kind == AOTEffectKind::ColorMatrix || op->kind == AOTEffectKind::Luma ||
                   op->kind == AOTEffectKind::AlphaThreshold;
  if (!supported && op->kind == AOTEffectKind::ColorSpaceXform) {
    auto steps = std::get_if<AOTColorSpaceXformParameters>(&op->parameters);
    supported = steps != nullptr && SupportedFillKernelSteps(steps->steps.get());
  }
  if (!supported) {
    return std::nullopt;
  }
  return Inputs{xpType};
}

}  // namespace tgfx

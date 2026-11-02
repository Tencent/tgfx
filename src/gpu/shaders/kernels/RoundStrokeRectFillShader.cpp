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

#include "gpu/shaders/kernels/RoundStrokeRectFillShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/aot/AOTDrawFacts.h"
#include "gpu/processors/PorterDuffXferProcessor.h"

namespace tgfx {

std::optional<RoundStrokeRectFillShader::Inputs> RoundStrokeRectFillShader::Extract(
    const ProgramInfo* programInfo, PermutationMatchFailure* failure) {
  // The executor-table row and the walk share this one body.
  return ExtractFacts(ExtractAOTDrawFacts(programInfo), failure);
}

std::optional<RoundStrokeRectFillShader::Inputs> RoundStrokeRectFillShader::ExtractFacts(
    const AOTDrawFacts& facts, PermutationMatchFailure*) {
  if (!ServesAlphaOnly && facts.alphaOnlyTarget) {
    return std::nullopt;
  }
  // The geometry kind IS the contract: RectDrawOp's round line join.
  if (facts.geometry.kind != AOTGeometryKind::RoundStrokeRect) {
    return std::nullopt;
  }
  // The kernel reads the clip from the runtime clip contract uniforms, so a sole analytic clip is
  // admitted alongside the FP-free form.
  const bool directClip = facts.colorFromPaint && (facts.coverage == CoverageKind::AARect ||
                                                   facts.coverage == CoverageKind::RRect);
  if (facts.fragmentCount != 0 && !directClip) {
    return std::nullopt;
  }
  // Skip perspective UV transforms — not covered by precompiled variants.
  if (facts.geometry.hasUVPerspective) {
    return std::nullopt;
  }
  Inputs inputs;
  inputs.hasUVMatrix = facts.geometry.hasUVMatrix;
  inputs.xpType = facts.xpType();
  return inputs;
}

}  // namespace tgfx

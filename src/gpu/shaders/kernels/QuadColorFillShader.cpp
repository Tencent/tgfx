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

#include "gpu/shaders/kernels/QuadColorFillShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/aot/AOTDrawFacts.h"
#include "gpu/processors/RectEffect.h"

namespace tgfx {

std::optional<QuadColorFillShader::Inputs> QuadColorFillShader::Extract(
    const ProgramInfo* programInfo, PermutationMatchFailure* failure) {
  // The executor-table row and the walk share this one body.
  return ExtractFacts(ExtractAOTDrawFacts(programInfo), failure);
}

std::optional<QuadColorFillShader::Inputs> QuadColorFillShader::ExtractFacts(
    const AOTDrawFacts& facts, PermutationMatchFailure*) {
  if (facts.alphaOnlyTarget) {
    return std::nullopt;
  }
  if (facts.geometry.kind != AOTGeometryKind::QuadPerEdgeAA) {
    return std::nullopt;
  }
  // Accept either zero fragment processors, a device-space mask, or one direct analytic clip
  // coverage FP (AA rect or rrect). The clip is evaluated by the runtime clip contract uniforms.
  Inputs inputs;
  if (facts.fragmentCount != 0) {
    if (facts.colorFPCount != 0) {
      return std::nullopt;
    }
    if (facts.coverage == CoverageKind::AARect || facts.coverage == CoverageKind::RRect) {
      // No additional sampler or permutation dimension is needed.
    } else {
      if (facts.coverage != CoverageKind::DeviceMask) {
        return std::nullopt;
      }
      inputs.hasMaskTexture = true;
    }
  }
  inputs.xpType = facts.xpType();
  return inputs;
}

}  // namespace tgfx

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

#include "gpu/shaders/kernels/ComplexEllipseFillShader.h"
#include <optional>
#include "gpu/PermutationMatch.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/aot/AOTDrawFacts.h"
#include "gpu/processors/PorterDuffXferProcessor.h"

namespace tgfx {

std::optional<ComplexEllipseFillShader::Inputs> ComplexEllipseFillShader::Extract(
    const ProgramInfo* programInfo, PermutationMatchFailure* failure) {
  // The executor-table row and the walk share this one body.
  return ExtractFacts(ExtractAOTDrawFacts(programInfo), failure);
}

std::optional<ComplexEllipseFillShader::Inputs> ComplexEllipseFillShader::ExtractFacts(
    const AOTDrawFacts& facts, PermutationMatchFailure* failure) {
  if (!ServesAlphaOnly && facts.alphaOnlyTarget) {
    return std::nullopt;
  }
  // The geometry kind IS the contract: RRectDrawOp's complex AA form (per-corner radii).
  if (facts.geometry.kind != AOTGeometryKind::ComplexEllipse) {
    return std::nullopt;
  }
  if (facts.colorFPCount != 0) {
    return std::nullopt;
  }
  // A sole coverage FP is served through the shared coverage contract, with no dimension either
  // way: an AA rect/rrect clip rides the runtime clip uniforms (coverage_uniforms.inc /
  // clip_coverage.inc) and a device-space mask is the runtime HasDeviceMask uniform over the
  // always-declared MaskTextureSampler (padded with the dummy texture when absent). LocalMask needs
  // a per-vertex UV this geometry does not provide and stays rejected.
  if (!facts.coverage || *facts.coverage == CoverageKind::LocalMask) {
    return Fail(failure, PermutationMatchFailure::CoverageShape);
  }
  Inputs inputs;
  inputs.isStroke = facts.geometry.isStroke;
  inputs.xpType = facts.xpType();
  return inputs;
}

}  // namespace tgfx

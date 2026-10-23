/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//       https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/shaders/level1/QuadColorFillShader.h"
#include <optional>
#include <vector>
#include "gpu/PermutationMatcher.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "gpu/shaders/PermutationRules.h"

namespace tgfx {

std::optional<PermutationMatchResult> QuadColorFillShader::Match(const ProgramInfo* programInfo) {
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != "QuadPerEdgeAAGeometryProcessor") {
    return std::nullopt;
  }
  // Accept either zero fragment processors, a device-space mask, or one direct analytic clip
  // coverage FP (AA rect or rrect). The clip is evaluated by the runtime clip contract uniforms.
  QuadColorFillInputs inputs;
  if (programInfo->numFragmentProcessors() != 0) {
    if (programInfo->numColorFragmentProcessors() != 0) {
      return std::nullopt;
    }
    if (HasDirectAnalyticClipCoverage(programInfo)) {
      // No additional sampler or permutation dimension is needed.
    } else {
      auto coverage = ClassifyCoverageFP(programInfo);
      if (!coverage || *coverage != CoverageKind::DeviceMask) {
        return std::nullopt;
      }
      inputs.hasMaskTexture = true;
    }
  }
  inputs.xpType = GetXPType(programInfo);
  auto composed = ComposeQuadColorFill(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto fragIndex = QuadColorFillShader::FD::domain().encode(composed->fragValues);
  return PermutationMatchResult{"QuadColorFillShader", 0, fragIndex};
}

}  // namespace tgfx

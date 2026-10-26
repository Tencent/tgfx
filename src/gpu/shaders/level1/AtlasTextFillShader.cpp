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

#include "gpu/shaders/level1/AtlasTextFillShader.h"
#include <optional>
#include <vector>
#include "gpu/PermutationMatcher.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/shaders/PermutationRules.h"

namespace tgfx {

std::optional<PermutationVariant> AtlasTextFillShader::Match(const ProgramInfo* programInfo) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != GpClassName) {
    return std::nullopt;
  }
  if (programInfo->numColorFragmentProcessors() != 0 || programInfo->numFragmentProcessors() > 1) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() == 1) {
    // A single coverage FP multiplies the atlas glyph coverage: either an alpha-only device-space
    // mask, or a direct analytic clip (AA rect / rrect) via the runtime clip contract.
    if (!HasDirectAnalyticClipCoverage(programInfo)) {
      auto fp = programInfo->getFragmentProcessor(0);
      if (!IsProcessor<DeviceSpaceTextureEffect>(fp)) {
        return std::nullopt;
      }
      auto* mask = static_cast<const DeviceSpaceTextureEffect*>(fp);
      if (!mask->isAlphaOnly() || mask->hasPerspective()) {
        return std::nullopt;
      }
    }
  }
  auto* atgp = static_cast<const AtlasTextGeometryProcessor*>(gp);
  AtlasTextFillInputs inputs;
  inputs.hasCoverage = atgp->getAAType() == AAType::Coverage;
  inputs.xpType = xpType;
  auto composed = ComposeAtlasTextFill(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto vertIndex = AtlasTextFillShader::VD::domain().encode(composed->vertValues);
  auto fragIndex = AtlasTextFillShader::FD::domain().encode(composed->fragValues);
  return PermutationVariant{vertIndex, fragIndex};
}

}  // namespace tgfx

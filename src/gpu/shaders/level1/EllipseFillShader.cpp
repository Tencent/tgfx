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

#include "gpu/shaders/level1/EllipseFillShader.h"
#include <optional>
#include <vector>
#include "gpu/PermutationMatcher.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"

namespace tgfx {

std::optional<PermutationMatchResult> EllipseFillShader::Match(const ProgramInfo* programInfo) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != GpClassName) {
    return std::nullopt;
  }
  if (programInfo->numColorFragmentProcessors() != 0) {
    return std::nullopt;
  }
  // A clip/mask coverage FP is served through the shared HAS_COVERAGE dimension (device-space
  // sampling in coverage_output.inc). LocalMask needs per-vertex UV the ellipse vertex shader does
  // not provide, so SharedCoverageValue rejects it.
  auto coverageType = SharedDeviceMaskValue(programInfo);
  if (!coverageType) {
    return std::nullopt;
  }
  auto xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto fragIndex = FD::domain().encode(std::vector<int>{xpType, *coverageType});
  return PermutationMatchResult{"EllipseFillShader", 0, fragIndex};
}

}  // namespace tgfx

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

#include "gpu/shaders/level1/ComplexNonAARRectFillShader.h"
#include <optional>
#include <vector>
#include "gpu/PermutationMatcher.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/ComplexNonAARRectGeometryProcessor.h"
#include "gpu/shaders/PermutationRules.h"

namespace tgfx {

std::optional<PermutationMatchResult> ComplexNonAARRectFillShader::Match(
    const ProgramInfo* programInfo) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != GpClassName) {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  auto* cngp = static_cast<const ComplexNonAARRectGeometryProcessor*>(gp);
  ComplexNonAARRectFillInputs inputs;
  inputs.isStroke = cngp->isStroke();
  inputs.xpType = GetXPType(programInfo);
  auto composed = ComposeComplexNonAARRectFill(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto vertIndex = ComplexNonAARRectFillShader::Dims::domain().encode(composed->vertValues);
  auto fragIndex = ComplexNonAARRectFillShader::FD::domain().encode(composed->fragValues);
  return PermutationMatchResult{"ComplexNonAARRectFillShader", vertIndex, fragIndex};
}

}  // namespace tgfx

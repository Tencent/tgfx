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

#include "gpu/shaders/level1/RoundStrokeRectFillShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/RoundStrokeRectGeometryProcessor.h"

namespace tgfx {

std::optional<RoundStrokeRectFillShader::Inputs> RoundStrokeRectFillShader::Extract(
    const ProgramInfo* programInfo) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  auto gp = programInfo->getGeometryProcessor();
  if (!IsProcessor<RoundStrokeRectGeometryProcessor>(gp)) {
    return std::nullopt;
  }
  // The kernel reads the clip from the runtime clip contract uniforms, so a sole analytic clip is
  // admitted alongside the FP-free form.
  if (!HasNoProcessorsOrDirectClip(programInfo)) {
    return std::nullopt;
  }
  auto* rsgp = static_cast<const RoundStrokeRectGeometryProcessor*>(gp);
  // Skip perspective UV transforms — not covered by precompiled variants.
  if (rsgp->isUVPerspective()) {
    return std::nullopt;
  }
  Inputs inputs;
  inputs.hasUVMatrix = rsgp->hasUVMatrixTransform();
  inputs.xpType = GetXPType(programInfo);
  return inputs;
}

}  // namespace tgfx

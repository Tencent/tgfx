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

#include "gpu/shaders/level1/NonAARRectFillShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/NonAARRectGeometryProcessor.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {

std::optional<NonAARRectFillShader::Inputs> NonAARRectFillShader::Extract(
    const ProgramInfo* programInfo) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  auto gp = programInfo->getGeometryProcessor();
  if (!IsProcessor<NonAARRectGeometryProcessor>(gp)) {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() > 1) {
    return std::nullopt;
  }
  auto* ngp = static_cast<const NonAARRectGeometryProcessor*>(gp);
  Inputs inputs;
  inputs.isStroke = ngp->isStroke();
  if (programInfo->numFragmentProcessors() == 1) {
    // A sole analytic clip (AA rect or rounded rect) rides the runtime clip contract uniforms, so
    // it needs no dimension: the FP is there only to write them.
    const bool clipOnly = programInfo->numColorFragmentProcessors() == 0 &&
                          HasDirectAnalyticClipCoverage(programInfo);
    if (!clipOnly) {
      auto fp = programInfo->getFragmentProcessor(0);
      // The textured variant carries the single-tap tiled modes only: the mipmap-repeat 4-tap path
      // and perspective coordinates stay on the runtime route. The textured form never combines
      // with stroke; Compose applies that rejection through the shared predicate.
      if (!IsProcessor<TiledTextureEffect>(fp)) {
        return std::nullopt;
      }
      auto* tte = static_cast<const TiledTextureEffect*>(fp);
      if (tte->hasPerspective()) {
        return std::nullopt;
      }
      int modeX = 0;
      int modeY = 0;
      tte->getShaderModes(&modeX, &modeY);
      if (!TiledModeSupported(modeX) || !TiledModeSupported(modeY)) {
        return std::nullopt;
      }
      inputs.textured = true;
    }
  }
  inputs.xpType = GetXPType(programInfo);
  return inputs;
}

}  // namespace tgfx

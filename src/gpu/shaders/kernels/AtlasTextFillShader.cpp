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

#include "gpu/shaders/kernels/AtlasTextFillShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/shaders/modules/AnalyticClipModule.h"
#include "gpu/shaders/modules/DeviceMaskModule.h"
#include "gpu/shaders/modules/ParamWriter.h"
#include "gpu/shaders/modules/PorterDuffXPModule.h"

namespace tgfx {

std::optional<AtlasTextFillShader::Inputs> AtlasTextFillShader::Extract(
    const ProgramInfo* programInfo) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  auto gp = programInfo->getGeometryProcessor();
  if (!IsProcessor<AtlasTextGeometryProcessor>(gp)) {
    return std::nullopt;
  }
  if (programInfo->numColorFragmentProcessors() != 0 || programInfo->numFragmentProcessors() > 1) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < XP_NONE) {
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
  Inputs inputs;
  inputs.hasCoverage = atgp->getAAType() == AAType::Coverage;
  inputs.xpType = xpType;
  return inputs;
}

ModuleBindFn AtlasTextFillShader::BindingFor(const ProgramInfo* /*programInfo*/,
                                             const FragmentProcessor* fp) {
  if (IsProcessor<RectEffect>(fp) || IsProcessor<RRectEffect>(fp)) {
    return &AnalyticClipModule::Bind;
  }
  if (IsProcessor<DeviceSpaceTextureEffect>(fp)) {
    return &DeviceMaskModule::Bind;
  }
  return nullptr;
}

XPBindFn AtlasTextFillShader::XPBindingFor(const ProgramInfo* programInfo) {
  auto* xp = programInfo->getXferProcessor();
  if (!IsProcessor<PorterDuffXferProcessor>(xp)) {
    return nullptr;
  }
  return &PorterDuffXPModule::Bind;
}

}  // namespace tgfx

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

#include "gpu/shaders/kernels/EllipseFillShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/EllipseGeometryProcessor.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/shaders/modules/ParamWriter.h"
#include "gpu/shaders/modules/PorterDuffXPModule.h"

namespace tgfx {

std::optional<EllipseFillShader::Inputs> EllipseFillShader::Extract(
    const ProgramInfo* programInfo) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  auto gp = programInfo->getGeometryProcessor();
  if (!IsProcessor<EllipseGeometryProcessor>(gp)) {
    return std::nullopt;
  }
  if (programInfo->numColorFragmentProcessors() != 0) {
    return std::nullopt;
  }
  // A clip/mask coverage FP is served through the shared HAS_COVERAGE dimension (device-space
  // sampling in coverage_output.inc). LocalMask needs per-vertex UV the ellipse vertex shader does
  // not provide, so SharedDeviceMaskValue rejects it.
  auto coverageType = SharedDeviceMaskValue(programInfo);
  if (!coverageType) {
    return std::nullopt;
  }
  auto xpType = GetXPType(programInfo);
  if (xpType < XP_NONE) {
    return std::nullopt;
  }
  return Inputs{xpType, *coverageType != 0};
}

XPBindFn EllipseFillShader::XPBindingFor(const ProgramInfo* programInfo) {
  auto* xp = programInfo->getXferProcessor();
  if (!IsProcessor<PorterDuffXferProcessor>(xp)) {
    return nullptr;
  }
  return &PorterDuffXPModule::Bind;
}

}  // namespace tgfx

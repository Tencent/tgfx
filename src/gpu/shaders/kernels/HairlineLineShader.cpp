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

#include "gpu/shaders/kernels/HairlineLineShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/HairlineLineGeometryProcessor.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/shaders/modules/ParamWriter.h"
#include "gpu/shaders/modules/PorterDuffXPModule.h"

namespace tgfx {

std::optional<HairlineLineShader::Inputs> HairlineLineShader::Extract(
    const ProgramInfo* programInfo) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  auto gp = programInfo->getGeometryProcessor();
  if (!IsProcessor<HairlineLineGeometryProcessor>(gp)) {
    return std::nullopt;
  }
  if (!HasNoProcessorsOrDirectClip(programInfo)) {
    return std::nullopt;
  }
  auto xpType = GetXPType(programInfo);
  if (xpType < XP_NONE) {
    return std::nullopt;
  }
  return Inputs{xpType};
}

XPBindFn HairlineLineShader::XPBindingFor(const ProgramInfo* programInfo) {
  auto* xp = programInfo->getXferProcessor();
  if (!IsProcessor<PorterDuffXferProcessor>(xp)) {
    return nullptr;
  }
  return &PorterDuffXPModule::Bind;
}

}  // namespace tgfx

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

#include "gpu/shaders/level1/MeshFillShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/MeshGeometryProcessor.h"

namespace tgfx {

std::optional<MeshFillShader::Inputs> MeshFillShader::Extract(const ProgramInfo* programInfo) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  auto gp = programInfo->getGeometryProcessor();
  if (!IsProcessor<MeshGeometryProcessor>(gp)) {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 0) {
    return std::nullopt;
  }
  auto* mgp = static_cast<const MeshGeometryProcessor*>(gp);
  Inputs inputs;
  inputs.hasTexCoords = mgp->getHasTexCoords();
  inputs.xpType = GetXPType(programInfo);
  return inputs;
}

}  // namespace tgfx

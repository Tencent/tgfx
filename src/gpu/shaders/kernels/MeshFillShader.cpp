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

#include "gpu/shaders/kernels/MeshFillShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/aot/AOTDrawFacts.h"
#include "gpu/processors/PorterDuffXferProcessor.h"

namespace tgfx {

std::optional<MeshFillShader::Inputs> MeshFillShader::Extract(const ProgramInfo* programInfo,
                                                              PermutationMatchFailure* failure) {
  // The executor-table row and the walk share this one body.
  return ExtractFacts(ExtractAOTDrawFacts(programInfo), failure);
}

std::optional<MeshFillShader::Inputs> MeshFillShader::ExtractFacts(const AOTDrawFacts& facts,
                                                                   PermutationMatchFailure*) {
  if (!ServesAlphaOnly && facts.alphaOnlyTarget) {
    return std::nullopt;
  }
  // The geometry kind IS the contract: MeshDrawOp's processor, no processor-tree walk.
  if (facts.geometry.kind != AOTGeometryKind::Mesh) {
    return std::nullopt;
  }
  if (facts.fragmentCount != 0) {
    return std::nullopt;
  }
  Inputs inputs;
  inputs.hasTexCoords = facts.geometry.hasTexCoords;
  inputs.xpType = facts.xpType();
  return inputs;
}

}  // namespace tgfx

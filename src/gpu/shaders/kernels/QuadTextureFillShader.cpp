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

#include "gpu/shaders/kernels/QuadTextureFillShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/aot/AOTDrawFacts.h"
#include "gpu/aot/ExecutorRecord.h"

namespace tgfx {

std::optional<QuadTextureFillShader::Inputs> QuadTextureFillShader::Extract(
    const ProgramInfo* programInfo, PermutationMatchFailure* failure) {
  // The executor-table row and the walk share this one body.
  return ExtractFacts(ExtractAOTDrawFacts(programInfo), failure);
}


std::optional<QuadTextureFillShader::Inputs> QuadTextureFillShader::ExtractFacts(
    const AOTDrawFacts& facts, PermutationMatchFailure*) {
  // The acceptance is the family's executor record; the Compose below turns the record's axis
  // sources into this family's domain values. One record row replaces the previous 90-line
  // hand-written predicate (its dual-run against this record passed the whole suite unmodified).
  auto* record = FindExecutorRecord(Name);
  if (record == nullptr) {
    return std::nullopt;
  }
  auto matched = MatchExecutorRecord(*record, facts);
  if (!matched) {
    return std::nullopt;
  }
  Inputs inputs;
  inputs.hasUVCoord = matched->hasUVCoord;
  inputs.hasSubset = matched->hasSubset;
  inputs.hasLocalMask = matched->hasLocalMask;
  inputs.xpType = matched->xpType;
  return inputs;
}

}  // namespace tgfx

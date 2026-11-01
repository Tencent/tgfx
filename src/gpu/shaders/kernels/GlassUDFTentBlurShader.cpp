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

#include "gpu/shaders/kernels/GlassUDFTentBlurShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/aot/AOTDrawFacts.h"

namespace tgfx {

std::optional<GlassUDFTentBlurShader::Inputs> GlassUDFTentBlurShader::Extract(
    const ProgramInfo* programInfo, PermutationMatchFailure* failure) {
  // The executor-table row and the walk share this one body.
  return ExtractFacts(ExtractAOTDrawFacts(programInfo), failure);
}

std::optional<GlassUDFTentBlurShader::Inputs> GlassUDFTentBlurShader::ExtractFacts(
    const AOTDrawFacts& facts, PermutationMatchFailure*) {
  // The family-relative alpha-only gate (RejectsAlphaOnlyTarget's original semantics).
  if (!ServesAlphaOnly && facts.alphaOnlyTarget) {
    return std::nullopt;
  }
  if (facts.geometry.kind != AOTGeometryKind::Default &&
      facts.geometry.kind != AOTGeometryKind::QuadPerEdgeAA) {
    return std::nullopt;
  }
  if (facts.colorFPCount != 1) {
    return std::nullopt;
  }
  // The shared device-mask ABI gate (SharedDeviceMaskValue's semantics): an unclassifiable or
  // local-mask coverage stays on other routes.
  if (!facts.coverage || *facts.coverage == CoverageKind::LocalMask) {
    return std::nullopt;
  }
  int xpType = facts.xpType();
  if (xpType < XP_NONE) {
    return std::nullopt;
  }
  // The color expression reads off the carried IR's opaque-kernel node: the tent blur's barrier
  // over a single servable texture leaf (the horizontal pass reads a plain TextureEffect, the
  // vertical a TiledTextureEffect — the node's child leaf carries the shape).
  auto shape = ReadOpaqueKernelIRShape(facts.ir, /*kernelKind=*/2, /*requiresTextureChild=*/true);
  if (!shape) {
    return std::nullopt;
  }
  // The loop upper bound is the fixed constant 64; a longer max radius exceeds it and falls back.
  if (shape->kernel->maxRadius > 64) {
    return std::nullopt;
  }
  return Inputs{xpType};
}

}  // namespace tgfx

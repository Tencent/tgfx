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

#include "gpu/shaders/kernels/MaskFillShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/aot/AOTDrawFacts.h"
#include "gpu/processors/PorterDuffXferProcessor.h"

namespace tgfx {

std::optional<MaskFillShader::Inputs> MaskFillShader::Extract(const ProgramInfo* programInfo,
                                                              PermutationMatchFailure* failure) {
  // The executor-table row and the walk share this one body.
  return ExtractFacts(ExtractAOTDrawFacts(programInfo), failure);
}

std::optional<MaskFillShader::Inputs> MaskFillShader::ExtractFacts(const AOTDrawFacts& facts,
                                                                   PermutationMatchFailure*) {
  if (!ServesAlphaOnly && facts.alphaOnlyTarget) {
    return std::nullopt;
  }
  // The geometry kind IS the contract: ShapeDrawOp's DefaultGeometryProcessor.
  if (facts.geometry.kind != AOTGeometryKind::Default) {
    return std::nullopt;
  }
  // Solid color (from the DefaultGP Color uniform) masked by a single alpha-only coverage
  // texture (a rasterized shape mask from ShapeDrawOp). No color fragment processors.
  if (facts.colorFPCount != 0 || facts.fragmentCount != 1) {
    return std::nullopt;
  }
  // The rasterized shape's mask is a prepare-grown coverage: the staleness guard drops exactly
  // that IR, so no record-time term exists for it. The row reads the live mask port's facts
  // instead — the third channel (AOTDrawFacts::liveMask), the same escape hatch as the YUV
  // pending format. Only the simple mask form is supported: a plain alpha-only texture with no
  // subset, no packed alpha, no YUV. Anything else falls back to ProgramBuilder.
  if (!facts.liveMask.present || !facts.liveMask.hasSampler || !facts.liveMask.alphaOnly ||
      facts.liveMask.hasSubset || facts.liveMask.isYUV || facts.liveMask.hasRGBAAA) {
    return std::nullopt;
  }
  // The mask coverage feeds the XferProcessor's coverage lerp, so Porter-Duff blends (DST_TEX and
  // framebuffer-fetch) are supported via the HAS_XP fragment dimension. Only a genuinely
  // unsupported transfer processor (xpType < 0) falls back to ProgramBuilder.
  const int xpType = facts.xpType();
  if (xpType < XP_NONE) {
    return std::nullopt;
  }
  return Inputs{xpType};
}

}  // namespace tgfx

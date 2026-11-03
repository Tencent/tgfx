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
#include "gpu/aot/DrawShape.h"
#include "gpu/processors/PorterDuffXferProcessor.h"

namespace tgfx {

std::optional<MaskFillShader::Inputs> MaskFillShader::ExtractFacts(const DrawShape& facts,
                                                                   PermutationMatchFailure*) {
  // The geometry kind IS the contract: ShapeDrawOp's DefaultGeometryProcessor.
  if (facts.geometry.kind != GeometryKind::Default) {
    return std::nullopt;
  }
  // Solid color (from the DefaultGP Color uniform) masked by a single alpha-only coverage
  // texture (a rasterized shape mask from ShapeDrawOp). No color fragment processors.
  if (facts.colorFPCount != 0 || facts.fragmentCount != 1) {
    return std::nullopt;
  }
  // The rasterized shape's mask is a prepare-grown coverage: the lowering appends it to the IR's
  // terms at prepare, so the draw carries exactly one term and it is the mask's texture leaf.
  // Only the simple mask form is supported: a plain alpha-only texture with no subset, no packed
  // alpha, no YUV. Anything else falls back to ProgramBuilder.
  const auto* mask = facts.ir != nullptr && facts.ir->coverage.size() == 1
                         ? BareTextureCoverageLeaf(facts.ir->coverage[0])
                         : nullptr;
  if (mask == nullptr || mask->samplingKind != AOTTextureSamplingKind::Plain ||
      !mask->isAlphaOnly || mask->subset.has_value() || mask->isYUV || mask->hasRGBAAA ||
      mask->textureProxy == nullptr || mask->textureProxy->getTextureView() == nullptr) {
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

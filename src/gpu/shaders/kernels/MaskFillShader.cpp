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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/shaders/kernels/MaskFillShader.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/shaders/contract/DrawFacts.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"

namespace tgfx {

bool MaskFillShader::Accept(const DrawShape& shape) {
  // The geometry kind IS the contract: ShapeDrawOp's DefaultGeometryProcessor.
  if (shape.geometry.kind != GeometryKind::Default) {
    return false;
  }
  // Solid color (from the DefaultGP Color uniform) masked by a single alpha-only coverage
  // texture (a rasterized shape mask from ShapeDrawOp). No color fragment processors.
  if (shape.colorFPCount != 0 || shape.fragmentCount != 1) {
    return false;
  }
  // The rasterized shape's mask is a prepare-grown coverage: the lowering appends it to the IR's
  // terms at prepare, so the draw carries exactly one term and it is the mask's texture leaf.
  // Only the simple mask form is supported: a plain alpha-only texture with no subset, no packed
  // alpha, no YUV. Anything else is not served by this kernel.
  const auto* mask = shape.ir != nullptr && shape.ir->coverage.size() == 1
                         ? BareTextureCoverageLeaf(shape.ir->coverage[0])
                         : nullptr;
  if (mask == nullptr || mask->samplingKind != AOTTextureSamplingKind::Plain ||
      !mask->isAlphaOnly || mask->subset.has_value() || mask->isYUV || mask->hasRGBAAA ||
      mask->textureProxy == nullptr || mask->textureProxy->getTextureView() == nullptr) {
    return false;
  }
  return shape.xpType() >= XP_NONE;
}

Variants MaskFillShader::Select(const DrawShape& shape) {
  Variants variants;
  variants.frag(XP_DEFINE_NAME, shape.xpType());
  return variants;
}

}  // namespace tgfx

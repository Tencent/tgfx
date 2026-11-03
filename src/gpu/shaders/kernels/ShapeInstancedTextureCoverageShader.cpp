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

#include "gpu/shaders/kernels/ShapeInstancedTextureCoverageShader.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/select/DrawShape.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"

namespace tgfx {

namespace {

bool IsAnalyticClipKind(const std::optional<Coverage>& kind) {
  return kind && (*kind == Coverage::AARect || *kind == Coverage::RRect);
}

// The gradient form's color graph: the geometry color lowered as the input, then the single
// GradientSource over it.
const AOTGradientParameters* SoleGradientLeaf(const AOTEffectGraph& graph) {
  if (graph.nodeCount() != 2 || !graph.root().isValid() || graph.root().index() != 1) {
    return nullptr;
  }
  auto* node = graph.nodeAt(graph.root());
  if (node == nullptr || node->kind != AOTEffectKind::GradientSource) {
    return nullptr;
  }
  return std::get_if<AOTGradientParameters>(&node->parameters);
}

}  // namespace

bool ShapeInstancedTextureCoverageShader::Accept(const DrawShape& shape) {
  // The geometry kind IS the contract: ShapeInstancedDrawOp's processor, in its non-AA form —
  // this kernel carries no AA coverage attribute.
  if (shape.geometry.kind != GeometryKind::ShapeInstanced || shape.geometry.hasCoverage) {
    return false;
  }
  if (shape.xpType() != XP_NONE) {
    return false;
  }
  // Two color forms: the geometry color alone, or a single-interval clamped gradient over it.
  // The gradient node in the carried IR is the contract: a single-interval colorizer over an
  // affine layout (the lowering refuses every other form).
  if (shape.colorFPCount == 1) {
    const auto* params = shape.ir != nullptr ? SoleGradientLeaf(shape.ir->color) : nullptr;
    if (params == nullptr || params->colorizerKind != 0 || params->hasPerspective) {
      return false;
    }
  } else if (shape.colorFPCount != 0) {
    return false;
  }
  // One coverage texture, optionally accompanied by an analytic clip. The clip rides the runtime
  // clip contract uniforms (no permutation change), so both forms select the same variant.
  if (shape.fragmentCount == shape.colorFPCount + 1) {
    // The sole term is the texture leaf — record-time, or the rasterized shape's prepare-grown
    // mask the lowering appended at prepare.
    const auto* texture = shape.ir != nullptr && shape.ir->coverage.size() == 1
                              ? BareTextureCoverageLeaf(shape.ir->coverage[0])
                              : nullptr;
    if (texture == nullptr || texture->samplingKind != AOTTextureSamplingKind::Plain ||
        texture->isYUV || texture->hasRGBAAA || texture->textureProxy == nullptr ||
        texture->textureProxy->getTextureView() == nullptr) {
      return false;
    }
    return true;
  }
  if (shape.fragmentCount != shape.colorFPCount + 2) {
    return false;
  }
  // The texture-plus-clip form keeps the term walk (the processor-tree walk could not tell the
  // two FPs apart either — the terms are what named the texture): the terms follow the coverage
  // FPs' order, so the non-analytic term's position names the texture.
  if (shape.ir == nullptr) {
    return false;
  }
  const auto& coverage = shape.ir->coverage;
  std::optional<Coverage> kinds[2] = {};
  size_t termCount = 0;
  for (const auto& term : coverage) {
    if (termCount >= 2) {
      break;
    }
    kinds[termCount++] = ClassifyCoverageTerm(term);
  }
  if (termCount != 2) {
    // Two FPs lowered to something other than two single terms (a composed clip among them):
    // not a shape this kernel evaluates.
    return false;
  }
  const AOTTextureParameters* textureLeaf = nullptr;
  if (IsAnalyticClipKind(kinds[0]) && !IsAnalyticClipKind(kinds[1])) {
    textureLeaf = BareTextureCoverageLeaf(coverage[1]);
  } else if (IsAnalyticClipKind(kinds[1]) && !IsAnalyticClipKind(kinds[0])) {
    textureLeaf = BareTextureCoverageLeaf(coverage[0]);
  } else {
    return false;
  }
  return textureLeaf != nullptr && textureLeaf->samplingKind == AOTTextureSamplingKind::Plain &&
         !textureLeaf->isYUV && !textureLeaf->hasRGBAAA && textureLeaf->textureProxy != nullptr;
}

Variants ShapeInstancedTextureCoverageShader::Select(const DrawShape& shape) {
  // The gradient presence mirrors the acceptance: one color FP is the sole gradient leaf.
  int gradient = shape.colorFPCount == 1 ? 1 : 0;
  Variants variants;
  variants.vert("GRADIENT", gradient).frag("GRADIENT", gradient);
  return variants;
}

}  // namespace tgfx

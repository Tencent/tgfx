/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//       https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/shaders/level1/PointwiseChainShader.h"
#include <optional>
#include <vector>
#include "gpu/PermutationMatcher.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/AOTPointwiseChainProcessor.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "gpu/processors/ComplexEllipseGeometryProcessor.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/EllipseGeometryProcessor.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/HairlineLineGeometryProcessor.h"
#include "gpu/processors/MeshGeometryProcessor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/shaders/PermutationRules.h"

namespace tgfx {

std::optional<PermutationMatchResult> PointwiseChainShader::Match(const ProgramInfo* programInfo) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  auto gp = programInfo->getGeometryProcessor();
  // GP profiles: 0 Default, 1 Quad, 2 Mesh (whose vertex buffers follow the
  // chain's attribute order), 3 the sampler-free AtlasText twin, 4 the ellipse layout, 5 the
  // hairline-line layout, 6 / 7 the complex rounded-rect fill / stroke layouts.
  int gpKind = -1;
  if (IsProcessor<DefaultGeometryProcessor>(gp)) {
    gpKind = 0;
  } else if (IsProcessor<QuadPerEdgeAAGeometryProcessor>(gp)) {
    gpKind = 1;
  } else if (IsProcessor<MeshGeometryProcessor>(gp)) {
    gpKind = 2;
  } else if (IsProcessor<AtlasTextGeometryProcessor>(gp)) {
    gpKind = 3;
  } else if (IsProcessor<EllipseGeometryProcessor>(gp)) {
    gpKind = 4;
  } else if (IsProcessor<HairlineLineGeometryProcessor>(gp)) {
    gpKind = 5;
  } else if (IsProcessor<ComplexEllipseGeometryProcessor>(gp)) {
    gpKind = static_cast<const ComplexEllipseGeometryProcessor*>(gp)->isStroke() ? 7 : 6;
  }
  if (gpKind < 0 || programInfo->numColorFragmentProcessors() != 1 ||
      programInfo->numFragmentProcessors() != 1) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (!IsProcessor<AOTPointwiseChainProcessor>(fp)) {
    return std::nullopt;
  }
  auto* chain = static_cast<const AOTPointwiseChainProcessor*>(fp);
  auto leafCount = chain->leafCount();
  // Sampler-only padding makes every non-empty chain present exactly four sampler children.
  if (leafCount != 0 && leafCount != 4) {
    return std::nullopt;
  }
  // Color-space transforms and gradients each share one chain-wide parameter block.
  int colorSpaceXformCount = 0;
  int gradientCount = 0;
  for (size_t index = 0; index < chain->slotCount(); ++index) {
    if (chain->slot(index).op == AOTChainOp::ColorSpaceXform) {
      ++colorSpaceXformCount;
    } else if (chain->slot(index).op == AOTChainOp::Gradient) {
      ++gradientCount;
    }
  }
  if (colorSpaceXformCount > 1 || gradientCount > 1) {
    return Fail(PermutationMatchFailure::GradientOrNoiseBudget);
  }
  for (size_t index = 0; index < leafCount; ++index) {
    auto leaf = chain->childProcessor(index);
    // A deferred (task-local) proxy reports zero samplers until it is instantiated; it always
    // binds exactly one when it is (a lazy proxy is internally rendered, hence never multi-plane
    // YUV), so only a multi-sampler leaf is rejected here.
    if (!IsProcessor<TextureEffect>(leaf) || leaf->numTextureSamplers() > 1) {
      return std::nullopt;
    }
    auto* texture = static_cast<const TextureEffect*>(leaf);
    // A leaf subset is a runtime uniform (the kernel always clamps, full bounds when absent), an
    // alpha-only leaf splats .r through its slot selector bit, and an RGBAAA leaf reads its alpha
    // area through the per-leaf AlphaStart / HasRgbaaa uniforms, so none needs a variant.
    // Perspective transforms are served: leaf coordinates carry w and divide per-pixel.
    if (texture->isYUV()) {
      return std::nullopt;
    }
  }
  PointwiseChainInputs inputs;
  inputs.gpKind = gpKind;
  inputs.textureCount = leafCount == 0 ? 0 : 1;
  inputs.hasMask = chain->hasMask();
  inputs.hasCoverageSubtree = chain->coverageRoot() >= 0;
  inputs.hasNoise = chain->hasNoise();
  inputs.hasGradient = gradientCount != 0;
  inputs.resourceProfile = chain->resourceProfile();
  inputs.xpType = GetXPType(programInfo);
  if (gpKind == 1) {
    auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
    if (quadGP->getHasSubset()) {
      if (!IsChainQuadSubsetServable(quadGP, chain)) {
        return Fail(PermutationMatchFailure::QuadSubset);
      }
      inputs.hasSubset = true;
    }
    inputs.hasUVCoord = !quadGP->hasUVMatrix();
    inputs.hasCoverage = true;
  } else if (gpKind == 2) {
    auto* meshGP = static_cast<const MeshGeometryProcessor*>(gp);
    // Mesh vertex buffers follow the chain's attribute order (position, coverage, texCoord,
    // color): texCoords ride the uvCoord slot and the color slot is unconditional (white for
    // colorless meshes). Mesh buffers always carry the coverage slot (1.0 for vertex meshes).
    inputs.hasUVCoord = meshGP->getHasTexCoords();
    inputs.hasCoverage = true;
  } else if (gpKind == 3) {
    // Only the sampler-free chain-route twin is servable: a sampler-owning atlas GP would bind
    // its atlas ahead of the chain's leaf samplers.
    if (gp->numTextureSamplers() != 0) {
      return std::nullopt;
    }
    // Atlas text buffers are (position, maskCoord, color) with no coverage slot; maskCoord
    // rides the uvCoord slot and the color slot is unconditional.
    inputs.hasUVCoord = true;
    inputs.hasCoverage = false;
  } else if (gpKind == 4) {
    inputs.hasCoverage = false;
  } else if (gpKind == 5) {
    // The hairline edge AA is per-pixel (edge-distance varying); the color is a uniform.
    inputs.hasCoverage = false;
  } else if (gpKind == 6 || gpKind == 7) {
    // Per-pixel edge AA like the ellipse; the color attribute is unconditional.
    inputs.hasCoverage = false;
  } else {
    // The DefaultGP's coverage attribute is unconditional (the non-AA path triangles carry a
    // constant 1.0 slot), so the DefaultGP always reports coverage.
    inputs.hasCoverage = true;
  }
  auto composed = ComposePointwiseChain(inputs);
  if (!composed) {
    if (IsAOTGraphProfile(inputs.resourceProfile)) {
      // A draw the extended resource kinds cannot place is a coverage gap of the merged family,
      // reported as it was on the pre-merge Graph family.
      return Fail(PermutationMatchFailure::PermutationNotEnumerated);
    }
    return std::nullopt;
  }
  auto vertIndex = PointwiseChainShader::VD::domain().encode(composed->vertValues);
  auto fragIndex = PointwiseChainShader::FD::domain().encode(composed->fragValues);
  return PermutationMatchResult{"PointwiseChainShader", vertIndex, fragIndex};
}

}  // namespace tgfx

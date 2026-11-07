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

#include "gpu/shaders/kernels/PointwiseChainShader.h"
#include <optional>
#include <vector>
#include "gpu/DrawingManager.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/select/DrawShape.h"
#include "gpu/aot/select/PermutationMatch.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "gpu/processors/ComplexEllipseGeometryProcessor.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/EllipseGeometryProcessor.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/HairlineLineGeometryProcessor.h"
#include "gpu/processors/MeshGeometryProcessor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"

namespace tgfx {

std::optional<PointwiseChainShader::Inputs> PointwiseChainShader::Extract(
    const ProgramInfo* programInfo, PermutationMatchFailure* failure) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  if (programInfo->getGeometryProcessor() == nullptr) {
    return std::nullopt;
  }
  // The interpreter's own subject: the whole effect expression resolves through the one compile
  // entry — a plan pass arrives with its program, a plain draw compiles it on this first match,
  // and every later prepare of the same op reads it back.
  const auto* program = AOTChainBuilder::ResolveChainProgram(programInfo);
  if (program == nullptr) {
    return std::nullopt;
  }
  return DeriveInputs(*program, programInfo, failure);
}

std::optional<PointwiseChainShader::Inputs> PointwiseChainShader::DeriveInputs(
    const AOTProgram& program, const ProgramInfo* programInfo, PermutationMatchFailure* failure) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  auto gp = programInfo->getGeometryProcessor();
  // The gpKind axis comes from the one geometry classification (DescribeGeometryProcessor), so the
  // family's vertex-layout choice and the matcher's facts cannot disagree.
  auto gpKindOptional = PointwiseGpKind(DescribeGeometryProcessor(gp));
  if (!gpKindOptional.has_value()) {
    return std::nullopt;
  }
  const int gpKind = *gpKindOptional;
  // Sampler-only padding makes every non-empty chain present exactly four sampler children.
  if (program.samplerChildCount != 0 && program.samplerChildCount != 4) {
    return std::nullopt;
  }
  // Color-space transforms and gradients each share one chain-wide parameter block.
  int colorSpaceXformCount = 0;
  int gradientCount = 0;
  for (const auto& slot : program.slots) {
    if (slot.op == AOTChainOp::ColorSpaceXform) {
      ++colorSpaceXformCount;
    } else if (slot.op == AOTChainOp::Gradient) {
      ++gradientCount;
    }
  }
  if (colorSpaceXformCount > 1 || gradientCount > 1) {
    return Fail(failure, PermutationMatchFailure::GradientOrNoiseBudget);
  }
  for (const auto& binding : program.bindings) {
    // Every leaf binding is a plain sampled texture: the planner refuses multi-plane, swizzled and
    // non-TileMode sources before a leaf reaches the program. A deferred (task-local) proxy has no
    // view yet and always binds one sampler once it is instantiated (a lazy proxy is internally
    // rendered, hence never multi-plane YUV), so only a view that resolves multi-plane is refused.
    // A leaf subset is a runtime uniform (the kernel always clamps, full bounds when absent), an
    // alpha-only leaf splats .r through its slot selector bit, and an RGBAAA leaf reads its alpha
    // area through the per-leaf AlphaStart / HasRgbaaa uniforms, so none needs a variant.
    // Perspective transforms are served: leaf coordinates carry w and divide per-pixel.
    if (binding.kind != AOTBinding::Kind::Leaf || binding.textureProxy == nullptr) {
      continue;
    }
    auto textureView = binding.textureProxy->getTextureView();
    if (textureView != nullptr && textureView->isYUV()) {
      return std::nullopt;
    }
  }
  Inputs inputs;
  inputs.gpKind = gpKind;
  inputs.textureCount = program.samplerChildCount == 0 ? 0 : 1;
  inputs.hasMask = program.hasMaskTexture;
  inputs.hasNoise = program.noiseSamplerBase >= 0;
  inputs.hasGradient = gradientCount != 0;
  inputs.resourceProfile = program.profile;
  inputs.xpType = GetXPType(programInfo);
  if (gpKind == GpKind::QuadPerEdgeAA) {
    auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
    if (quadGP->getHasSubset()) {
      if (!IsChainQuadSubsetServable(quadGP, program)) {
        return Fail(failure, PermutationMatchFailure::QuadSubset);
      }
      inputs.hasSubset = true;
    }
    inputs.hasUVCoord = !quadGP->hasUVMatrix();
    inputs.hasCoverage = true;
  } else if (gpKind == GpKind::Mesh) {
    auto* meshGP = static_cast<const MeshGeometryProcessor*>(gp);
    // Mesh vertex buffers follow the chain's attribute order (position, coverage, texCoord,
    // color): texCoords ride the uvCoord slot and the color slot is unconditional (white for
    // colorless meshes). Mesh buffers always carry the coverage slot (1.0 for vertex meshes).
    inputs.hasUVCoord = meshGP->getHasTexCoords();
    inputs.hasCoverage = true;
  } else if (gpKind == GpKind::AtlasText) {
    // The atlas GP may own its atlas sampler (an unmodified draw) or not (the chain-route twin a
    // plan task built): the glyph leaf binds through the chain expression either way, so at most
    // one sampler (the atlas itself) is acceptable.
    if (gp->numTextureSamplers() > 1) {
      return std::nullopt;
    }
    // Atlas text buffers are (position, maskCoord, color) with no coverage slot; maskCoord
    // rides the uvCoord slot and the color slot is unconditional.
    inputs.hasUVCoord = true;
    inputs.hasCoverage = false;
  } else if (gpKind == GpKind::Ellipse) {
    inputs.hasCoverage = false;
  } else if (gpKind == GpKind::HairlineLine) {
    // The hairline edge AA is per-pixel (edge-distance varying); the color is a uniform.
    inputs.hasCoverage = false;
  } else if (gpKind == GpKind::ComplexEllipseFill || gpKind == GpKind::ComplexEllipseStroke) {
    // Per-pixel edge AA like the ellipse; the color attribute is unconditional.
    inputs.hasCoverage = false;
  } else {
    // The DefaultGP's coverage attribute is unconditional (the non-AA path triangles carry a
    // constant 1.0 slot), so the DefaultGP always reports coverage.
    inputs.hasCoverage = true;
  }
  // Compose is re-run here only for the miss-reason accounting: an extended-profile draw the rule
  // cannot place is a coverage gap of the merged family and must be reported as such.
  if (!Compose(inputs)) {
    if (IsAOTGraphProfile(inputs.resourceProfile)) {
      // A draw the extended resource kinds cannot place is a coverage gap of the merged family,
      // reported as it was on the pre-merge Graph family.
      return Fail(failure, PermutationMatchFailure::PermutationNotEnumerated);
    }
    return std::nullopt;
  }
  return inputs;
}

std::optional<PermutationVariant> PointwiseChainShader::MatchProgram(
    const AOTProgram& program, const ProgramInfo* programInfo) {
  auto inputs = DeriveInputs(program, programInfo, nullptr);
  if (!inputs) {
    return std::nullopt;
  }
  auto composed = Compose(*inputs);
  if (!composed) {
    return std::nullopt;
  }
  return PermutationVariant{VertDims::domain().encode(composed->vertValues),
                            FragDims::domain().encode(composed->fragValues)};
}

}  // namespace tgfx

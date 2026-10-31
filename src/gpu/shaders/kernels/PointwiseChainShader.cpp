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
#include "gpu/AOTChainBuilder.h"
#include "gpu/DrawingManager.h"
#include "gpu/PermutationMatch.h"
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

namespace tgfx {

std::optional<PointwiseChainShader::Inputs> PointwiseChainShader::Extract(
    const ProgramInfo* programInfo, PermutationMatchFailure* failure) {
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
  if (gpKind < 0) {
    return std::nullopt;
  }
  // Dual acceptance: a draw still carrying the chain processor (a multi-pass plan task's pass,
  // until the plan route retires) reads its shape directly; any other draw is the interpreter's
  // own subject — the whole effect expression is compiled here.
  const AOTProgram* compiled = nullptr;
  if (programInfo->numColorFragmentProcessors() == 1 && programInfo->numFragmentProcessors() == 1) {
    auto fp = programInfo->getFragmentProcessor(0);
    if (IsProcessor<AOTPointwiseChainProcessor>(fp)) {
      compiled = &static_cast<const AOTPointwiseChainProcessor*>(fp)->program();
    }
  }
  if (compiled == nullptr) {
    auto context = programInfo->getContext();
    if (context == nullptr || context->drawingManager() == nullptr) {
      return std::nullopt;
    }
    AOTProgram program = {};
    if (!AOTChainBuilder::CompileChainProgramForInfo(context->drawingManager()->drawingAllocator(),
                                                     programInfo, &program)) {
      return std::nullopt;
    }
    // Keep the compiled program on the info: the walk stops here when the acceptance checks below
    // pass, and the winner's writer uploads from it instead of rebuilding the expression per draw.
    programInfo->setCachedChainProgram(std::move(program));
    compiled = programInfo->getCachedChainProgram();
  }
  // Everything below reads the compiled program, never a processor shell: the program is the
  // single description of what the draw's chain variant needs.
  const auto& program = *compiled;
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
  if (gpKind == 1) {
    auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
    if (quadGP->getHasSubset()) {
      if (!IsChainQuadSubsetServable(quadGP, program)) {
        return Fail(failure, PermutationMatchFailure::QuadSubset);
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

}  // namespace tgfx

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

#include "gpu/shaders/ChainInputs.h"
#include <optional>
#include <vector>
#include "gpu/DrawingManager.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/AOTResourceProfile.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/FamilyDomains.h"
#include "gpu/shaders/contract/ChainISA.h"
#include "gpu/shaders/contract/DrawFacts.h"
#include "gpu/shaders/modules/YUVChainTail.h"

namespace tgfx {

// The contract-side budgets and the declaration-side array lengths are one contract. The contract
// header cannot include the declaration layer (LayeringTest), so the agreement is pinned here, in
// a translation unit that sees both: a drift fails the build instead of shipping a kernel whose
// injected capacities disagree with the arrays it declares.
static_assert(static_cast<size_t>(ChainSlotPackedCounts[0]) == ChainResourceCapacity.instructions,
              "the plain chain's declared slot count drifted from the planner's budget");
static_assert(static_cast<size_t>(ChainSlotPackedCounts[6]) ==
                  NoiseGraphResourceCapacity.instructions,
              "the noise chain's declared slot count drifted from the planner's budget");
static_assert(static_cast<size_t>(ChainSlotPackedCounts[0]) ==
                  TiledGraphResourceCapacity.instructions,
              "the gradient shape shares the plain chain's slot storage");
static_assert(static_cast<size_t>(ChainDataCounts[0]) ==
                  static_cast<size_t>(ChainSlotPackedCounts[0]) * ChainISA::PoolSlotWords +
                      ChainISA::BlockWords,
              "the declared pool length is not slots * stride + block words");
static_assert(static_cast<size_t>(ChainDataCounts[6]) ==
                  static_cast<size_t>(ChainSlotPackedCounts[6]) * ChainISA::PoolSlotWords +
                      ChainISA::BlockWords,
              "the declared pool length is not slots * stride + block words");
static_assert(YUVChainTailSlotWords == ChainISA::PoolSlotWords &&
                  YUVChainTailBlockWords == ChainISA::BlockWords,
              "the YUV tail's pool layout differs from the chain's");
static_assert(static_cast<size_t>(YUVChainTailDataCount) ==
                  static_cast<size_t>(YUVChainTailSlotCapacity) * ChainISA::PoolSlotWords +
                      ChainISA::BlockWords,
              "the YUV tail's declared pool length is not slots * stride + block words");

std::optional<ChainInputs::Inputs> ChainInputs::DeriveInputs(const AOTProgram& program,
                                                             const ProgramInfo* programInfo,
                                                             PermutationMatchFailure* failure) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  // The gpKind axis comes from the one geometry classification (DescribeGeometryProcessor), so the
  // family's vertex-layout choice and the matcher's facts cannot disagree.
  auto gpKindOptional =
      PointwiseGpKind(DescribeGeometryProcessor(programInfo->getGeometryLayout()));
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
    const auto& quad = programInfo->getGeometryLayout().parameters;
    if (quad.hasSubset) {
      if (!IsChainQuadSubsetServable(quad, program)) {
        return Fail(failure, PermutationMatchFailure::QuadSubset);
      }
      inputs.hasSubset = true;
    }
    inputs.hasUVCoord = !quad.hasUVMatrix;
    inputs.hasCoverage = true;
  } else if (gpKind == GpKind::Mesh) {
    // Mesh vertex buffers follow the chain's attribute order (position, coverage, texCoord,
    // color): texCoords ride the uvCoord slot and the color slot is unconditional (white for
    // colorless meshes). Mesh buffers always carry the coverage slot (1.0 for vertex meshes).
    inputs.hasUVCoord = programInfo->getGeometryLayout().parameters.hasTexCoords;
    inputs.hasCoverage = true;
  } else if (gpKind == GpKind::AtlasText) {
    // The atlas geometry may own its atlas sampler (an unmodified draw) or not (the chain-route
    // twin a plan task built): the glyph leaf binds through the chain expression either way, so at
    // most one sampler (the atlas itself) is acceptable.
    if (programInfo->getGeometryLayout().parameters.atlasProxy != nullptr) {
      // one atlas sampler at most by construction
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
  } else if (gpKind >= GpKind::ComplexEllipseFill) {
    // Per-pixel edge coverage like the ellipse (the complex ellipse, the two non-AA rounded-rect
    // SDFs and the hairline quad); the color attribute is unconditional where the buffer has one.
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

std::optional<Variant> ChainInputs::MatchVariant(const AOTProgram& program,
                                                 const ProgramInfo* programInfo) {
  auto inputs = DeriveInputs(program, programInfo, nullptr);
  if (!inputs) {
    return std::nullopt;
  }
  auto composed = Compose(*inputs);
  if (!composed) {
    return std::nullopt;
  }
  const auto vertDomain = VertDims::domain();
  const auto fragDomain = FragDims::domain();
  const auto vert = [&](const char* name) {
    return composed->vertValues[vertDomain.indexOf(name)];
  };
  const auto frag = [&](const char* name) {
    return composed->fragValues[fragDomain.indexOf(name)];
  };
  ChainLegacyCell cell = {};
  cell.gpLayout = vert("GP_LAYOUT");
  cell.coverage = vert("HAS_COVERAGE");
  cell.uvCoord = vert("HAS_UV_COORD");
  cell.subset = vert("HAS_SUBSET");
  cell.xp = frag("HAS_XP");
  cell.textureCount = frag("TEXTURE_COUNT");
  cell.mask = frag("HAS_MASK_TEXTURE");
  cell.resource = frag("RESOURCE_KIND");
  const uint8_t stroke = cell.gpLayout == 4 ? 1 : 0;
  return EncodeVariant(FamilyIndexOf("Chain"), ChainGeometryOfCell(cell),
                       {static_cast<uint8_t>(cell.subset), stroke, ChainProfileOfCell(cell),
                        static_cast<uint8_t>(cell.xp)});
}

std::optional<std::pair<uint32_t, uint32_t>> ChainInputs::MatchProgram(
    const AOTProgram& program, const ProgramInfo* programInfo) {
  auto inputs = DeriveInputs(program, programInfo, nullptr);
  if (!inputs) {
    return std::nullopt;
  }
  auto composed = Compose(*inputs);
  if (!composed) {
    return std::nullopt;
  }
  return std::make_pair(VertDims::domain().encode(composed->vertValues),
                        FragDims::domain().encode(composed->fragValues));
}

std::set<std::pair<uint32_t, uint32_t>> ChainInputs::Reachable() {
  std::set<std::pair<uint32_t, uint32_t>> result;
  const auto vertDomain = VertDims::domain();
  const auto fragDomain = FragDims::domain();
  for (const auto& inputs : AllInputs()) {
    auto composed = Compose(inputs);
    if (!composed) {
      continue;
    }
    result.emplace(vertDomain.encode(composed->vertValues),
                   fragDomain.encode(composed->fragValues));
  }
  return result;
}

}  // namespace tgfx

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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/ProgramInfo.h"
#include "gpu/Swizzle.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"
#include "gpu/processors/ClampedGradientEffect.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/contract/DrawFacts.h"

namespace tgfx {

namespace {

void AddReadSwizzle(const std::shared_ptr<Texture>& texture, uint32_t* required) {
  if (texture != nullptr && AOTReadSwizzleMode(texture->format()) != 0) {
    *required |= ShaderTextureReadSwizzle;
  }
}

// The ShaderTextureRead semantics the draw's sampled textures need. YUV planes are excluded:
// the YUV kernels read the plane channels they need (.r / .rg), which the read swizzle preserves.
uint32_t RequiredTextureReads(const ProgramInfo* programInfo) {
  uint32_t required = 0;
  auto gp = programInfo->getGeometryProcessor();
  for (size_t i = 0; gp != nullptr && i < gp->numTextureSamplers(); ++i) {
    AddReadSwizzle(gp->textureAt(i), &required);
  }
  FragmentProcessor::Iter iter(programInfo);
  for (auto fp = iter.next(); fp != nullptr; fp = iter.next()) {
    if (IsProcessor<TextureEffect>(fp) && static_cast<const TextureEffect*>(fp)->isYUV()) {
      continue;
    }
    for (size_t i = 0; i < fp->numTextureSamplers(); ++i) {
      AddReadSwizzle(fp->textureAt(i), &required);
    }
  }
  return required;
}

// The color graph names the gradient node; scanning it avoids re-walking the processor tree the
// lowering already consumed.
bool GraphHasGradientSource(const AOTEffectGraph& graph) {
  for (size_t index = 0; index < graph.nodeCount(); ++index) {
    const auto* node = graph.nodeAt(AOTNodeID(static_cast<uint32_t>(index)));
    if (node != nullptr && node->kind == AOTEffectKind::GradientSource) {
      return true;
    }
  }
  return false;
}

// The no-IR fallback, kept for the paths that match without a carried IR (tests and the
// pre-lowering probes): the production path reads the IR.
bool ContainsGradientSource(const FragmentProcessor* processor) {
  if (processor == nullptr) {
    return false;
  }
  if (IsProcessor<ClampedGradientEffect>(processor)) {
    return true;
  }
  for (size_t index = 0; index < processor->numChildProcessors(); ++index) {
    if (ContainsGradientSource(processor->childProcessor(index))) {
      return true;
    }
  }
  return false;
}

}  // namespace

DrawShape ExtractDrawShape(const ProgramInfo* programInfo) {
  DrawShape facts = {};
  facts.alphaOnlyTarget = programInfo->getOutputSwizzle() == Swizzle::AAAA();
  facts.colorFPCount = programInfo->numColorFragmentProcessors();
  facts.fragmentCount = programInfo->numFragmentProcessors();
  facts.requiredTextureReads = RequiredTextureReads(programInfo);
  facts.coverage = ClassifyCoverageTerms(programInfo);
  facts.ir = programInfo->getCarriedIR();
  // The geometry and xfer facts are match-time facts: the matching runs after the processors
  // exist, so they are described off the made processors — one description, no record-time copy.
  facts.geometry = DescribeGeometryProcessor(programInfo->getGeometryProcessor());
  facts.xfer = ClassifyXferForMatch(programInfo->getXferProcessor());
  if (facts.ir != nullptr) {
    facts.hasGradientSource = GraphHasGradientSource(facts.ir->color);
  } else {
    for (size_t index = 0; index < programInfo->numFragmentProcessors(); ++index) {
      if (ContainsGradientSource(programInfo->getFragmentProcessor(index))) {
        facts.hasGradientSource = true;
        break;
      }
    }
  }
  return facts;
}

}  // namespace tgfx

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
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/DrawIR.h"
#include <atomic>
#include "gpu/aot/AOTEffectDecomposer.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "gpu/processors/ComposeFragmentProcessor.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/processors/TextureEffect.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {
namespace {

// Diagnostic only (see AOTLowerDrawCount): successful LowerDraw runs since the last reset.
std::atomic<uint64_t> lowerDrawCount = {0};

// Flattens Compose processors recursively: one coverage FP may carry a whole clip stack, and the
// classifier works per leaf, not per position.
void FlattenCoverageLeaves(const FragmentProcessor* processor,
                           std::vector<const FragmentProcessor*>* leaves) {
  if (processor == nullptr) {
    return;
  }
  if (IsProcessor<ComposeFragmentProcessor>(processor)) {
    for (size_t index = 0; index < processor->numChildProcessors(); ++index) {
      FlattenCoverageLeaves(processor->childProcessor(index), leaves);
    }
    return;
  }
  leaves->push_back(processor);
}

bool IsAnalyticCoverageLeaf(const FragmentProcessor* processor) {
  return processor->numChildProcessors() == 0 &&
         (IsProcessor<RectEffect>(processor) || IsProcessor<RRectEffect>(processor));
}

// Lowers a consecutive run of coverage leaves into one term: a chain rooted at the
// geometry-coverage unit, each leaf consuming the previous link's output. The same chaining
// serves the analytic runs and the subtree/glyph runs; only the term's kind differs.
bool LowerCoverageRun(const std::vector<const FragmentProcessor*>& leaves, size_t begin, size_t end,
                      AOTCoverageTermKind kind, AOTCoverageTerm* term) {
  if (term == nullptr || begin >= end) {
    return false;
  }
  AOTNodeBuilder builder = {};
  AOTNodeID unit = AOTNodeID::Invalid();
  if (!builder.addGeometryCoverage(&unit)) {
    return false;
  }
  AOTNodeID current = unit;
  for (size_t index = begin; index < end; ++index) {
    AOTNodeID next = AOTNodeID::Invalid();
    if (!leaves[index]->lowerToAOT(&builder, current, &next)) {
      return false;
    }
    current = next;
  }
  if (current == unit) {
    return false;
  }
  term->kind = kind;
  term->root = current;
  return builder.finish(current, &term->graph);
}

}  // namespace

std::optional<std::vector<AOTCoverageTerm>> LowerCoverageTerms(
    const std::vector<const FragmentProcessor*>& coverageProcessors,
    const FragmentProcessor* glyphLeaf, BlockAllocator* allocator) {
  if (allocator == nullptr) {
    return std::nullopt;
  }
  // Classify every leaf by what it is. Analytic leaves chain into runs; a non-analytic leaf
  // closes the pending run and becomes its own term.
  std::vector<AOTCoverageTerm> terms = {};
  bool hasMask = false;
  bool hasSubtree = false;
  for (auto* termFP : coverageProcessors) {
    std::vector<const FragmentProcessor*> leaves = {};
    FlattenCoverageLeaves(termFP, &leaves);
    size_t runBegin = 0;
    for (size_t index = 0; index <= leaves.size(); ++index) {
      const bool closing = index == leaves.size();
      auto* leaf = closing ? nullptr : leaves[index];
      const bool inRun = !closing && leaf != glyphLeaf && IsAnalyticCoverageLeaf(leaf);
      if (inRun) {
        continue;
      }
      if (index > runBegin) {
        AOTCoverageTerm term = {};
        if (LowerCoverageRun(leaves, runBegin, index, AOTCoverageTermKind::Analytic, &term)) {
          terms.push_back(std::move(term));
        } else {
          // The run's analytic leaves did not lower; the run keeps the subtree semantics the
          // previous whole-term acceptance gave such a shape.
          if (hasSubtree ||
              !LowerCoverageRun(leaves, runBegin, index, AOTCoverageTermKind::Subtree, &term)) {
            return std::nullopt;
          }
          hasSubtree = true;
          terms.push_back(std::move(term));
        }
      }
      runBegin = index + 1;
      if (closing) {
        break;
      }
      if (leaf == glyphLeaf) {
        AOTCoverageTerm term = {};
        if (!LowerCoverageRun(leaves, index, index + 1, AOTCoverageTermKind::Glyph, &term)) {
          return std::nullopt;
        }
        terms.push_back(std::move(term));
        continue;
      }
      if (IsProcessor<DeviceSpaceTextureEffect>(leaf)) {
        auto* mask = static_cast<const DeviceSpaceTextureEffect*>(leaf);
        if (hasMask || !mask->isAlphaOnly() || mask->hasPerspective()) {
          return std::nullopt;
        }
        hasMask = true;
        AOTCoverageTerm term = {};
        term.kind = AOTCoverageTermKind::DeviceMask;
        term.mask.textureProxy = mask->getTextureProxy();
        term.mask.uvMatrix = mask->getUVMatrix();
        term.mask.samplingKind = AOTTextureSamplingKind::Device;
        term.mask.isAlphaOnly = true;
        term.mask.hasPerspective = false;
        terms.push_back(std::move(term));
        continue;
      }
      if (hasSubtree) {
        return std::nullopt;
      }
      hasSubtree = true;
      AOTCoverageTerm term = {};
      if (!LowerCoverageRun(leaves, index, index + 1, AOTCoverageTermKind::Subtree, &term)) {
        return std::nullopt;
      }
      terms.push_back(std::move(term));
    }
  }
  return terms;
}

std::optional<DrawIR> LowerDraw(const std::vector<const FragmentProcessor*>& colorProcessors,
                                const std::vector<const FragmentProcessor*>& coverageProcessors,
                                const GeometryProcessor* geometryProcessor,
                                const XferProcessor* xferProcessor, BlendMode blendMode,
                                BlockAllocator* allocator) {
  if (allocator == nullptr) {
    return std::nullopt;
  }
  DrawIR ir = {};
  ir.geometryProcessor = geometryProcessor;
  ir.xferProcessor = xferProcessor;
  ir.blendMode = blendMode;

  std::vector<const FragmentProcessor*> colors = colorProcessors;
  PlacementPtr<FragmentProcessor> unitColor = nullptr;
  if (colors.empty()) {
    // A color-free draw (a pure mask or clip fill) takes the paint color as-is, which is a
    // one-slot const-color chain modulating the geometry color.
    unitColor = ConstColorProcessor::Make(allocator, PMColor::White(), InputMode::ModulateRGBA);
    if (unitColor == nullptr) {
      return std::nullopt;
    }
    colors.push_back(unitColor.get());
  }
  if (!AOTEffectDecomposer::Lower(colors, &ir.color)) {
    return std::nullopt;
  }

  // The atlas glyph mask joins as a synthesized coverage leaf sourced from the maskCoord
  // attribute: only the alpha-only atlas readback is expressible — a color atlas replaces the
  // output color, which the chain cannot carry. A null geometry processor contributes none.
  const bool atlasText =
      geometryProcessor != nullptr && IsProcessor<AtlasTextGeometryProcessor>(geometryProcessor);
  if (coverageProcessors.size() > (atlasText ? 1u : 2u)) {
    return std::nullopt;
  }
  std::vector<PlacementPtr<FragmentProcessor>> ownedLeaves = {};
  std::vector<const FragmentProcessor*> termFPs = coverageProcessors;
  const FragmentProcessor* glyphLeaf = nullptr;
  if (atlasText) {
    auto* atlasGP = static_cast<const AtlasTextGeometryProcessor*>(geometryProcessor);
    auto atlasProxy = atlasGP->getTextureProxy();
    if (atlasProxy == nullptr || !atlasProxy->isAlphaOnly()) {
      return std::nullopt;
    }
    auto state = atlasGP->getSamplerState();
    SamplingOptions sampling(state.minFilterMode, state.magFilterMode, state.mipmapMode);
    SamplingArgs args = {TileMode::Clamp, TileMode::Clamp, sampling, SrcRectConstraint::Fast};
    // Identity: CoordTransform::getTotalMatrix applies the texture normalization itself, so the
    // leaf maps maskCoord straight into the atlas's normalized space.
    auto uvMatrix = Matrix::I();
    auto atlasLeaf = TextureEffect::Make(allocator, std::move(atlasProxy), args, &uvMatrix);
    if (atlasLeaf == nullptr) {
      return std::nullopt;
    }
    glyphLeaf = atlasLeaf.get();
    ownedLeaves.push_back(std::move(atlasLeaf));
    termFPs.push_back(glyphLeaf);
  }

  auto terms = LowerCoverageTerms(termFPs, glyphLeaf, allocator);
  if (!terms.has_value()) {
    return std::nullopt;
  }
  ir.coverage = std::move(*terms);
  lowerDrawCount.fetch_add(1, std::memory_order_relaxed);
  return ir;
}

uint64_t AOTLowerDrawCount() {
  return lowerDrawCount.load(std::memory_order_relaxed);
}

void ResetAOTLowerDrawCount() {
  lowerDrawCount.store(0, std::memory_order_relaxed);
}

}  // namespace tgfx

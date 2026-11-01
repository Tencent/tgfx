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

#include "gpu/aot/AOTDrawFacts.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/ClampedGradientEffect.h"
#include "gpu/processors/SingleIntervalGradientColorizer.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {

AOTDrawFacts ExtractAOTDrawFacts(const ProgramInfo* programInfo) {
  AOTDrawFacts facts = {};
  facts.info = programInfo;
  facts.alphaOnlyTarget = programInfo->getOutputSwizzle() == Swizzle::AAAA();
  facts.colorFPCount = programInfo->numColorFragmentProcessors();
  facts.fragmentCount = programInfo->numFragmentProcessors();
  facts.coverage = ClassifyCoverageTerms(programInfo);
  facts.ir = programInfo->getCarriedIR();
  if (facts.ir != nullptr) {
    facts.geometry = facts.ir->geometry;
    facts.xfer = facts.ir->xfer;
    facts.colorFromPaint = facts.ir->colorFromPaint;
  } else {
    // A draw without a carried IR still has exact facts: the descriptor twins read the made
    // processor and the xfer the same way the record-time forms would have.
    facts.geometry = DescribeGeometryProcessor(programInfo->getGeometryProcessor());
    facts.xfer = ClassifyXferForMatch(programInfo->getXferProcessor());
  }
  // The third channel: the sole coverage processor's raw texture facts, read live. A
  // prepare-grown mask (the rasterized shape) lowers to no record-time term, so its family reads
  // these instead of the IR — the same escape hatch as the YUV pending format.
  if (facts.fragmentCount == facts.colorFPCount + 1) {
    auto* fp = programInfo->getFragmentProcessor(static_cast<uint32_t>(facts.colorFPCount));
    if (IsProcessor<TextureEffect>(fp)) {
      auto* texture = static_cast<const TextureEffect*>(fp);
      facts.liveMask.present = true;
      facts.liveMask.alphaOnly = texture->isAlphaOnly();
      facts.liveMask.hasSubset = texture->hasSubset();
      facts.liveMask.isYUV = texture->isYUV();
      facts.liveMask.hasRGBAAA = texture->hasRGBAAA();
      facts.liveMask.hasSampler = texture->numTextureSamplers() > 0;
    }
  }
  // The third channel's color-side twin: the sole color processor's gradient facts, read live
  // for the same reason — the rasterized instanced shape's coverage grows at prepare, and the
  // dropped IR carried the color graph with it.
  if (facts.colorFPCount == 1) {
    auto* fp = programInfo->getFragmentProcessor(0);
    if (IsProcessor<ClampedGradientEffect>(fp)) {
      auto* gradient = static_cast<const ClampedGradientEffect*>(fp);
      facts.liveGradient.present = true;
      facts.liveGradient.singleInterval =
          gradient->numChildProcessors() == 2 &&
          IsProcessor<SingleIntervalGradientColorizer>(gradient->childProcessor(0));
      auto* layout =
          gradient->numChildProcessors() == 2 ? gradient->childProcessor(1) : nullptr;
      facts.liveGradient.hasPerspective =
          layout == nullptr || layout->numCoordTransforms() != 1 ||
          layout->coordTransform(0)->matrix.hasPerspective();
    }
  }
  return facts;
}

}  // namespace tgfx

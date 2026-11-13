/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "layers/processors/GlassUDFTentBlurFragmentProcessor.h"
#include "core/utils/Log.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {

// Returns the shader expression for sampling a child at a coord offset by the tent kernel.
// "offset" and "offsetValue" are shader variables declared in the emitted code.
PlacementPtr<FragmentProcessor> GlassUDFTentBlurFragmentProcessor::Make(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> fineSource,
    PlacementPtr<FragmentProcessor> coarseSource, float fineRadius, float coarseRadius,
    GlassUDFBlurDirection direction, int maxRadius, bool inputIsPacked, GlassUDFField field) {
  if (allocator == nullptr) {
    return nullptr;
  }
  bool usesFine = field == GlassUDFField::Refraction;
  if (usesFine && (fineSource == nullptr || fineRadius <= 0.0f)) {
    return nullptr;
  }
  if (!usesFine && (coarseSource == nullptr || coarseRadius <= 0.0f)) {
    return nullptr;
  }
  if (maxRadius < 1) {
    return nullptr;
  }
  return allocator->make<GlassUDFTentBlurFragmentProcessor>(
      std::move(fineSource), std::move(coarseSource), fineRadius, coarseRadius, direction,
      maxRadius, inputIsPacked, field);
}

GlassUDFTentBlurFragmentProcessor::GlassUDFTentBlurFragmentProcessor(
    PlacementPtr<FragmentProcessor> fineSource, PlacementPtr<FragmentProcessor> coarseSource,
    float fineRadius, float coarseRadius, GlassUDFBlurDirection direction, int maxRadius,
    bool inputIsPacked, GlassUDFField field)
    : FragmentProcessor(ClassID()), fineRadius(fineRadius), coarseRadius(coarseRadius),
      direction(direction), maxRadius(maxRadius), inputIsPacked(inputIsPacked), field(field) {
  // The lowered node reads its source off child 0 (see lowerToAOT), so the active source is the
  // one registered child either way.
  if (field == GlassUDFField::EdgeLight) {
    registerChildProcessor(std::move(coarseSource));
  } else {
    registerChildProcessor(std::move(fineSource));
  }
}

bool GlassUDFTentBlurFragmentProcessor::lowerToAOT(AOTNodeBuilder* builder, AOTNodeID input,
                                                   AOTNodeID* output) const {
  // Same contract as the gaussian blur's opaque node: the active source child lowers into the
  // graph (a texture leaf — the horizontal pass reads a plain TextureEffect, the vertical a
  // TiledTextureEffect), everything else stays inside the family's kernel. The matcher reads
  // the child's servable shape and the radius cap off the node.
  if (builder == nullptr || output == nullptr || numChildProcessors() != 1) {
    return false;
  }
  AOTNodeID childNode = AOTNodeID::Invalid();
  if (!childProcessor(0)->lowerToAOT(builder, input, &childNode)) {
    return false;
  }
  AOTOpaqueKernelParameters parameters = {};
  parameters.kernel = AOTOpaqueKernel::GlassUDFTentBlur;
  parameters.maxRadius = maxRadius;
  parameters.tentFineRadius = fineRadius;
  parameters.tentCoarseRadius = coarseRadius;
  parameters.tentField = static_cast<int>(field);
  parameters.tentInputIsPacked = inputIsPacked ? 1 : 0;
  parameters.tentVertical = direction == GlassUDFBlurDirection::Vertical;
  return builder->addOpaqueKernel(childNode, parameters, output);
}

}  // namespace tgfx

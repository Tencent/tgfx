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
  // The source of the requested field is always child 0, so onSetData reads the step matrix from a
  // valid child either way.
  if (field == GlassUDFField::EdgeLight) {
    registerChildProcessor(std::move(coarseSource));
  } else {
    registerChildProcessor(std::move(fineSource));
  }
}

void GlassUDFTentBlurFragmentProcessor::onComputeProcessorKey(BytesKey* key) const {
  key->write(maxRadius);
  key->write(static_cast<uint32_t>(inputIsPacked));
  key->write(static_cast<uint32_t>(field));
}

void GlassUDFTentBlurFragmentProcessor::onSetData(UniformData*,
                                                  UniformData* fragmentUniformData) const {
  auto processor = childProcessor(0);
  Point stepVectors[] = {{0, 0}, {1.0f, 0}};
  if (direction == GlassUDFBlurDirection::Vertical) {
    stepVectors[1] = {0, 1.0f};
  }
  DEBUG_ASSERT(processor->numCoordTransforms() == 1);
  auto matrix = processor->coordTransform(0)->getTotalMatrix();
  matrix.mapPoints(stepVectors, 2);
  Point step = stepVectors[1] - stepVectors[0];
  Point radii = {fineRadius, coarseRadius};
  fragmentUniformData->setData("GlassUDFRadius", radii);
  fragmentUniformData->setData("GlassUDFStep", step);
  // The precompiled tent-blur kernel selects its tap path with this flag: a TiledTextureEffect
  // child must run the Decal/tiling tap semantics (out-of-subset reads resolve to the border),
  // while a plain TextureEffect child only clamps to the subset. GaussianBlur1D uploads the same
  // flag for the same reason; without it the kernel stays on the plain-clamp path and pulls edge
  // texels into the blur wherever the sampling window leaves the subset.
  fragmentUniformData->setDataOptional("TiledChild",
                                       IsProcessor<TiledTextureEffect>(processor) ? 1 : 0);
  // Default for the shared kernel's runtime device mask; a present mask FP overwrites it with 1
  // later in traversal. The uniform ring buffer is recycled without clearing, so leaving this
  // unwritten makes the draw inherit an earlier draw's flag and sample the padded dummy mask
  // (or skip a real one) — the same default GaussianBlur1D and the other runtime-mask kernels
  // upload.
  fragmentUniformData->setDataOptional("HasDeviceMask", 0);
  // The precompiled tent-blur kernel folds the field selection and the packed-input decode into
  // runtime uniforms, so both values ride this same onSetData path.
  if (fragmentUniformData->hasField("Field")) {
    fragmentUniformData->setData("Field", static_cast<int>(field));
  }
  if (fragmentUniformData->hasField("InputIsPacked")) {
    fragmentUniformData->setData("InputIsPacked", inputIsPacked ? 1 : 0);
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
  return builder->addOpaqueKernel(childNode, parameters, output);
}

}  // namespace tgfx

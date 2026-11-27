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
#include "GeneratedOperators.h"
#include "core/utils/Log.h"
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

void GlassUDFTentBlurFragmentProcessor::emitCode(EmitArgs& args) const {
  if (args.irBuilder == nullptr || args.outputValue == INVALID_VALUE_ID) {
    return;
  }
  if (args.textureSamplers == nullptr || args.transformedCoords == nullptr) {
    return;
  }
  auto childSamplers = args.textureSamplers->childInputs(0);
  auto childCoords = args.transformedCoords->childInputs(0);
  if (childSamplers.count() != 1 || childCoords.count() < 1 ||
      childCoords[0].type() != SLType::Float2) {
    args.irBuilder->MarkFailed("the UDF tent blur child input is not a single vec2 texture read");
    return;
  }
  auto ir = args.irBuilder;
  const auto samplerValue = args.samplerValue(childSamplers[0]);
  if (samplerValue == INVALID_VALUE_ID) {
    args.irBuilder->MarkFailed("the UDF tent blur sampler has no IR value");
    return;
  }
  const auto texCoord =
      ir->InternValue(childCoords[0].name(), SLType::Float2, Precision::High, IRValueKind::Varying);
  if (texCoord == INVALID_VALUE_ID) {
    args.irBuilder->MarkFailed("the UDF tent blur coordinate has no IR value");
    return;
  }
  GlassUDFTentBlurOp::UniformNames names = {};
  const int32_t maxRadiusInt = static_cast<int32_t>(maxRadius);
  const int32_t refractionPacked = (field == GlassUDFField::Refraction && inputIsPacked) ? 1 : 0;
  const int32_t edgeLight = field == GlassUDFField::EdgeLight ? 1 : 0;
  ir->StoreOutput(args.outputValue,
                  GlassUDFTentBlurOp::Emit(ir, args.uniformHandler, &names, samplerValue,
                                           texCoord, maxRadiusInt, refractionPacked, edgeLight));
}

void GlassUDFTentBlurFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                                  UniformData* fragmentUniformData) const {
  auto processor = childProcessor(0);
  Point stepVectors[] = {{0, 0}, {1, 0}};
  if (direction == GlassUDFBlurDirection::Vertical) {
    stepVectors[1] = {0, 1};
  }

  DEBUG_ASSERT(processor->numCoordTransforms() == 1);
  auto transform = processor->coordTransform(0);
  auto matrix = transform->getTotalMatrix();
  matrix.mapPoints(stepVectors, 2);
  Point step = stepVectors[1] - stepVectors[0];
  fragmentUniformData->setData("Radius",
                               field == GlassUDFField::Refraction ? fineRadius : coarseRadius);
  fragmentUniformData->setData("Step", step);
}



void GlassUDFTentBlurFragmentProcessor::onComputeProcessorKey(BytesKey* key) const {
  key->write(maxRadius);
  key->write(static_cast<uint32_t>(field));
  key->write(static_cast<uint32_t>(inputIsPacked));
}

}  // namespace tgfx

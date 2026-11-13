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

#include "gpu/processors/PerlinNoiseFragmentProcessor.h"
#include <algorithm>
#include <cstring>
#include "core/shaders/PerlinNoiseShader.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/resources/TextureView.h"

namespace tgfx {

PerlinNoiseFragmentProcessor::PerlinNoiseFragmentProcessor(
    PerlinNoiseType noiseType, int numOctaves, bool stitchTiles,
    std::unique_ptr<PerlinNoiseShader::PaintingData> paintingData,
    std::shared_ptr<TextureView> permutationsView, std::shared_ptr<TextureView> noiseView,
    const Matrix* uvMatrix, bool hasOriginalLookupData)
    : FragmentProcessor(ClassID()), hasOriginalLookupData(hasOriginalLookupData),
      noiseType(noiseType), numOctaves(numOctaves), stitchTiles(stitchTiles),
      paintingData(std::move(paintingData)), permutationsView(std::move(permutationsView)),
      noiseView(std::move(noiseView)) {
  if (uvMatrix) {
    coordTransform = CoordTransform(*uvMatrix);
  }
  addCoordTransform(&coordTransform);
}

std::shared_ptr<Texture> PerlinNoiseFragmentProcessor::onTextureAt(size_t index) const {
  if (index == 0) {
    return permutationsView->getTexture();
  }
  return noiseView->getTexture();
}

SamplerState PerlinNoiseFragmentProcessor::onSamplerStateAt(size_t) const {
  return SamplerState(SamplingOptions(FilterMode::Nearest));
}

PlacementPtr<PerlinNoiseFragmentProcessor> PerlinNoiseFragmentProcessor::MakeFromAOTParameters(
    BlockAllocator* allocator, const AOTPerlinNoiseParameters& parameters) {
  auto paintingData = std::make_unique<PerlinNoiseShader::PaintingData>(
      parameters.baseFrequencyX, parameters.baseFrequencyY, parameters.stitchWidth,
      parameters.stitchHeight);
  auto processor =
      MakeFromViews(allocator, static_cast<PerlinNoiseType>(parameters.noiseType),
                    parameters.numOctaves, parameters.stitchTiles, std::move(paintingData),
                    parameters.permutationsView, parameters.noiseView, &parameters.uvMatrix);
  if (processor != nullptr) {
    processor->lookupData = parameters.lookupData;
  }
  return processor;
}

bool PerlinNoiseFragmentProcessor::lowerToAOT(AOTNodeBuilder* builder, AOTNodeID input,
                                              AOTNodeID* output) const {
  if (builder == nullptr || output == nullptr) {
    return false;
  }
  AOTPerlinNoiseParameters parameters = {};
  parameters.noiseType = static_cast<int>(noiseType);
  parameters.numOctaves = numOctaves;
  parameters.stitchTiles = stitchTiles;
  parameters.permutationsView = permutationsView;
  parameters.noiseView = noiseView;
  parameters.baseFrequencyX = paintingData->baseFrequencyX;
  parameters.baseFrequencyY = paintingData->baseFrequencyY;
  parameters.stitchWidth = paintingData->stitchWidth;
  parameters.stitchHeight = paintingData->stitchHeight;
  parameters.uvMatrix = coordTransform.matrix;
  if (lookupData == nullptr && hasOriginalLookupData) {
    auto data = std::make_shared<AOTPerlinNoiseLookupData>();
    static_assert(sizeof(paintingData->latticeSelector) == sizeof(data->permutations));
    static_assert(sizeof(paintingData->noise) == sizeof(data->noise));
    std::memcpy(data->permutations.data(), paintingData->latticeSelector,
                data->permutations.size());
    std::memcpy(data->noise.data(), paintingData->noise, data->noise.size());
    lookupData = std::move(data);
  }
  parameters.lookupData = lookupData;
  return builder->addPerlinNoiseSource(input, parameters, output);
}

PlacementPtr<PerlinNoiseFragmentProcessor> PerlinNoiseFragmentProcessor::Make(
    BlockAllocator* allocator, Context* context, PerlinNoiseType noiseType, int numOctaves,
    bool stitchTiles, std::unique_ptr<PerlinNoiseShader::PaintingData> paintingData,
    const Matrix* uvMatrix) {
  if (paintingData == nullptr) {
    return nullptr;
  }

  // Upload permutations texture: 256x1 A8.
  auto permutationsView = TextureView::MakeAlpha(
      context, PerlinNoiseShader::BLOCK_SIZE, 1, paintingData->latticeSelector,
      static_cast<size_t>(PerlinNoiseShader::BLOCK_SIZE), false);
  if (permutationsView == nullptr) {
    return nullptr;
  }

  // Upload noise texture: 256x4 RGBA8888.
  // noise[4][256][2] is uint16_t array. Each row is one channel (R/G/B/A of the output noise).
  // Each uint16_t pair [gradX, gradY] maps to one RGBA pixel:
  //   R = low8(gradX), G = high8(gradX), B = low8(gradY), A = high8(gradY)
  auto noiseView =
      TextureView::MakeRGBA(context, PerlinNoiseShader::BLOCK_SIZE, 4,
                            reinterpret_cast<const uint8_t*>(&paintingData->noise[0][0][0]),
                            static_cast<size_t>(PerlinNoiseShader::BLOCK_SIZE) * 4, false);
  if (noiseView == nullptr) {
    return nullptr;
  }

  return allocator->make<PerlinNoiseFragmentProcessor>(
      noiseType, numOctaves, stitchTiles, std::move(paintingData), std::move(permutationsView),
      std::move(noiseView), uvMatrix, true);
}

PlacementPtr<PerlinNoiseFragmentProcessor> PerlinNoiseFragmentProcessor::MakeFromViews(
    BlockAllocator* allocator, PerlinNoiseType noiseType, int numOctaves, bool stitchTiles,
    std::unique_ptr<PerlinNoiseShader::PaintingData> paintingData,
    std::shared_ptr<TextureView> permutationsView, std::shared_ptr<TextureView> noiseView,
    const Matrix* uvMatrix) {
  if (paintingData == nullptr || permutationsView == nullptr || noiseView == nullptr) {
    return nullptr;
  }
  return allocator->make<PerlinNoiseFragmentProcessor>(
      noiseType, numOctaves, stitchTiles, std::move(paintingData), std::move(permutationsView),
      std::move(noiseView), uvMatrix);
}

}  // namespace tgfx

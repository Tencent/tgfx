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

#include "GLSLPerlinNoiseFragmentProcessor.h"
#include "core/shaders/PerlinNoiseShader.h"
#include "gpu/resources/TextureView.h"

namespace tgfx {

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

  return allocator->make<GLSLPerlinNoiseFragmentProcessor>(
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
  return allocator->make<GLSLPerlinNoiseFragmentProcessor>(
      noiseType, numOctaves, stitchTiles, std::move(paintingData), std::move(permutationsView),
      std::move(noiseView), uvMatrix);
}

GLSLPerlinNoiseFragmentProcessor::GLSLPerlinNoiseFragmentProcessor(
    PerlinNoiseType noiseType, int numOctaves, bool stitchTiles,
    std::unique_ptr<PerlinNoiseShader::PaintingData> paintingData,
    std::shared_ptr<TextureView> permutationsView, std::shared_ptr<TextureView> noiseView,
    const Matrix* uvMatrix, bool hasOriginalLookupData)
    : PerlinNoiseFragmentProcessor(noiseType, numOctaves, stitchTiles, std::move(paintingData),
                                   std::move(permutationsView), std::move(noiseView), uvMatrix,
                                   hasOriginalLookupData) {
}

void GLSLPerlinNoiseFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                                 UniformData* fragmentUniformData) const {
  float baseFreq[2] = {paintingData->baseFrequencyX, paintingData->baseFrequencyY};
  fragmentUniformData->setData("baseFrequency", baseFreq);

  float stitchDataValues[2] = {0.0f, 0.0f};
  if (stitchTiles) {
    stitchDataValues[0] = static_cast<float>(paintingData->stitchWidth);
    stitchDataValues[1] = static_cast<float>(paintingData->stitchHeight);
  }
  fragmentUniformData->setDataOptional("stitchData", stitchDataValues);
}
}  // namespace tgfx

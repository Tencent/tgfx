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

#include "GeneratedOperators.h"
#include "GLSLPerlinNoiseFragmentProcessor.h"
#include "core/shaders/PerlinNoiseShader.h"
#include "gpu/resources/TextureView.h"

namespace tgfx {

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
      std::move(noiseView), uvMatrix);
}

GLSLPerlinNoiseFragmentProcessor::GLSLPerlinNoiseFragmentProcessor(
    PerlinNoiseType noiseType, int numOctaves, bool stitchTiles,
    std::unique_ptr<PerlinNoiseShader::PaintingData> paintingData,
    std::shared_ptr<TextureView> permutationsView, std::shared_ptr<TextureView> noiseView,
    const Matrix* uvMatrix)
    : PerlinNoiseFragmentProcessor(noiseType, numOctaves, stitchTiles, std::move(paintingData),
                                   std::move(permutationsView), std::move(noiseView), uvMatrix) {
}

void GLSLPerlinNoiseFragmentProcessor::emitCode(EmitArgs& args) const {
  if (args.irBuilder == nullptr || args.outputValue == INVALID_VALUE_ID ||
      args.textureSamplers == nullptr || args.transformedCoords == nullptr ||
      args.textureSamplers->count() < 2 || args.transformedCoords->count() < 1) {
    return;
  }
  auto ir = args.irBuilder;
  const auto permSampler = args.samplerValue((*args.textureSamplers)[0]);
  const auto noiseSampler = args.samplerValue((*args.textureSamplers)[1]);
  if (permSampler == INVALID_VALUE_ID || noiseSampler == INVALID_VALUE_ID) {
    ir->MarkFailed("the perlin noise samplers have no IR values");
    return;
  }
  const auto coord = ir->InternValue((*args.transformedCoords)[0].name(), SLType::Float2,
                                     Precision::High, IRValueKind::Varying);
  if (coord == INVALID_VALUE_ID) {
    ir->MarkFailed("the perlin noise coordinate has no IR value");
    return;
  }
  PerlinNoiseKernelOp::UniformNames names = {};
  ir->StoreOutput(args.outputValue,
                  PerlinNoiseKernelOp::Emit(ir, args.uniformHandler, &names, permSampler,
                                            noiseSampler, coord,
                                            static_cast<int32_t>(numOctaves),
                                            static_cast<int32_t>(noiseType),
                                            stitchTiles ? 1 : 0));
}

void GLSLPerlinNoiseFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                                 UniformData* fragmentUniformData) const {
  float baseFreq[2] = {paintingData->baseFrequencyX, paintingData->baseFrequencyY};
  fragmentUniformData->setData("baseFrequency", baseFreq);

  // The kernel always reads the member, and the uniform buffer is pooled and reused, so a skipped
  // write would expose the previous draw's bytes. Zero is the no-stitching value the shader's step
  // term expects.
  float stitchDataValues[2] = {0.0f, 0.0f};
  if (stitchTiles) {
    stitchDataValues[0] = static_cast<float>(paintingData->stitchWidth);
    stitchDataValues[1] = static_cast<float>(paintingData->stitchHeight);
  }
  fragmentUniformData->setData("stitchData", stitchDataValues);
}
}  // namespace tgfx

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

#include "PerlinNoiseShader.h"
#include <algorithm>
#include <cmath>
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/PerlinNoiseFragmentProcessor.h"

namespace tgfx {

static std::shared_ptr<PerlinNoiseShader> MakePerlinNoise(PerlinNoiseType noiseType,
                                                          float baseFrequencyX,
                                                          float baseFrequencyY, int numOctaves,
                                                          float seed, const ISize* tileSize) {
  if (baseFrequencyX <= 0 || baseFrequencyY <= 0) {
    return nullptr;
  }
  if (numOctaves < 1) {
    return nullptr;
  }
  return std::make_shared<PerlinNoiseShader>(noiseType, baseFrequencyX, baseFrequencyY, numOctaves,
                                             seed, tileSize);
}

std::shared_ptr<Shader> Shader::MakeFractalNoise(float baseFrequencyX, float baseFrequencyY,
                                                 int numOctaves, float seed,
                                                 const ISize* tileSize) {
  auto shader = MakePerlinNoise(PerlinNoiseType::FractalNoise, baseFrequencyX, baseFrequencyY,
                                numOctaves, seed, tileSize);
  if (shader) {
    shader->weakThis = shader;
  }
  return shader;
}

std::shared_ptr<Shader> Shader::MakeTurbulence(float baseFrequencyX, float baseFrequencyY,
                                               int numOctaves, float seed, const ISize* tileSize) {
  auto shader = MakePerlinNoise(PerlinNoiseType::Turbulence, baseFrequencyX, baseFrequencyY,
                                numOctaves, seed, tileSize);
  if (shader) {
    shader->weakThis = shader;
  }
  return shader;
}

PerlinNoiseShader::PerlinNoiseShader(PerlinNoiseType noiseType, float baseFrequencyX,
                                     float baseFrequencyY, int numOctaves, float seed,
                                     const ISize* tileSize)
    : noiseType(noiseType), baseFrequencyX(baseFrequencyX), baseFrequencyY(baseFrequencyY),
      numOctaves(std::min(numOctaves, MAX_OCTAVES)), seed(seed),
      tileSize(tileSize ? *tileSize : ISize::MakeEmpty()),
      stitchTiles(tileSize != nullptr && !tileSize->isEmpty()) {
}

std::unique_ptr<PerlinNoiseShader::PaintingData> PerlinNoiseShader::getPaintingData() const {
  return std::make_unique<PaintingData>(seed, baseFrequencyX, baseFrequencyY, tileSize);
}

bool PerlinNoiseShader::isEqual(const Shader* shader) const {
  auto other = static_cast<const PerlinNoiseShader*>(shader);
  return noiseType == other->noiseType && baseFrequencyX == other->baseFrequencyX &&
         baseFrequencyY == other->baseFrequencyY && numOctaves == other->numOctaves &&
         seed == other->seed && tileSize == other->tileSize;
}

PlacementPtr<FragmentProcessor> PerlinNoiseShader::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix,
    const std::shared_ptr<ColorSpace>& /*dstColorSpace*/) const {
  auto data = getPaintingData();
  if (data == nullptr) {
    return nullptr;
  }
  auto allocator = args.context->drawingAllocator();
  return PerlinNoiseFragmentProcessor::Make(allocator, args.context, noiseType, numOctaves,
                                            stitchTiles, std::move(data), uvMatrix);
}

// --- PaintingData implementation ---

PerlinNoiseShader::PaintingData::PaintingData(float seed, float baseFreqX, float baseFreqY,
                                              const ISize& tileSize)
    : baseFrequencyX(baseFreqX), baseFrequencyY(baseFreqY) {
  init(seed);
  if (!tileSize.isEmpty()) {
    stitch(tileSize);
  }
}

PerlinNoiseShader::PaintingData::PaintingData(const PaintingData& that)
    : baseFrequencyX(that.baseFrequencyX), baseFrequencyY(that.baseFrequencyY),
      stitchWidth(that.stitchWidth), stitchWrapX(that.stitchWrapX), stitchHeight(that.stitchHeight),
      stitchWrapY(that.stitchWrapY), currentSeed(that.currentSeed) {
  memcpy(latticeSelector, that.latticeSelector, sizeof(latticeSelector));
  memcpy(noise, that.noise, sizeof(noise));
}

int PerlinNoiseShader::PaintingData::random() {
  int result = RAND_AMPLITUDE * (currentSeed % RAND_Q) - RAND_R * (currentSeed / RAND_Q);
  if (result <= 0) {
    result += RAND_MAXIMUM;
  }
  currentSeed = result;
  return result;
}

void PerlinNoiseShader::PaintingData::init(float seed) {
  // According to the SVG spec, we must truncate (not round) the seed value.
  currentSeed = static_cast<int>(seed);
  // The seed value clamp to the range [1, RAND_MAXIMUM - 1].
  if (currentSeed <= 0) {
    currentSeed = -(currentSeed % (RAND_MAXIMUM - 1)) + 1;
  }
  if (currentSeed > RAND_MAXIMUM - 1) {
    currentSeed = RAND_MAXIMUM - 1;
  }
  for (int channel = 0; channel < 4; ++channel) {
    for (int i = 0; i < BLOCK_SIZE; ++i) {
      latticeSelector[i] = static_cast<uint8_t>(i);
      noise[channel][i][0] = static_cast<uint16_t>(random() % (2 * BLOCK_SIZE));
      noise[channel][i][1] = static_cast<uint16_t>(random() % (2 * BLOCK_SIZE));
    }
  }
  // Fisher-Yates shuffle
  for (int i = BLOCK_SIZE - 1; i > 0; --i) {
    int k = latticeSelector[i];
    int j = random() % BLOCK_SIZE;
    latticeSelector[i] = latticeSelector[j];
    latticeSelector[j] = static_cast<uint8_t>(k);
  }

  // Apply permutations to noise data
  {
    uint16_t noiseCopy[4][BLOCK_SIZE][2];
    memcpy(noiseCopy, noise, sizeof(noiseCopy));
    for (int i = 0; i < BLOCK_SIZE; ++i) {
      for (int channel = 0; channel < 4; ++channel) {
        for (int j = 0; j < 2; ++j) {
          noise[channel][i][j] = noiseCopy[channel][latticeSelector[i]][j];
        }
      }
    }
  }

  // Compute normalized gradients from permuted noise data
  static constexpr float HALF_MAX_16BITS = 32767.5f;
  static constexpr float INV_BLOCK_SIZE = 1.0f / static_cast<float>(BLOCK_SIZE);
  for (int channel = 0; channel < 4; ++channel) {
    for (int i = 0; i < BLOCK_SIZE; ++i) {
      float gradX = (static_cast<float>(noise[channel][i][0]) - BLOCK_SIZE) * INV_BLOCK_SIZE;
      float gradY = (static_cast<float>(noise[channel][i][1]) - BLOCK_SIZE) * INV_BLOCK_SIZE;
      float length = std::sqrt(gradX * gradX + gradY * gradY);
      if (length > 0.0f) {
        gradX /= length;
        gradY /= length;
      }
      noise[channel][i][0] = static_cast<uint16_t>(std::lround((gradX + 1.0f) * HALF_MAX_16BITS));
      noise[channel][i][1] = static_cast<uint16_t>(std::lround((gradY + 1.0f) * HALF_MAX_16BITS));
    }
  }
}

void PerlinNoiseShader::PaintingData::stitch(const ISize& tileSizeIn) {
  auto tileWidth = static_cast<float>(tileSizeIn.width);
  auto tileHeight = static_cast<float>(tileSizeIn.height);
  if (baseFrequencyX != 0.0f) {
    float lowFreqX = std::floor(tileWidth * baseFrequencyX) / tileWidth;
    float highFreqX = std::ceil(tileWidth * baseFrequencyX) / tileWidth;
    if (lowFreqX != 0.0f && baseFrequencyX / lowFreqX < highFreqX / baseFrequencyX) {
      baseFrequencyX = lowFreqX;
    } else {
      baseFrequencyX = highFreqX;
    }
  }
  if (baseFrequencyY != 0.0f) {
    float lowFreqY = std::floor(tileHeight * baseFrequencyY) / tileHeight;
    float highFreqY = std::ceil(tileHeight * baseFrequencyY) / tileHeight;
    if (lowFreqY != 0.0f && baseFrequencyY / lowFreqY < highFreqY / baseFrequencyY) {
      baseFrequencyY = lowFreqY;
    } else {
      baseFrequencyY = highFreqY;
    }
  }
  float stitchW = tileWidth * baseFrequencyX;
  float stitchH = tileHeight * baseFrequencyY;
  stitchWidth = std::min(static_cast<int>(std::lround(stitchW)),
                         std::numeric_limits<int>::max() - PERLIN_NOISE);
  stitchWrapX = PERLIN_NOISE + stitchWidth;
  stitchHeight = std::min(static_cast<int>(std::lround(stitchH)),
                          std::numeric_limits<int>::max() - PERLIN_NOISE);
  stitchWrapY = PERLIN_NOISE + stitchHeight;
}

}  // namespace tgfx

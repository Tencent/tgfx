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

#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include "tgfx/core/Shader.h"
#include "tgfx/core/Size.h"

namespace tgfx {

enum class PerlinNoiseType { FractalNoise, Turbulence };

class PerlinNoiseShader : public Shader {
 public:
  static constexpr int BLOCK_SIZE = 256;
  static constexpr int MAX_OCTAVES = 255;
  // Half of the largest possible value for an unsigned 16-bit int. Used to encode the normalized
  // gradient vectors into the noise table as two uint16 values.
  static constexpr float HALF_MAX_16BITS = 32767.5f;

  struct PaintingData {
    PaintingData(float seed, float baseFrequencyX, float baseFrequencyY, const ISize& tileSize);

    PaintingData(const PaintingData& that);

    uint8_t latticeSelector[BLOCK_SIZE];
    uint16_t noise[4][BLOCK_SIZE][2];
    float baseFrequencyX;
    float baseFrequencyY;
    int stitchWidth = 0;
    int stitchWrapX = 0;
    int stitchHeight = 0;
    int stitchWrapY = 0;

   private:
    static constexpr int PERLIN_NOISE = 4096;
    static constexpr int RAND_MAXIMUM = 2147483647;  // 2^31 - 1
    static constexpr int RAND_AMPLITUDE = 16807;     // 7^5, primitive root of m
    static constexpr int RAND_Q = 127773;            // m / a
    static constexpr int RAND_R = 2836;              // m % a

    int currentSeed = 0;

    int random();
    void init(float seed);
    void stitch(const ISize& tileSize);
  };

  PerlinNoiseShader(PerlinNoiseType noiseType, float baseFrequencyX, float baseFrequencyY,
                    int numOctaves, float seed, const ISize* tileSize);

  std::unique_ptr<PaintingData> getPaintingData() const;

  PerlinNoiseType noiseType;
  float baseFrequencyX;
  float baseFrequencyY;
  int numOctaves;
  float seed;
  ISize tileSize;
  bool stitchTiles;

 protected:
  Type type() const override {
    return Type::PerlinNoise;
  }

  bool isEqual(const Shader* shader) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(
      const FPArgs& args, const Matrix* uvMatrix,
      const std::shared_ptr<ColorSpace>& dstColorSpace) const override;
};
}  // namespace tgfx

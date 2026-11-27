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
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <vector>
#include "tgfx/core/Point.h"

namespace tgfx {

static constexpr int TextureGrainTableSize = 256;
constexpr int TextureGrainOctaveCount = 3;
constexpr float TextureGrainPersistence = 0.5f;
constexpr float TextureGrainOctaveScale = 2.0f;
constexpr float TextureGrainDisplacementScale = 2.5f;

constexpr float TextureGrainWeightSum() {
  float sum = 0.0f;
  float weight = 1.0f;
  for (int octave = 0; octave < TextureGrainOctaveCount; ++octave) {
    sum += weight;
    weight *= TextureGrainPersistence;
  }
  return sum;
}

constexpr float TextureGrainMaximumOctaveScale() {
  float scale = 1.0f;
  for (int octave = 1; octave < TextureGrainOctaveCount; ++octave) {
    scale *= TextureGrainOctaveScale;
  }
  return scale;
}

const std::vector<uint8_t>& TextureGrainGradientTable();

// Components are in [0, 255].
Point TextureGrainSeedOffset(uint32_t seed, int octave);

Point TextureGrainSignedFBM(const uint8_t* gradientTable, float x, float y, uint32_t seed);

}  // namespace tgfx

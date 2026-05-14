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

#include <memory>
#include "core/shaders/PerlinNoiseShader.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {
class Context;
class Texture;

class PerlinNoiseFragmentProcessor : public FragmentProcessor {
 public:
  static PlacementPtr<PerlinNoiseFragmentProcessor> Make(
      BlockAllocator* allocator, Context* context, PerlinNoiseType noiseType, int numOctaves,
      bool stitchTiles, std::unique_ptr<PerlinNoiseShader::PaintingData> paintingData,
      const Matrix* uvMatrix);

  std::string name() const override {
    return "PerlinNoiseFragmentProcessor";
  }

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  size_t onCountTextureSamplers() const override {
    return 2;
  }

  std::shared_ptr<Texture> onTextureAt(size_t index) const override;

  SamplerState onSamplerStateAt(size_t index) const override;

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  PerlinNoiseFragmentProcessor(PerlinNoiseType noiseType, int numOctaves, bool stitchTiles,
                               std::unique_ptr<PerlinNoiseShader::PaintingData> paintingData,
                               std::shared_ptr<Texture> permutationsTexture,
                               std::shared_ptr<Texture> noiseTexture, const Matrix* uvMatrix);

  PerlinNoiseType noiseType;
  int numOctaves;
  bool stitchTiles;
  std::unique_ptr<PerlinNoiseShader::PaintingData> paintingData;
  std::shared_ptr<Texture> permutationsTexture;
  std::shared_ptr<Texture> noiseTexture;
  CoordTransform coordTransform;
};
}  // namespace tgfx

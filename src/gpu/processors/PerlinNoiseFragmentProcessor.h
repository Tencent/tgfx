/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include "core/shaders/PerlinNoiseShader.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {
class Context;
class Texture;
class TextureView;

// The procedural-noise source processor. The precompiled side is served by the pointwise chain's
// NoiseGraph resource kind: the noise is a leaf in the chain graph (one instruction slot, its
// parameters in the chain's fixed noise block), and any operators composed above it are ordinary
// chain slots — the dedicated perlin kernel and its folded slot records were removed with it.
class PerlinNoiseFragmentProcessor : public FragmentProcessor {
 public:
  friend class BlockAllocator;
  DEFINE_PROCESSOR_CLASS_ID
  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  static PlacementPtr<PerlinNoiseFragmentProcessor> Make(
      BlockAllocator* allocator, Context* context, PerlinNoiseType noiseType, int numOctaves,
      bool stitchTiles, std::unique_ptr<PerlinNoiseShader::PaintingData> paintingData,
      const Matrix* uvMatrix);

  // Rebuilds a PerlinNoiseFragmentProcessor from already-built lookup textures, skipping the
  // TextureView::MakeAlpha/MakeRGBA upload that Make() performs. Used by the chain plan's
  // executor to reconstruct the processor from a lowered AOTPerlinNoiseParameters, whose
  // permutationsView and noiseView were captured at lowerToAOT() time and must not be
  // regenerated.
  static PlacementPtr<PerlinNoiseFragmentProcessor> MakeFromViews(
      BlockAllocator* allocator, PerlinNoiseType noiseType, int numOctaves, bool stitchTiles,
      std::unique_ptr<PerlinNoiseShader::PaintingData> paintingData,
      std::shared_ptr<TextureView> permutationsView, std::shared_ptr<TextureView> noiseView,
      const Matrix* uvMatrix);

  std::string name() const override {
    return "PerlinNoiseFragmentProcessor";
  }

  PerlinNoiseType getNoiseType() const {
    return noiseType;
  }

  int getNumOctaves() const {
    return numOctaves;
  }

  bool isStitchTiles() const {
    return stitchTiles;
  }

  const PerlinNoiseShader::PaintingData* getPaintingData() const {
    return paintingData.get();
  }

  size_t onCountTextureSamplers() const override {
    return 2;
  }

  std::shared_ptr<Texture> onTextureAt(size_t index) const override;

  SamplerState onSamplerStateAt(size_t index) const override;

 protected:
  PerlinNoiseFragmentProcessor(PerlinNoiseType noiseType, int numOctaves, bool stitchTiles,
                               std::unique_ptr<PerlinNoiseShader::PaintingData> paintingData,
                               std::shared_ptr<TextureView> permutationsView,
                               std::shared_ptr<TextureView> noiseView, const Matrix* uvMatrix);

  PerlinNoiseType noiseType;
  int numOctaves;
  bool stitchTiles;
  std::unique_ptr<PerlinNoiseShader::PaintingData> paintingData;
  std::shared_ptr<TextureView> permutationsView;
  std::shared_ptr<TextureView> noiseView;
  CoordTransform coordTransform;
};
}  // namespace tgfx

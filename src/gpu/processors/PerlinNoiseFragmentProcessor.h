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
#include <vector>
#include "core/shaders/PerlinNoiseShader.h"
#include "gpu/AOTEffect.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {
class Context;
class Texture;
class TextureView;

class PerlinNoiseFragmentProcessor : public FragmentProcessor {
 public:
  // The precompiled PerlinNoiseFillShader carries three pointwise-operator slots (mirroring
  // PointwiseTailShader): the first is shared with an operator FP composed on top of this
  // processor, the rest are reserved for slot records uploaded by the processor itself.
  static constexpr size_t MaxPointwiseSlots = 3;

  static PlacementPtr<PerlinNoiseFragmentProcessor> Make(
      BlockAllocator* allocator, Context* context, PerlinNoiseType noiseType, int numOctaves,
      bool stitchTiles, std::unique_ptr<PerlinNoiseShader::PaintingData> paintingData,
      const Matrix* uvMatrix);

  // Rebuilds a PerlinNoiseFragmentProcessor from already-built lookup textures, skipping the
  // TextureView::MakeAlpha/MakeRGBA upload that Make() performs. Used by AOTPlanExecutor to
  // reconstruct the processor from a lowered AOTPerlinNoiseParameters, whose permutationsView and
  // noiseView were captured at lowerToAOT() time and must not be regenerated. slots holds up to
  // MaxPointwiseSlots pointwise operators applied after the noise, which the processor uploads
  // into the kernel's OpType/Slot1OpType uniform records; pass an empty vector for a bare noise
  // source.
  static PlacementPtr<PerlinNoiseFragmentProcessor> MakeFromViews(
      BlockAllocator* allocator, PerlinNoiseType noiseType, int numOctaves, bool stitchTiles,
      std::unique_ptr<PerlinNoiseShader::PaintingData> paintingData,
      std::shared_ptr<TextureView> permutationsView, std::shared_ptr<TextureView> noiseView,
      const Matrix* uvMatrix, const std::vector<AOTPointwiseSlot>& slots);

  std::string name() const override {
    return "PerlinNoiseFragmentProcessor";
  }

  size_t pointwiseSlotCount() const {
    return slotCount;
  }

  const AOTPointwiseSlot& pointwiseSlot(size_t index) const {
    return pointwiseSlots[index];
  }

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  size_t onCountTextureSamplers() const override {
    return 2;
  }

  std::shared_ptr<Texture> onTextureAt(size_t index) const override;

  SamplerState onSamplerStateAt(size_t index) const override;

  bool lowerToAOT(AOTNodeBuilder* builder, AOTNodeID input, AOTNodeID* output) const override;

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  PerlinNoiseFragmentProcessor(PerlinNoiseType noiseType, int numOctaves, bool stitchTiles,
                               std::unique_ptr<PerlinNoiseShader::PaintingData> paintingData,
                               std::shared_ptr<TextureView> permutationsView,
                               std::shared_ptr<TextureView> noiseView, const Matrix* uvMatrix,
                               const std::vector<AOTPointwiseSlot>& slots);

  PerlinNoiseType noiseType;
  int numOctaves;
  bool stitchTiles;
  std::unique_ptr<PerlinNoiseShader::PaintingData> paintingData;
  std::shared_ptr<TextureView> permutationsView;
  std::shared_ptr<TextureView> noiseView;
  CoordTransform coordTransform;
  size_t slotCount = 0;
  std::array<AOTPointwiseSlot, MaxPointwiseSlots> pointwiseSlots = {};
};
}  // namespace tgfx

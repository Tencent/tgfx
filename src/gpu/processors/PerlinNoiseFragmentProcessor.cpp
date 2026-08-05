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
#include "gpu/AOTEffect.h"
#include "gpu/resources/TextureView.h"

namespace tgfx {

PerlinNoiseFragmentProcessor::PerlinNoiseFragmentProcessor(
    PerlinNoiseType noiseType, int numOctaves, bool stitchTiles,
    std::unique_ptr<PerlinNoiseShader::PaintingData> paintingData,
    std::shared_ptr<TextureView> permutationsView, std::shared_ptr<TextureView> noiseView,
    const Matrix* uvMatrix, const std::vector<AOTPointwiseSlot>& slots)
    : FragmentProcessor(ClassID()), noiseType(noiseType), numOctaves(numOctaves),
      stitchTiles(stitchTiles), paintingData(std::move(paintingData)),
      permutationsView(std::move(permutationsView)), noiseView(std::move(noiseView)),
      slotCount(std::min(slots.size(), MaxPointwiseSlots)) {
  for (size_t index = 0; index < slotCount; ++index) {
    pointwiseSlots[index] = slots[index];
  }
  if (uvMatrix) {
    coordTransform = CoordTransform(*uvMatrix);
  }
  addCoordTransform(&coordTransform);
}

void PerlinNoiseFragmentProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  uint32_t key = static_cast<uint32_t>(numOctaves);
  key = key << 3;
  if (noiseType == PerlinNoiseType::FractalNoise) {
    key |= 0x1;
  } else {
    key |= 0x2;
  }
  if (stitchTiles) {
    key |= 0x4;
  }
  bytesKey->write(key);
  // Slot kinds are runtime uniforms in the precompiled kernel, but a runtime-built program would
  // bake them into code, so the key must separate them (mirroring AOTPointwiseTailProcessor).
  bytesKey->write(static_cast<uint32_t>(slotCount));
  for (size_t index = 0; index < slotCount; ++index) {
    bytesKey->write(static_cast<uint32_t>(pointwiseSlots[index].type));
    if (pointwiseSlots[index].type == AOTPointwiseOpType::ColorSpaceXform) {
      bytesKey->write(
          ColorSpaceXformSteps::XFormKey(pointwiseSlots[index].colorSpaceXform.steps.get()));
    }
  }
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
  return builder->addPerlinNoiseSource(input, parameters, output);
}
}  // namespace tgfx

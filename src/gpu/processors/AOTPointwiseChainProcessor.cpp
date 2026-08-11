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

#include "gpu/processors/AOTPointwiseChainProcessor.h"
#include "gpu/ColorSpaceXformHelper.h"

namespace tgfx {
namespace {
static void UploadChainSlot(UniformData* uniformData, size_t index, const AOTChainSlot& slot) {
  auto prefix = "Slot" + std::to_string(index);
  int selector = slot.op == AOTChainOp::Blend ? slot.blend.blendMode : 0;
  if (slot.op == AOTChainOp::ConstColor) {
    selector = slot.constColor.inputMode;
  }
  if (slot.op == AOTChainOp::Texture) {
    // Bit 0: modulate by the paint alpha. Bit 1: alpha-only leaf, splat .r into all channels.
    selector = slot.textureModulate | (slot.textureAlphaOnly << 1);
  }
  int packed[] = {static_cast<int>(slot.op), slot.in0, slot.in1, selector};
  uniformData->setDataOptional(prefix + "Packed", packed);
  switch (slot.op) {
    case AOTChainOp::ColorMatrix: {
      const auto& matrix = slot.colorMatrix.matrix;
      float matrixData[] = {
          matrix[0],  matrix[5],  matrix[10], matrix[15], matrix[1],  matrix[6],
          matrix[11], matrix[16], matrix[2],  matrix[7],  matrix[12], matrix[17],
          matrix[3],  matrix[8],  matrix[13], matrix[18],
      };
      float vectorData[] = {matrix[4], matrix[9], matrix[14], matrix[19]};
      uniformData->setDataOptional(prefix + "ColorMatrix", matrixData);
      uniformData->setDataOptional(prefix + "ColorVector", vectorData);
      break;
    }
    case AOTChainOp::Luma: {
      float lumaThresh[] = {slot.luma.kr, slot.luma.kg, slot.luma.kb, 0.0f};
      uniformData->setDataOptional(prefix + "LumaThresh", lumaThresh);
      break;
    }
    case AOTChainOp::AlphaThreshold: {
      float lumaThresh[] = {0.0f, 0.0f, 0.0f, slot.alphaThreshold.threshold};
      uniformData->setDataOptional(prefix + "LumaThresh", lumaThresh);
      break;
    }
    case AOTChainOp::ConstColor: {
      const auto& color = slot.constColor.color;
      float colorData[] = {color[0], color[1], color[2], color[3]};
      uniformData->setDataOptional(prefix + "ConstColor", colorData);
      break;
    }
    case AOTChainOp::AARectCoverage: {
      // The AA math evaluates to 0 at the rect coordinates, so outset by 0.5 to interpolate from 0
      // at a half-pixel inset to 1 at a half-pixel outset (same as GLSLAARectEffect::onSetData).
      const auto& rect = slot.rectCoverage.rect;
      float rectData[] = {rect[0] - 0.5f, rect[1] - 0.5f, rect[2] + 0.5f, rect[3] + 0.5f};
      uniformData->setDataOptional("CoverageRect", rectData);
      break;
    }
    default:
      break;
  }
}
}  // namespace

PlacementPtr<AOTPointwiseChainProcessor> AOTPointwiseChainProcessor::Make(
    BlockAllocator* allocator, std::vector<PlacementPtr<FragmentProcessor>> textureLeaves,
    const std::vector<AOTChainSlot>& slots, size_t rootSlot, int tiledLeafIndex,
    const AOTTiledTextureRecipe* tiledRecipe, PlacementPtr<FragmentProcessor> maskChild) {
  if (allocator == nullptr || slots.empty() || slots.size() > MaxSlots ||
      rootSlot >= slots.size()) {
    return nullptr;
  }
  auto leafCount = textureLeaves.size();
  if ((leafCount != 0 && leafCount != 1 && leafCount != 2 && leafCount != 4) ||
      leafCount > slots.size()) {
    return nullptr;
  }
  if (maskChild != nullptr && maskChild->name() != "DeviceSpaceTextureEffect") {
    return nullptr;
  }
  if (tiledLeafIndex >= 0 &&
      (static_cast<size_t>(tiledLeafIndex) >= leafCount || tiledRecipe == nullptr)) {
    return nullptr;
  }
  // Leaves pair slot-for-slot with samplers: slot k must be the leaf that samples TextureSampler_k.
  for (size_t index = 0; index < leafCount; ++index) {
    if (textureLeaves[index] == nullptr || slots[index].op != AOTChainOp::Texture) {
      return nullptr;
    }
  }
  for (const auto& slot : slots) {
    if (slot.op == AOTChainOp::None) {
      return nullptr;
    }
    if (slot.op == AOTChainOp::ColorSpaceXform && slot.colorSpaceXform.steps == nullptr) {
      return nullptr;
    }
    // -1 (the geometry color) is a legitimate blend operand; only an unmapped edge (-2) is invalid.
    if (slot.op == AOTChainOp::Blend && (slot.in0 == -2 || slot.in1 == -2)) {
      return nullptr;
    }
  }
  return allocator->make<AOTPointwiseChainProcessor>(
      std::move(textureLeaves), slots, rootSlot, tiledLeafIndex, tiledRecipe, std::move(maskChild));
}

AOTPointwiseChainProcessor::AOTPointwiseChainProcessor(
    std::vector<PlacementPtr<FragmentProcessor>> textureLeaves,
    const std::vector<AOTChainSlot>& newSlots, size_t rootSlot, int tiledLeafIndex,
    const AOTTiledTextureRecipe* tiledRecipe, PlacementPtr<FragmentProcessor> maskChildFP)
    : FragmentProcessor(ClassID()), _slotCount(newSlots.size()), rootSlot(rootSlot),
      tiledLeafIndex(tiledLeafIndex), hasMaskChild(maskChildFP != nullptr) {
  if (tiledRecipe != nullptr) {
    _tiledRecipe = *tiledRecipe;
  }
  for (size_t index = 0; index < newSlots.size(); ++index) {
    slots[index] = newSlots[index];
  }
  for (auto& leaf : textureLeaves) {
    registerChildProcessor(std::move(leaf));
  }
  if (maskChildFP != nullptr) {
    registerChildProcessor(std::move(maskChildFP));
  }
}

void AOTPointwiseChainProcessor::emitCode(EmitArgs& args) const {
  // This processor is only ever prepared with ProgramLookupMode::PrecompiledOnly through the
  // decomposition executor; a miss makes the executor re-run the original draw, so the runtime
  // program builder never sees it. Emitting the DAG for JIT would require porting the blend
  // emission of XfermodeFragmentProcessor; until a caller needs it, fail loudly instead of
  // silently producing wrong pixels.
  LOGE("AOTPointwiseChainProcessor::emitCode() is not supported on the runtime path!");
  args.fragBuilder->codeAppendf("%s = vec4(1.0, 0.0, 1.0, 1.0);", args.outputColor.c_str());
}

void AOTPointwiseChainProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  bytesKey->write(static_cast<uint32_t>(_slotCount));
  bytesKey->write(static_cast<uint32_t>(rootSlot));
  bytesKey->write(static_cast<uint32_t>(numChildProcessors()));
  bytesKey->write(static_cast<uint32_t>(hasMaskChild ? 1 : 0));
  for (size_t index = 0; index < _slotCount; ++index) {
    bytesKey->write(static_cast<uint32_t>(slots[index].op));
    if (slots[index].op == AOTChainOp::ColorSpaceXform) {
      bytesKey->write(ColorSpaceXformSteps::XFormKey(slots[index].colorSpaceXform.steps.get()));
    }
  }
}

void AOTPointwiseChainProcessor::onSetData(UniformData*, UniformData* fragmentUniformData) const {
  if (fragmentUniformData == nullptr) {
    return;
  }
  fragmentUniformData->setDataOptional("RootIndex", static_cast<int>(rootSlot));
  fragmentUniformData->setDataOptional("SlotCount", static_cast<int>(_slotCount));
  fragmentUniformData->setDataOptional("TiledLeafIndex", tiledLeafIndex);
  if (tiledLeafIndex >= 0) {
    int modeX = static_cast<int>(_tiledRecipe.shaderModeX);
    int modeY = static_cast<int>(_tiledRecipe.shaderModeY);
    fragmentUniformData->setDataOptional("TiledModeX", modeX);
    fragmentUniformData->setDataOptional("TiledModeY", modeY);
    float subsetRect[] = {_tiledRecipe.shaderSubset.left, _tiledRecipe.shaderSubset.top,
                          _tiledRecipe.shaderSubset.right, _tiledRecipe.shaderSubset.bottom};
    fragmentUniformData->setDataOptional("TiledSubset", subsetRect);
    float clampRect[] = {_tiledRecipe.shaderClamp.left, _tiledRecipe.shaderClamp.top,
                         _tiledRecipe.shaderClamp.right, _tiledRecipe.shaderClamp.bottom};
    fragmentUniformData->setDataOptional("TiledClamp", clampRect);
    if (_tiledRecipe.usesShaderDimensions) {
      fragmentUniformData->setDataOptional("TiledDimension", _tiledRecipe.shaderDimensions);
    }
    int strict = _tiledRecipe.strict ? 1 : 0;
    fragmentUniformData->setDataOptional("TiledStrict", strict);
  }
  for (size_t index = 0; index < MaxSlots; ++index) {
    UploadChainSlot(fragmentUniformData, index, slots[index]);
  }
  // The color-space parameters are one shared chain-wide block, so the kernel supports at most one
  // color-space op per chain (enforced by the matcher). The helper writes the same field names the
  // standalone ColorSpaceXformEffect uses, prefixed with "Chain".
  for (size_t index = 0; index < _slotCount; ++index) {
    if (slots[index].op == AOTChainOp::ColorSpaceXform) {
      ColorSpaceXformHelper("Chain").setData(fragmentUniformData,
                                             slots[index].colorSpaceXform.steps.get());
      break;
    }
  }
}

}  // namespace tgfx

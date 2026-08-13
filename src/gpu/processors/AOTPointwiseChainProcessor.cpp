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
  int selector = slot.op == AOTChainOp::Blend
                     ? slot.blend.blendMode | (slot.blend.multiplyInputAlpha ? 0x100 : 0)
                     : 0;
  if (slot.op == AOTChainOp::ConstColor) {
    selector = slot.constColor.inputMode;
  }
  if (slot.op == AOTChainOp::Texture) {
    // Bit 0: modulate by the paint alpha. Bit 1: alpha-only leaf, splat .r into all channels.
    // Bit 2: coverage-root leaf, modulate by the coverage unit alpha.
    selector =
        slot.textureModulate | (slot.textureAlphaOnly << 1) | (slot.textureModulateUnit << 2);
  }
  int packed[] = {static_cast<int>(slot.op), slot.in0, slot.in1, selector};
  uniformData->setArrayElementOptional("SlotPacked", index, packed);
  switch (slot.op) {
    case AOTChainOp::ColorMatrix: {
      const auto& matrix = slot.colorMatrix.matrix;
      float matrixData[] = {
          matrix[0],  matrix[5],  matrix[10], matrix[15], matrix[1],  matrix[6],
          matrix[11], matrix[16], matrix[2],  matrix[7],  matrix[12], matrix[17],
          matrix[3],  matrix[8],  matrix[13], matrix[18],
      };
      float vectorData[] = {matrix[4], matrix[9], matrix[14], matrix[19]};
      uniformData->setArrayElementOptional("SlotColorMatrix", index, matrixData);
      uniformData->setArrayElementOptional("SlotColorVector", index, vectorData);
      break;
    }
    case AOTChainOp::Luma: {
      float lumaThresh[] = {slot.luma.kr, slot.luma.kg, slot.luma.kb, 0.0f};
      uniformData->setArrayElementOptional("SlotLumaThresh", index, lumaThresh);
      break;
    }
    case AOTChainOp::AlphaThreshold: {
      float lumaThresh[] = {0.0f, 0.0f, 0.0f, slot.alphaThreshold.threshold};
      uniformData->setArrayElementOptional("SlotLumaThresh", index, lumaThresh);
      break;
    }
    case AOTChainOp::ConstColor: {
      const auto& color = slot.constColor.color;
      float colorData[] = {color[0], color[1], color[2], color[3]};
      uniformData->setArrayElementOptional("SlotConstColor", index, colorData);
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

// Writes the chain-wide gradient parameter block (UBO fields declared unconditionally in
// pointwise_chain.frag). Field names mirror the dedicated gradient kernels, prefixed with
// "Gradient"; the unrolled-binary arrays use the scale0_1-style names of gradient_fill.frag.
static void UploadGradientParams(UniformData* uniformData, const AOTGradientParameters& params) {
  uniformData->setDataOptional("GradientLayoutType", params.layoutType);
  uniformData->setDataOptional("GradientBias", params.bias);
  uniformData->setDataOptional("GradientScale", params.scale);
  uniformData->setDataOptional("GradientColorizerKind", params.colorizerKind);
  uniformData->setDataOptional("GradientLeftBorder", params.leftBorder);
  uniformData->setDataOptional("GradientRightBorder", params.rightBorder);
  uniformData->setDataOptional("GradientStart", params.start);
  uniformData->setDataOptional("GradientEnd", params.end);
  uniformData->setDataOptional("GradientScale01", params.scale01);
  uniformData->setDataOptional("GradientBias01", params.bias01);
  uniformData->setDataOptional("GradientScale23", params.scale23);
  uniformData->setDataOptional("GradientBias23", params.bias23);
  uniformData->setDataOptional("GradientThreshold", params.threshold);
  uniformData->setDataOptional("GradientIntervalCount", params.intervalCount);
  uniformData->setDataOptional("GradientThresholds1_7", params.thresholds1_7);
  uniformData->setDataOptional("GradientThresholds9_13", params.thresholds9_13);
  static const char* scaleNames[] = {
      "GradientScale0_1", "GradientScale2_3",   "GradientScale4_5",   "GradientScale6_7",
      "GradientScale8_9", "GradientScale10_11", "GradientScale12_13", "GradientScale14_15"};
  static const char* biasNames[] = {"GradientBias0_1",   "GradientBias2_3",  "GradientBias4_5",
                                    "GradientBias6_7",   "GradientBias8_9",  "GradientBias10_11",
                                    "GradientBias12_13", "GradientBias14_15"};
  for (size_t index = 0; index < 8; ++index) {
    uniformData->setDataOptional(scaleNames[index], params.scales[index]);
    uniformData->setDataOptional(biasNames[index], params.biases[index]);
  }
}
}  // namespace

PlacementPtr<AOTPointwiseChainProcessor> AOTPointwiseChainProcessor::Make(
    BlockAllocator* allocator, std::vector<PlacementPtr<FragmentProcessor>> textureLeaves,
    const std::vector<AOTChainSlot>& slots, size_t rootSlot, int tiledLeafIndex,
    const AOTTiledTextureRecipe* tiledRecipe, PlacementPtr<FragmentProcessor> maskChild,
    int coverageRootSlot, uint32_t coordSourceMask, PlacementPtr<FragmentProcessor> lutChild,
    int lutLeafIndex, std::vector<PlacementPtr<FragmentProcessor>> samplerPadding) {
  if (allocator == nullptr || slots.empty() || slots.size() > MaxSlots ||
      rootSlot >= slots.size()) {
    return nullptr;
  }
  if (coverageRootSlot >= 0 && static_cast<size_t>(coverageRootSlot) >= slots.size()) {
    return nullptr;
  }
  auto leafCount = textureLeaves.size();
  // Sampler-binding children include the DAG leaves plus sampler-only children (the LUT gradient
  // texture and phantom padding). TEXTURE_COUNT exists only as 0 or 4, so any chain with leaves
  // must pad exactly up to the four-sampler artifacts.
  size_t samplerChildren = leafCount + (lutChild != nullptr ? 1 : 0) + samplerPadding.size();
  if ((samplerChildren != 0 && samplerChildren != 4) || leafCount > slots.size()) {
    return nullptr;
  }
  if (maskChild != nullptr && maskChild->name() != "DeviceSpaceTextureEffect") {
    return nullptr;
  }
  // The LUT child binds the sampler right after the DAG leaves; the mask child's binding point
  // relative to it is undefined, so reject the (currently unreachable) combination.
  if (lutChild != nullptr && maskChild != nullptr) {
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
      std::move(textureLeaves), slots, rootSlot, tiledLeafIndex, tiledRecipe, std::move(maskChild),
      coverageRootSlot, coordSourceMask, std::move(lutChild), lutLeafIndex,
      std::move(samplerPadding));
}

AOTPointwiseChainProcessor::AOTPointwiseChainProcessor(
    std::vector<PlacementPtr<FragmentProcessor>> textureLeaves,
    const std::vector<AOTChainSlot>& newSlots, size_t rootSlot, int tiledLeafIndex,
    const AOTTiledTextureRecipe* tiledRecipe, PlacementPtr<FragmentProcessor> maskChildFP,
    int coverageRootSlot, uint32_t coordSourceMask, PlacementPtr<FragmentProcessor> lutChildFP,
    int lutLeafIndex, std::vector<PlacementPtr<FragmentProcessor>> samplerPadding)
    : FragmentProcessor(ClassID()), _slotCount(newSlots.size()), rootSlot(rootSlot),
      tiledLeafIndex(tiledLeafIndex), hasMaskChild(maskChildFP != nullptr),
      coverageRootSlot(coverageRootSlot), coordSourceMask(coordSourceMask),
      lutLeafIndex(lutLeafIndex) {
  if (tiledRecipe != nullptr) {
    _tiledRecipe = *tiledRecipe;
  }
  for (size_t index = 0; index < newSlots.size(); ++index) {
    slots[index] = newSlots[index];
    if (slots[index].op == AOTChainOp::Gradient) {
      gradientCoordTransform = CoordTransform(slots[index].gradient.coordMatrix);
    }
  }
  // Exposed unconditionally (a default identity transform when no gradient slot exists) so the
  // leaf transforms keep stable ordinals: the gradient transform is index 0, leaf k is index
  // k+1, and the GP-written CoordTransformMatrix_* uniforms line up the same way for every chain.
  addCoordTransform(&gradientCoordTransform);
  for (auto& leaf : textureLeaves) {
    registerChildProcessor(std::move(leaf));
  }
  if (lutChildFP != nullptr) {
    registerChildProcessor(std::move(lutChildFP));
  }
  for (auto& phantom : samplerPadding) {
    registerChildProcessor(std::move(phantom));
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
  bytesKey->write(static_cast<uint32_t>(coverageRootSlot + 1));
  bytesKey->write(coordSourceMask);
  bytesKey->write(static_cast<uint32_t>(lutLeafIndex + 1));
  for (size_t index = 0; index < _slotCount; ++index) {
    bytesKey->write(static_cast<uint32_t>(slots[index].op));
    if (slots[index].op == AOTChainOp::ColorSpaceXform) {
      bytesKey->write(ColorSpaceXformSteps::XFormKey(slots[index].colorSpaceXform.steps.get()));
    }
  }
}

void AOTPointwiseChainProcessor::onSetData(UniformData* vertexUniformData,
                                           UniformData* fragmentUniformData) const {
  if (vertexUniformData != nullptr) {
    // Absent in variants without a uvCoord attribute (the field is compiled out there).
    vertexUniformData->setDataOptional("CoordSourceMask", static_cast<int>(coordSourceMask));
  }
  if (fragmentUniformData == nullptr) {
    return;
  }
  fragmentUniformData->setDataOptional("RootIndex", static_cast<int>(rootSlot));
  fragmentUniformData->setDataOptional("CoverageRootIndex", coverageRootSlot);
  fragmentUniformData->setDataOptional("SlotCount", static_cast<int>(_slotCount));
  fragmentUniformData->setDataOptional("TiledLeafIndex", tiledLeafIndex);
  fragmentUniformData->setDataOptional("GradientLUTLeaf", lutLeafIndex);
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
  // The gradient parameters are one shared chain-wide block (at most one gradient slot per chain,
  // enforced by the builder). The vertex-side coordinate transform travels through the GP-written
  // CoordTransformMatrix_0 instead (see the constructor).
  for (size_t index = 0; index < _slotCount; ++index) {
    if (slots[index].op == AOTChainOp::Gradient) {
      UploadGradientParams(fragmentUniformData, slots[index].gradient);
      break;
    }
  }
}

}  // namespace tgfx

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
#include "gpu/Swizzle.h"
#include "gpu/resources/TextureView.h"

namespace tgfx {
namespace {
// std140 column packing of a 3x3 matrix stored in Matrix::get9 row-major order, mirroring
// UniformData::setData(const Matrix&) so chain uploads match the same layout.
static std::array<float, 12> MatrixColumnMajor(const std::array<float, 9>& values) {
  return {values[0], values[3], values[6], 0.0f,      values[1], values[4],
          values[7], 0.0f,      values[2], values[5], values[8], 0.0f};
}

// Uploads one tiled recipe into the kernel's Tiled<field><suffix> uniform set ("" for the first
// recipe, "2" for the second).
static void UploadTiledRecipe(UniformData* uniformData, const AOTTiledTextureRecipe& recipe,
                              const std::string& suffix) {
  uniformData->setDataOptional("TiledModeX" + suffix, static_cast<int>(recipe.shaderModeX));
  uniformData->setDataOptional("TiledModeY" + suffix, static_cast<int>(recipe.shaderModeY));
  float subsetRect[] = {recipe.shaderSubset.left, recipe.shaderSubset.top,
                        recipe.shaderSubset.right, recipe.shaderSubset.bottom};
  uniformData->setDataOptional("TiledSubset" + suffix, subsetRect);
  float clampRect[] = {recipe.shaderClamp.left, recipe.shaderClamp.top, recipe.shaderClamp.right,
                       recipe.shaderClamp.bottom};
  uniformData->setDataOptional("TiledClamp" + suffix, clampRect);
  if (recipe.usesShaderDimensions) {
    uniformData->setDataOptional("TiledDimension" + suffix, recipe.shaderDimensions);
  }
  uniformData->setDataOptional("TiledStrict" + suffix, recipe.strict ? 1 : 0);
}

static void UploadChainSlot(UniformData* uniformData, size_t index, const AOTChainSlot& slot,
                            int rrectOrdinal) {
  int selector = slot.op == AOTChainOp::Blend
                     ? slot.blend.blendMode | (slot.blend.multiplyInputAlpha ? 0x100 : 0)
                     : 0;
  if (slot.op == AOTChainOp::ConstColor) {
    selector = slot.constColor.inputMode;
  }
  if (slot.op == AOTChainOp::Texture) {
    // Bit 0: modulate by the paint alpha. Bit 1: alpha-only leaf, splat .r into all channels.
    // Bit 2: coverage-root leaf, modulate by the coverage unit alpha. Bit 3: alpha-only leaf
    // modulated by the geometry color's RGB (a two-child blend operand, whose input is
    // (geom.rgb, 1.0)). Bit 4: alpha-only leaf modulated by the full geometry color (the color
    // root, whose readback is sample.a * the complete premul input color).
    selector = slot.textureModulate | (slot.textureAlphaOnly << 1) |
               (slot.textureModulateUnit << 2) | (slot.textureModulateGeometryRGB << 3) |
               (slot.textureModulateFullInput << 4);
  }
  if (slot.op == AOTChainOp::TexModulate) {
    // Bit 0: the source leaf is alpha-only. Bits 16-19: the leading raw-sampling slot's index
    // into chainLeafTex (not Packed.z, which the register allocator rewrites).
    selector = slot.texModulateAlphaOnly | (slot.texModulateSourceSlot << 16);
  }
  if (slot.op == AOTChainOp::RRectCoverage) {
    // Bits 16-19: the slot's ordinal into the CoverageRRect* parameter arrays.
    selector = rrectOrdinal << 16;
  }
  // Bits 20-24: the result register + 1 (0 marks a dead instruction whose result nothing reads;
  // the kernel then skips the chainResults write).
  selector |= (slot.outRegister + 1) << 20;
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
      // at a half-pixel inset to 1 at a half-pixel outset (same as GLSLRectEffect::onSetData).
      const auto& rect = slot.rectCoverage.rect;
      float rectData[] = {rect[0] - 0.5f, rect[1] - 0.5f, rect[2] + 0.5f, rect[3] + 0.5f};
      uniformData->setDataOptional("CoverageRect", rectData);
      break;
    }
    case AOTChainOp::LocalRectCoverage: {
      // The local rect keeps its exact coordinates; the kernel's AA ramp clamps each edge distance
      // to half a pixel after mapping through the device-to-local matrix.
      const auto& params = slot.localRectCoverage;
      float rectData[] = {params.rect[0], params.rect[1], params.rect[2], params.rect[3]};
      uniformData->setDataOptional("CoverageLocalRect", rectData);
      uniformData->setDataOptional("CoverageLocalDeviceToLocal",
                                   MatrixColumnMajor(params.deviceToLocal));
      break;
    }
    case AOTChainOp::RRectCoverage: {
      const auto& params = slot.rrectCoverage;
      float rectData[] = {params.rect[0], params.rect[1], params.rect[2], params.rect[3]};
      uniformData->setArrayElementOptional("CoverageRRectRect", static_cast<size_t>(rrectOrdinal),
                                           rectData);
      uniformData->setArrayElementOptional("CoverageRRectRadiiX", static_cast<size_t>(rrectOrdinal),
                                           params.radiiX);
      uniformData->setArrayElementOptional("CoverageRRectRadiiY", static_cast<size_t>(rrectOrdinal),
                                           params.radiiY);
      float antiAliasData[] = {params.antiAlias};
      uniformData->setArrayElementOptional("CoverageRRectAntiAlias",
                                           static_cast<size_t>(rrectOrdinal), antiAliasData);
      uniformData->setArrayElementOptional("CoverageRRectDeviceToLocal",
                                           static_cast<size_t>(rrectOrdinal),
                                           MatrixColumnMajor(params.deviceToLocal));
      break;
    }
    default:
      break;
  }
}

// Writes the extended chain's gradient parameter block.
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

bool AOTPointwiseChainProcessor::ValidateSlots(const std::vector<AOTChainSlot>& slots,
                                               size_t rootSlot, int coverageRootSlot,
                                               int clipCoverageRegister, size_t leafCount,
                                               bool hasLUT,
                                               const std::vector<int>& tiledLeafIndices,
                                               AOTResourceProfile* profile) {
  if (profile == nullptr || slots.empty() || slots.size() > MaxSlots) {
    return false;
  }
  // Validation domains are separated (fail-closed): in0/in1 are INPUT selectors and
  // may carry the kernel's special negative designators (-1 geometry color ... -5 opaque
  // geometry color) or a result register; outRegister / rootSlot / coverageRootSlot /
  // clipCoverageRegister are OUTPUT registers and must name a real chainResults slot (>= 0) or
  // the explicit dead-instruction marker -1 (a raw sampling slot whose result nothing reads —
  // the computed-input texture's leading fetch). The upload packs the output as
  // (outRegister + 1) << 20, so any value below -1 would shift left into undefined behavior
  // instead of being rejected here.
  const auto validInput = [](int reg) { return reg >= -5 && reg < static_cast<int>(MaxRegisters); };
  const auto validOutput = [](int reg) {
    return reg >= -1 && reg < static_cast<int>(MaxRegisters);
  };
  if (rootSlot >= MaxRegisters || (coverageRootSlot >= 0 && !validOutput(coverageRootSlot)) ||
      clipCoverageRegister >= static_cast<int>(MaxRegisters) || leafCount > slots.size()) {
    return false;
  }
  for (const auto& slot : slots) {
    if (!validOutput(slot.outRegister) || !validInput(slot.in0) || !validInput(slot.in1)) {
      return false;
    }
    // Opcode whitelist: the kernel's evaluation switch has no default branch (an unknown value
    // falls through into the ColorSpaceXform evaluation), so an out-of-enum opcode must be
    // rejected here rather than silently misinterpreted.
    switch (slot.op) {
      case AOTChainOp::ColorMatrix:
      case AOTChainOp::Luma:
      case AOTChainOp::AlphaThreshold:
      case AOTChainOp::ColorSpaceXform:
      case AOTChainOp::Texture:
      case AOTChainOp::ConstColor:
      case AOTChainOp::Blend:
      case AOTChainOp::AARectCoverage:
      case AOTChainOp::Gradient:
      case AOTChainOp::LocalRectCoverage:
      case AOTChainOp::RRectCoverage:
      case AOTChainOp::TexModulate:
      case AOTChainOp::InputOpaque:
      case AOTChainOp::MulAlpha:
      case AOTChainOp::PerlinNoise:
        break;
      case AOTChainOp::None:
      default:
        return false;
    }
    // Selector sub-field domains: the upload packs these ints straight into the selector's bit
    // fields, so an out-of-domain value does not merely misbehave — it bleeds into neighboring
    // bits (the result register rides bits 20-24, the rrect/tex-source ordinals bits 16-19) and
    // can index chainResults/chainLeafTex out of bounds in the kernel. The owned API is public
    // surface, so fail closed here instead of trusting the builder.
    if (slot.op == AOTChainOp::ConstColor &&
        (slot.constColor.inputMode < 0 || slot.constColor.inputMode > 2)) {
      return false;
    }
    if (slot.op == AOTChainOp::Blend && (slot.blend.blendMode < 0 || slot.blend.blendMode > 0xFF ||
                                         slot.blend.childType < 0 || slot.blend.childType > 2)) {
      return false;
    }
    if (slot.op == AOTChainOp::Texture &&
        ((slot.textureModulate | slot.textureAlphaOnly | slot.textureModulateGeometryRGB |
          slot.textureModulateFullInput | slot.textureModulateUnit) &
         ~1) != 0) {
      return false;
    }
    if (slot.op == AOTChainOp::TexModulate &&
        (slot.texModulateSourceSlot < 0 || slot.texModulateSourceSlot > 3 ||
         (slot.texModulateAlphaOnly & ~1) != 0)) {
      return false;
    }
    // Required-input checks: the kernel resolves an unmapped input (-2) to vec4(0.0) silently, so
    // an operator that actually consumes its input must not leave it unmapped. Texture slots
    // carry flags in in0/in1 rather than register indices, and Texture is exempt.
    if (slot.op != AOTChainOp::Texture && slot.op != AOTChainOp::PerlinNoise && slot.in0 == -2) {
      return false;
    }
    if ((slot.op == AOTChainOp::Blend || slot.op == AOTChainOp::MulAlpha) && slot.in1 == -2) {
      return false;
    }
  }
  for (size_t index = 0; index < tiledLeafIndices.size(); ++index) {
    auto leafIndex = tiledLeafIndices[index];
    if (leafIndex < 0 || static_cast<size_t>(leafIndex) >= leafCount) {
      return false;
    }
    for (size_t previous = 0; previous < index; ++previous) {
      if (tiledLeafIndices[previous] == leafIndex) {
        return false;
      }
    }
  }
  // Only real DAG leaves have prefetched samples; LUT and padding children do not.
  for (size_t index = 0; index < slots.size(); ++index) {
    const auto& slot = slots[index];
    if ((slot.op == AOTChainOp::Texture) != (index < leafCount)) {
      return false;
    }
    if (slot.op == AOTChainOp::TexModulate &&
        static_cast<size_t>(slot.texModulateSourceSlot) >= leafCount) {
      return false;
    }
  }
  size_t localRectSlots = 0;
  size_t deviceRectSlots = 0;
  size_t colorSpaceSlots = 0;
  size_t gradientSlots = 0;
  size_t noiseSlots = 0;
  size_t rrectSlots = 0;
  for (const auto& slot : slots) {
    if (slot.op == AOTChainOp::ColorSpaceXform &&
        (!ColorSpaceXformStepsSupported(slot.colorSpaceXform.steps.get()) ||
         ++colorSpaceSlots > MaxColorSpaceXformSlots)) {
      return false;
    }
    if (slot.op == AOTChainOp::PerlinNoise &&
        (++noiseSlots > 1 || !AOTPerlinNoiseParametersSupported(slot.noise) ||
         slot.noise.permutationsView == nullptr || slot.noise.noiseView == nullptr ||
         leafCount > 2 || hasLUT)) {
      return false;
    }
    if (slot.op == AOTChainOp::Gradient && ++gradientSlots > MaxGradientSlots) {
      return false;
    }
    if (slot.op == AOTChainOp::AARectCoverage && ++deviceRectSlots > MaxDeviceRectCoverageSlots) {
      return false;
    }
    // The kernel carries one chain-wide local-rect parameter set and four rrect array elements.
    if (slot.op == AOTChainOp::LocalRectCoverage && ++localRectSlots > MaxLocalRectCoverageSlots) {
      return false;
    }
    if (slot.op == AOTChainOp::RRectCoverage && ++rrectSlots > MaxRRectCoverageSlots) {
      return false;
    }
  }
  if (noiseSlots > 0 && gradientSlots > 0) {
    return false;
  }
  auto resolved = *profile;
  if (resolved == AOTResourceProfile::Automatic) {
    resolved = noiseSlots > 0      ? AOTResourceProfile::NoiseGraph
               : gradientSlots > 0 ? AOTResourceProfile::TiledGraph
                                   : AOTResourceProfile::Chain;
  }
  AOTResourceDemand demand = {slots.size(),
                              0,
                              leafCount + (hasLUT ? 1u : 0u) + noiseSlots * 2,
                              tiledLeafIndices.size(),
                              noiseSlots,
                              gradientSlots,
                              colorSpaceSlots,
                              deviceRectSlots,
                              localRectSlots,
                              rrectSlots};
  if (!FitsAOTResourceProfile(demand, resolved)) {
    return false;
  }
  *profile = resolved;
  return true;
}

PlacementPtr<AOTPointwiseChainProcessor> AOTPointwiseChainProcessor::Make(
    BlockAllocator* allocator, std::vector<PlacementPtr<FragmentProcessor>> textureLeaves,
    const std::vector<AOTChainSlot>& slots, size_t rootSlot, int tiledLeafIndex,
    const AOTTiledTextureRecipe* tiledRecipe, PlacementPtr<FragmentProcessor> maskChild,
    int coverageRootSlot, uint32_t coordSourceMask, PlacementPtr<FragmentProcessor> lutChild,
    int lutLeafIndex, std::vector<PlacementPtr<FragmentProcessor>> samplerPadding,
    bool maskChildIsPhantom, int clipCoverageRegister, int tiledLeafIndex2,
    const AOTTiledTextureRecipe* tiledRecipe2, AOTResourceProfile profile,
    const std::vector<AOTChainTiledLeaf>& additionalTiledLeaves,
    const std::vector<AOTChainDeviceLeaf>& deviceLeaves) {
  if (allocator == nullptr) {
    return nullptr;
  }
  auto leafCount = textureLeaves.size();
  // The recipe pointers belong to the processor-facing signature; the leaf indices they pair
  // with are checked by ValidateSlots together with the additional recipes.
  if ((tiledLeafIndex >= 0 && tiledRecipe == nullptr) ||
      (tiledLeafIndex2 >= 0 && (tiledLeafIndex < 0 || tiledRecipe2 == nullptr))) {
    return nullptr;
  }
  std::vector<int> tiledLeafIndices = {};
  if (tiledLeafIndex >= 0) {
    tiledLeafIndices.push_back(tiledLeafIndex);
  }
  if (tiledLeafIndex2 >= 0) {
    tiledLeafIndices.push_back(tiledLeafIndex2);
  }
  for (const auto& tiled : additionalTiledLeaves) {
    tiledLeafIndices.push_back(tiled.leafIndex);
  }
  if (!ValidateSlots(slots, rootSlot, coverageRootSlot, clipCoverageRegister, leafCount,
                     lutChild != nullptr, tiledLeafIndices, &profile)) {
    return nullptr;
  }
  for (const auto& leaf : textureLeaves) {
    if (leaf == nullptr) {
      return nullptr;
    }
  }
  for (const auto& child : samplerPadding) {
    if (child == nullptr || child->name() != "TextureEffect" || child->numTextureSamplers() > 1) {
      return nullptr;
    }
  }
  // Sampler-binding children include the DAG leaves plus sampler-only children (the LUT gradient
  // texture and phantom padding). TEXTURE_COUNT exists only as 0 or 4, so any chain with leaves
  // must pad exactly up to the four-sampler artifacts.
  size_t samplerChildren = leafCount + (lutChild != nullptr ? 1 : 0) + samplerPadding.size();
  if (samplerChildren != 0 && samplerChildren != 4) {
    return nullptr;
  }
  if (maskChild != nullptr && !maskChildIsPhantom &&
      maskChild->name() != "DeviceSpaceTextureEffect") {
    return nullptr;
  }
  if (lutChild != nullptr &&
      (lutLeafIndex < 0 || lutLeafIndex > 3 || static_cast<size_t>(lutLeafIndex) != leafCount)) {
    return nullptr;
  }
  for (const auto& slot : slots) {
    if (slot.op != AOTChainOp::PerlinNoise) {
      continue;
    }
    // The two noise lookup tables ride the first two sampler-only children.
    if (samplerPadding.size() < 2 || samplerPadding[0] == nullptr || samplerPadding[1] == nullptr ||
        samplerPadding[0]->textureAt(0) != slot.noise.permutationsView->getTexture() ||
        samplerPadding[1]->textureAt(0) != slot.noise.noiseView->getTexture()) {
      return nullptr;
    }
  }
  // A device-space leaf is one of the DAG leaves (a texture slot), never a sampler-only child, and
  // each leaf takes at most one device-space matrix.
  uint32_t seenDeviceLeaves = 0;
  for (const auto& deviceLeaf : deviceLeaves) {
    if (deviceLeaf.leafIndex < 0 || static_cast<size_t>(deviceLeaf.leafIndex) >= leafCount ||
        deviceLeaf.textureProxy == nullptr || deviceLeaf.uvMatrix.hasPerspective()) {
      return nullptr;
    }
    auto bit = 1u << static_cast<uint32_t>(deviceLeaf.leafIndex);
    if ((seenDeviceLeaves & bit) != 0) {
      return nullptr;
    }
    seenDeviceLeaves |= bit;
  }
  return allocator->make<AOTPointwiseChainProcessor>(
      std::move(textureLeaves), slots, rootSlot, tiledLeafIndex, tiledRecipe, std::move(maskChild),
      coverageRootSlot, coordSourceMask, std::move(lutChild), lutLeafIndex,
      std::move(samplerPadding), maskChildIsPhantom, clipCoverageRegister, tiledLeafIndex2,
      tiledRecipe2, profile, additionalTiledLeaves, deviceLeaves);
}

AOTPointwiseChainProcessor::AOTPointwiseChainProcessor(
    std::vector<PlacementPtr<FragmentProcessor>> textureLeaves,
    const std::vector<AOTChainSlot>& newSlots, size_t rootSlot, int tiledLeafIndex,
    const AOTTiledTextureRecipe* tiledRecipe, PlacementPtr<FragmentProcessor> maskChildFP,
    int coverageRootSlot, uint32_t coordSourceMask, PlacementPtr<FragmentProcessor> lutChildFP,
    int lutLeafIndex, std::vector<PlacementPtr<FragmentProcessor>> samplerPadding,
    bool maskChildIsPhantom, int clipCoverageRegister, int tiledLeafIndex2,
    const AOTTiledTextureRecipe* tiledRecipe2, AOTResourceProfile profile,
    const std::vector<AOTChainTiledLeaf>& additionalTiledLeaves,
    const std::vector<AOTChainDeviceLeaf>& deviceLeaves)
    : FragmentProcessor(ClassID()), _slotCount(newSlots.size()), rootSlot(rootSlot),
      _resourceProfile(profile), hasMaskSlotChild(maskChildFP != nullptr),
      hasMaskChild(maskChildFP != nullptr && !maskChildIsPhantom),
      coverageRootSlot(coverageRootSlot), clipCoverageRegister(clipCoverageRegister),
      coordSourceMask(coordSourceMask), lutLeafIndex(lutLeafIndex), deviceLeaves(deviceLeaves) {
  if (tiledLeafIndex >= 0 && tiledRecipe != nullptr) {
    tiledLeaves.push_back({tiledLeafIndex, *tiledRecipe});
  }
  if (tiledLeafIndex2 >= 0 && tiledRecipe2 != nullptr) {
    tiledLeaves.push_back({tiledLeafIndex2, *tiledRecipe2});
  }
  tiledLeaves.insert(tiledLeaves.end(), additionalTiledLeaves.begin(), additionalTiledLeaves.end());
  for (size_t index = 0; index < newSlots.size(); ++index) {
    slots[index] = newSlots[index];
    if (slots[index].op == AOTChainOp::Gradient) {
      gradientCoordTransform = CoordTransform(slots[index].gradient.coordMatrix);
    } else if (slots[index].op == AOTChainOp::PerlinNoise) {
      gradientCoordTransform = CoordTransform(slots[index].noise.uvMatrix);
      noiseSamplerBase = static_cast<int>(textureLeaves.size());
    }
  }
  if (IsAOTGraphProfile(_resourceProfile)) {
    addCoordTransform(&gradientCoordTransform);
  }
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

uint32_t AOTPointwiseChainProcessor::deviceLeafMask() const {
  uint32_t mask = 0;
  for (const auto& deviceLeaf : deviceLeaves) {
    mask |= 1u << static_cast<uint32_t>(deviceLeaf.leafIndex);
  }
  return mask;
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
  // The chain's whole instruction structure — slot count, wiring, opcodes, root, coverage root,
  // coord-source mask, LUT leaf — travels in per-draw uniforms that onSetData rewrites before
  // every draw, and the base class already keys this processor by class ID plus the children's
  // keys (texture identities, sampler flags), so the variant selection is preserved. Two chains
  // sharing a variant and a texture set therefore share one program and the per-draw data
  // differentiates them; encoding the instruction structure here would split the program cache
  // by effect topology and defeat the variant reuse that is the point of the chain kernel.
  static constexpr uint32_t ChainProgramTag = 0xA07C41A7u;
  bytesKey->write(ChainProgramTag);
  bytesKey->write(static_cast<uint32_t>(_resourceProfile));
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
  // The mask sampler slot is always bound in four-leaf variants; this flag selects the
  // runtime application. Absent in leaf-free variants (compile-time mask there).
  fragmentUniformData->setDataOptional("HasMaskTexture", hasMaskChild ? 1 : 0);
  fragmentUniformData->setDataOptional("RootIndex", static_cast<int>(rootSlot));
  fragmentUniformData->setDataOptional("CoverageRootIndex", coverageRootSlot);
  fragmentUniformData->setDataOptional("ClipCoverageRegister", clipCoverageRegister);
  fragmentUniformData->setDataOptional("SlotCount", static_cast<int>(_slotCount));
  // Written for every draw (absent in leaf-free variants): a stale mask from an earlier draw
  // would make an ordinary leaf sample in device space.
  fragmentUniformData->setDataOptional("DeviceLeafMask", static_cast<int>(deviceLeafMask()));
  for (const auto& deviceLeaf : deviceLeaves) {
    // Same mapping as GLSLDeviceSpaceTextureEffect::onSetData: destination fragment coordinates
    // to the texture's sampling space.
    auto textureView = deviceLeaf.textureProxy->getTextureView();
    if (textureView == nullptr) {
      continue;
    }
    auto deviceCoordMatrix = deviceLeaf.uvMatrix;
    auto scale = textureView->getTextureCoord(1, 1);
    deviceCoordMatrix.postScale(scale.x, scale.y);
    std::array<float, 9> values = {
        deviceCoordMatrix[0], deviceCoordMatrix[1], deviceCoordMatrix[2],
        deviceCoordMatrix[3], deviceCoordMatrix[4], deviceCoordMatrix[5],
        deviceCoordMatrix[6], deviceCoordMatrix[7], deviceCoordMatrix[8]};
    fragmentUniformData->setArrayElementOptional("DeviceLeafCoordMatrix",
                                                 static_cast<size_t>(deviceLeaf.leafIndex),
                                                 MatrixColumnMajor(values));
  }
  if (leafCount() > 0) {
    std::array<int, 4> readSwizzles = {};
    for (size_t index = 0; index < leafCount() && index < _slotCount; ++index) {
      if (slots[index].op == AOTChainOp::Texture) {
        auto texture = childProcessor(index)->textureAt(0);
        if (texture != nullptr) {
          readSwizzles[index] = AOTReadSwizzleMode(texture->format());
        }
      }
    }
    fragmentUniformData->setData("LeafReadSwizzles", readSwizzles);
  }
  // Each selector names the leaf that reads the matching recipe set; the kernel never reads a set
  // whose selector is -1, so an absent recipe needs no upload.
  fragmentUniformData->setDataOptional("GradientLUTLeaf", lutLeafIndex);
  if (_resourceProfile == AOTResourceProfile::TiledGraph) {
    for (size_t index = 0; index < TiledGraphResourceCapacity.tiledRecipes; ++index) {
      int modes[] = {-1, 0, 0, 0};
      if (index < tiledLeaves.size()) {
        const auto& tiled = tiledLeaves[index];
        modes[0] = tiled.leafIndex;
        modes[1] = static_cast<int>(tiled.recipe.shaderModeX);
        modes[2] = static_cast<int>(tiled.recipe.shaderModeY);
        modes[3] = tiled.recipe.strict ? 1 : 0;
        const auto& subset = tiled.recipe.shaderSubset;
        const auto& clamp = tiled.recipe.shaderClamp;
        float subsetData[] = {subset.left, subset.top, subset.right, subset.bottom};
        float clampData[] = {clamp.left, clamp.top, clamp.right, clamp.bottom};
        fragmentUniformData->setArrayElement("TiledRecipeSubsets", index, subsetData);
        fragmentUniformData->setArrayElement("TiledRecipeClamps", index, clampData);
        fragmentUniformData->setArrayElement("TiledRecipeDimensions", index,
                                             tiled.recipe.shaderDimensions);
      }
      fragmentUniformData->setArrayElement("TiledRecipeModes", index, modes);
    }
  } else {
    fragmentUniformData->setDataOptional("TiledLeafIndex", tiledLeaf());
    fragmentUniformData->setDataOptional("TiledLeafIndex2", tiledLeaf2());
    for (size_t index = 0; index < tiledLeaves.size(); ++index) {
      UploadTiledRecipe(fragmentUniformData, tiledLeaves[index].recipe, index == 0 ? "" : "2");
    }
  }
  // RRect coverage slots consume the CoverageRRect* arrays in slot order; the upload assigns the
  // ordinals the same way, so the selector's bits 16-19 match the array element written here.
  // Fill all declared slots (including inactive ones, whose OP_NONE marker protects the static
  // texture prefetch), but never probe the 16 absent indices of a base-chain shader.
  int rrectOrdinal = 0;
  const auto* capacity = GetAOTResourceCapacity(_resourceProfile);
  for (size_t index = 0; index < capacity->instructions; ++index) {
    UploadChainSlot(fragmentUniformData, index, slots[index], rrectOrdinal);
    if (slots[index].op == AOTChainOp::RRectCoverage) {
      ++rrectOrdinal;
    }
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
    if (slots[index].op == AOTChainOp::PerlinNoise) {
      const auto& noise = slots[index].noise;
      float frequencyStitch[] = {noise.baseFrequencyX, noise.baseFrequencyY,
                                 static_cast<float>(noise.stitchWidth),
                                 static_cast<float>(noise.stitchHeight)};
      int modeOctaves[] = {noise.noiseType, noise.numOctaves, noise.stitchTiles ? 1 : 0,
                           noiseSamplerBase};
      fragmentUniformData->setDataOptional("NoiseFrequencyStitch", frequencyStitch);
      fragmentUniformData->setDataOptional("NoiseModeOctaves", modeOctaves);
      break;
    }
  }
}

}  // namespace tgfx

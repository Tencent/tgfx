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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/bind/Binder.h"
#include <array>
#include "gpu/ColorSpaceXformHelper.h"
#include "gpu/Swizzle.h"
#include "gpu/YUVColorConversion.h"
#include "gpu/aot/ir/ChainLayoutMath.h"
#include "gpu/aot/ir/NodeContract.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/TextureView.h"
#include "gpu/resources/YUVTextureView.h"
#include "gpu/shaders/KernelContract.h"
#include "gpu/shaders/contract/ChainOps.h"
#include "gpu/shaders/modules/AnalyticClipModule.h"
#include "gpu/shaders/modules/BlendParams.h"
#include "gpu/shaders/modules/BlurKernelModule.h"
#include "gpu/shaders/modules/ChainParams.h"
#include "gpu/shaders/modules/DeviceMaskModule.h"
#include "gpu/shaders/modules/GradientModule.h"
#include "gpu/shaders/modules/ParamWriter.h"
#include "gpu/shaders/modules/PorterDuffXPModule.h"
#include "gpu/shaders/modules/TextureSourceModule.h"
#include "gpu/shaders/modules/TiledSourceModule.h"

namespace tgfx {
namespace {

/// One chain instruction's per-slot parameter write. The packed word and its selector stay with
/// the caller's dialect (the chain encodes result registers, the YUV tail encodes the op's small
/// value), so the table carries only the parameter arrays: `index` is the slot's index (the
/// per-op arrays), `ordinal` is its ordinal into a per-kind array (the coverage capacities; the
/// slot index for every other op). A null row means the instruction writes no parameter block.
using AOTOpBinder = void (*)(UniformData* uniformData, size_t index, int ordinal,
                             const AOTChainSlot& slot);

/// One row per AOTChainOp, in the op ABI value order (Op::* in KernelContract.h): the
/// instruction's uniform write. A separate table on purpose — the instruction vocabulary and the
/// node vocabulary are not one-to-one (see ChainOps.h and NodeContract.h).
struct AOTOpContract {
  AOTOpBinder bind = nullptr;
};

constexpr AOTOpContract OpContracts[] = {
    {BindColorMatrixSlot},        // ColorMatrix (0)
    {BindLumaSlot},               // Luma (1)
    {BindAlphaThresholdSlot},     // AlphaThreshold (2)
    {},                           // ColorSpaceXform (3): rides the program-level Chain block
    {},                           // None (4, the inactive-slot marker)
    {},                           // Texture (5): the sampler binding carries its records
    {BindConstColorSlot},         // ConstColor (6)
    {},                           // Blend (7): the chain's operands are registers
    {BindAARectCoverageSlot},     // AARectCoverage (8)
    {},                           // Gradient (9): the program-level gradient block
    {BindLocalRectCoverageSlot},  // LocalRectCoverage (10)
    {BindRRectCoverageSlot},      // RRectCoverage (11)
    {},                           // TexModulate (12, synthesized by the texture encoder)
    {},                           // InputOpaque (13)
    {},                           // MulAlpha (14)
    {},                           // PerlinNoise (15): the program-level noise block
};

constexpr size_t OpContractCount = sizeof(OpContracts) / sizeof(AOTOpContract);
static_assert(OpContractCount == ChainISA::OpCount,
              "every AOTChainOp needs exactly one contract row in OpContracts");

const AOTOpContract& OpContractOf(AOTChainOp op) {
  static constexpr AOTOpContract fallback = {};
  auto index = static_cast<size_t>(op);
  return index < OpContractCount ? OpContracts[index] : fallback;
}

// Uploads one tiled recipe into the kernel's Tiled<field><suffix> uniform set ("" for the first
// recipe, "2" for the second).
void UploadTiledRecipe(UniformData* uniformData, const AOTTiledTextureRecipe& recipe,
                       const ChainParams::TiledRecipeNames& names) {
  uniformData->setDataOptional(names.modeX, static_cast<int>(recipe.shaderModeX));
  uniformData->setDataOptional(names.modeY, static_cast<int>(recipe.shaderModeY));
  float subsetRect[] = {recipe.shaderSubset.left, recipe.shaderSubset.top,
                        recipe.shaderSubset.right, recipe.shaderSubset.bottom};
  uniformData->setDataOptional(names.subset, subsetRect);
  float clampRect[] = {recipe.shaderClamp.left, recipe.shaderClamp.top, recipe.shaderClamp.right,
                       recipe.shaderClamp.bottom};
  uniformData->setDataOptional(names.clamp, clampRect);
  if (recipe.usesShaderDimensions) {
    uniformData->setDataOptional(names.dimension, recipe.shaderDimensions);
  }
  uniformData->setDataOptional(names.strict, recipe.strict ? 1 : 0);
}

void UploadChainSlot(UniformData* uniformData, size_t index, const AOTChainSlot& slot) {
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
  // Bits 20-24: the result register + 1 (0 marks a dead instruction whose result nothing reads;
  // the kernel then skips the chainResults write).
  selector |= (slot.outRegister + 1) << 20;
  int packed[] = {static_cast<int>(slot.op), slot.in0, slot.in1, selector};
  uniformData->setArrayElementOptional(ChainParams::SlotPacked.name, index, packed);
  // The parameter block is the instruction contract's bind column: one function per op that
  // writes one, a null row for the instructions that carry none (they ride the packed word).
  if (auto bind = OpContractOf(slot.op).bind) {
    bind(uniformData, index, static_cast<int>(index), slot);
  }
}

// Writes one operator's parameter block into the slot's data-pool region. The block layout and
// the packer both come from contract/ChainOps.h, so a field cannot be written at one offset and
// read at another.
template <typename Data>
void UploadSlotData(UniformData* uniformData, size_t index, const Data& data,
                    void (*pack)(const Data&, float*)) {
  float words[ChainISA::PoolSlotWords * 4] = {};
  pack(data, words);
  const size_t base = index * static_cast<size_t>(ChainISA::PoolSlotWords);
  for (size_t word = 0; word < static_cast<size_t>(ChainISA::PoolSlotWords); ++word) {
    const std::array<float, 4> word4 = {words[word * 4], words[word * 4 + 1], words[word * 4 + 2],
                                        words[word * 4 + 3]};
    uniformData->setArrayElementOptional(ChainParams::ChainData.name, base + word, word4);
  }
}

ChainISA::Float4 ToFloat4(const std::array<float, 4>& values) {
  return {values[0], values[1], values[2], values[3]};
}

// Writes the chain's gradient parameter block into the pool's chain-wide region. The three
// selectors stay runtime uniforms until the structural fields move to specialization constants.
void UploadGradientParams(UniformData* uniformData, size_t blockBase,
                          const AOTGradientParameters& params) {
  uniformData->setDataOptional(ChainParams::GradientLayoutType.name, params.layoutType);
  uniformData->setDataOptional(ChainParams::GradientColorizerKind.name, params.colorizerKind);
  uniformData->setDataOptional(ChainParams::GradientIntervalCount.name, params.intervalCount);
  ChainISA::GradientData data = {};
  data.leftBorder = ToFloat4(params.leftBorder);
  data.rightBorder = ToFloat4(params.rightBorder);
  data.start = ToFloat4(params.start);
  data.end = ToFloat4(params.end);
  data.scale01 = ToFloat4(params.scale01);
  data.bias01 = ToFloat4(params.bias01);
  data.scale23 = ToFloat4(params.scale23);
  data.bias23 = ToFloat4(params.bias23);
  data.threshold = params.threshold;
  data.bias = params.bias;
  data.scale = params.scale;
  data.thresholds1_7 = ToFloat4(params.thresholds1_7);
  data.thresholds9_13 = ToFloat4(params.thresholds9_13);
  for (size_t index = 0; index < 8; ++index) {
    data.scales.v[index] = ToFloat4(params.scales[index]);
    data.biases.v[index] = ToFloat4(params.biases[index]);
  }
  float words[ChainISA::GradientDataWords * 4] = {};
  ChainISA::PackGradient(data, words);
  for (size_t word = 0; word < static_cast<size_t>(ChainISA::GradientDataWords); ++word) {
    const std::array<float, 4> word4 = {words[word * 4], words[word * 4 + 1], words[word * 4 + 2],
                                        words[word * 4 + 3]};
    uniformData->setArrayElementOptional(ChainParams::ChainData.name, blockBase + word, word4);
  }
}

}  // namespace

void UploadPointwiseSlot(UniformData* uniformData, const AOTChainSlot& slot) {
  // One pointwise operator as chain slot 0: the packed word and the parameter registers the slot
  // interpreter reads (pointwise_op.inc). A single-operator kernel (TexturedEffect) and the chain
  // thus share one parameter ABI. The single-operator kernels accept the four color operators
  // only; a Blend's operand encoding belongs to UploadYUVTailSlot, whose selector bit 8 carries
  // the operand order where the chain's carries multiplyInputAlpha.
  UploadChainSlot(uniformData, 0, slot);
}

// The instruction contract's bind column (declared in Binder.h): each function writes one op's
// per-slot parameter block into the chain arrays.
void BindColorMatrixSlot(UniformData* uniformData, size_t index, int /*ordinal*/,
                         const AOTChainSlot& slot) {
  ChainISA::ColorMatrixData data = {};
  float translate[4] = {};
  PackAOTColorMatrix(slot.colorMatrix.matrix, data.linear.m, translate);
  data.translate = {translate[0], translate[1], translate[2], translate[3]};
  UploadSlotData(uniformData, index, data, ChainISA::PackColorMatrix);
}

void BindLumaSlot(UniformData* uniformData, size_t index, int /*ordinal*/,
                  const AOTChainSlot& slot) {
  UploadSlotData(uniformData, index, slot.luma, ChainISA::PackLuma);
}

void BindAlphaThresholdSlot(UniformData* uniformData, size_t index, int /*ordinal*/,
                            const AOTChainSlot& slot) {
  UploadSlotData(uniformData, index, slot.alphaThreshold, ChainISA::PackAlphaThreshold);
}

void BindConstColorSlot(UniformData* uniformData, size_t index, int /*ordinal*/,
                        const AOTChainSlot& slot) {
  const auto& color = slot.constColor.color;
  ChainISA::ConstColorData data = {};
  data.color = {color[0], color[1], color[2], color[3]};
  UploadSlotData(uniformData, index, data, ChainISA::PackConstColor);
}

void BindAARectCoverageSlot(UniformData* uniformData, size_t index, int /*ordinal*/,
                            const AOTChainSlot& slot) {
  // The AA math evaluates to 0 at the rect coordinates, so outset by 0.5 to interpolate from 0
  // at a half-pixel inset to 1 at a half-pixel outset (same as GLSLRectEffect::onSetData).
  const auto& rect = slot.rectCoverage.rect;
  ChainISA::AARectCoverageData data = {};
  data.rect = {rect[0] - 0.5f, rect[1] - 0.5f, rect[2] + 0.5f, rect[3] + 0.5f};
  UploadSlotData(uniformData, index, data, ChainISA::PackAARectCoverage);
}

void BindLocalRectCoverageSlot(UniformData* uniformData, size_t index, int /*ordinal*/,
                               const AOTChainSlot& slot) {
  // The local rect keeps its exact coordinates; the kernel's AA ramp clamps each edge distance to
  // half a pixel after mapping through the device-to-local matrix.
  const auto& params = slot.localRectCoverage;
  ChainISA::LocalRectCoverageData data = {};
  data.rect = {params.rect[0], params.rect[1], params.rect[2], params.rect[3]};
  const auto columns = Mat3ToStd140ColumnMajor(params.deviceToLocal);
  for (size_t column = 0; column < 3; ++column) {
    for (size_t row = 0; row < 3; ++row) {
      data.deviceToLocal.m[column * 3 + row] = columns[column * 4 + row];
    }
  }
  UploadSlotData(uniformData, index, data, ChainISA::PackLocalRectCoverage);
}

void BindRRectCoverageSlot(UniformData* uniformData, size_t index, int /*ordinal*/,
                           const AOTChainSlot& slot) {
  const auto& params = slot.rrectCoverage;
  ChainISA::RRectCoverageData data = {};
  data.rect = {params.rect[0], params.rect[1], params.rect[2], params.rect[3]};
  data.radiiX = {params.radiiX[0], params.radiiX[1], params.radiiX[2], params.radiiX[3]};
  data.radiiY = {params.radiiY[0], params.radiiY[1], params.radiiY[2], params.radiiY[3]};
  const auto columns = Mat3ToStd140ColumnMajor(params.deviceToLocal);
  for (size_t column = 0; column < 3; ++column) {
    for (size_t row = 0; row < 3; ++row) {
      data.deviceToLocal.m[column * 3 + row] = columns[column * 4 + row];
    }
  }
  data.antiAlias = params.antiAlias;
  UploadSlotData(uniformData, index, data, ChainISA::PackRRectCoverage);
}

// One YUV-tail instruction's records (see BindYUVProgram): the chain's uniform arrays with the
// linear-tail selector — the kernel's macro binding reads exactly these bits.
void UploadYUVTailSlot(UniformData* uniformData, size_t index, const AOTChainSlot& slot) {
  int selector = 0;
  if (slot.op == AOTChainOp::ConstColor) {
    selector = slot.constColor.inputMode;
  } else if (slot.op == AOTChainOp::Blend) {
    selector = slot.blend.blendMode | (slot.blend.childType == 1 ? 0x100 : 0);
  }
  int packed[] = {static_cast<int>(slot.op), -2, -2, selector};
  uniformData->setArrayElementOptional(ChainParams::SlotPacked.name, index, packed);
  // The parameter block comes from the instruction contract — except the linear tail's Blend,
  // whose operand is the const color (the chain's blend reads registers, so its row writes
  // nothing; the tail's dialect supplies the color instead).
  if (slot.op == AOTChainOp::Blend) {
    // The tail's blend operand is the const color, packed into the Blend block's color field.
    const auto& color = slot.constColor.color;
    ChainISA::BlendData data = {};
    data.color = {color[0], color[1], color[2], color[3]};
    UploadSlotData(uniformData, index, data, ChainISA::PackBlend);
  } else if (auto bind = OpContractOf(slot.op).bind) {
    bind(uniformData, index, static_cast<int>(index), slot);
  }
}

void BindYUVProgram(const AOTProgram& program, UniformData* fragmentUniformData) {
  if (fragmentUniformData == nullptr) {
    return;
  }
  const auto& slots = program.slots;
  fragmentUniformData->setDataOptional(ChainParams::SlotCount.name, static_cast<int>(slots.size()));
  // The declared capacity exceeds the real count: the inactive tail carries the OP_NONE marker,
  // matching the fixed-size storage every multi-slot kernel keeps.
  AOTChainSlot inactiveSlot = {};
  for (size_t index = 0; index < MaxPointwiseSlots; ++index) {
    UploadYUVTailSlot(fragmentUniformData, index,
                      index < slots.size() ? slots[index] : inactiveSlot);
  }
  // The color-space parameters are one shared block (at most one transform per tail, enforced
  // by the matching walk): the helper writes the same Chain-prefixed fields the interpreter's
  // binder writes.
  for (const auto& slot : slots) {
    if (slot.op == AOTChainOp::ColorSpaceXform) {
      ColorSpaceXformHelper("Chain").setData(fragmentUniformData, slot.colorSpaceXform.steps.get());
      break;
    }
  }
}

void BindChainProgram(const AOTProgram& program, UniformData* vertexUniformData,
                      UniformData* fragmentUniformData) {
  const auto& slots = program.slots;
  const auto slotCount = slots.size();
  if (vertexUniformData != nullptr) {
    // Absent in variants without a uvCoord attribute (the field is compiled out there).
    vertexUniformData->setDataOptional(ChainParams::CoordSourceMask.name,
                                       static_cast<int>(program.coordSourceMask));
  }
  if (fragmentUniformData == nullptr) {
    return;
  }
  // The mask sampler slot is always bound in four-leaf variants; this flag selects the
  // runtime application. Absent in leaf-free variants (compile-time mask there).
  fragmentUniformData->setDataOptional(ChainParams::HasMaskTexture.name,
                                       program.hasMaskTexture ? 1 : 0);
  fragmentUniformData->setDataOptional(ChainParams::RootIndex.name,
                                       static_cast<int>(program.rootRegister));
  fragmentUniformData->setDataOptional(ChainParams::CoverageRootIndex.name,
                                       program.coverageRootRegister);
  fragmentUniformData->setDataOptional(ChainParams::ClipCoverageRegister.name,
                                       program.clipCoverageRegister);
  fragmentUniformData->setDataOptional(ChainParams::SlotCount.name, static_cast<int>(slotCount));
  // Written for every draw (absent in leaf-free variants): a stale mask from an earlier draw
  // would make an ordinary leaf sample in device space.
  uint32_t deviceMask = 0;
  for (const auto& deviceLeaf : program.deviceLeaves) {
    deviceMask |= 1u << static_cast<uint32_t>(deviceLeaf.leafIndex);
  }
  fragmentUniformData->setDataOptional(ChainParams::DeviceLeafMask.name,
                                       static_cast<int>(deviceMask));
  for (const auto& deviceLeaf : program.deviceLeaves) {
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
    fragmentUniformData->setArrayElementOptional(ChainParams::DeviceLeafCoordMatrix.name,
                                                 static_cast<size_t>(deviceLeaf.leafIndex),
                                                 Mat3ToStd140ColumnMajor(values));
  }
  bool hasLeaf = false;
  for (const auto& binding : program.bindings) {
    hasLeaf |= binding.kind == AOTBinding::Kind::Leaf;
  }
  if (hasLeaf) {
    std::array<int, 4> readSwizzles = {};
    for (size_t index = 0; index < program.bindings.size() && index < slotCount; ++index) {
      if (slots[index].op == AOTChainOp::Texture &&
          program.bindings[index].kind == AOTBinding::Kind::Leaf) {
        auto textureView = program.bindings[index].textureProxy->getTextureView();
        if (textureView != nullptr) {
          readSwizzles[index] = AOTReadSwizzleMode(textureView->getTexture()->format());
        }
      }
    }
    fragmentUniformData->setData(ChainParams::LeafReadSwizzles.name, readSwizzles);
  }
  // Each selector names the leaf that reads the matching recipe set; the kernel never reads a set
  // whose selector is -1, so an absent recipe needs no upload.
  fragmentUniformData->setDataOptional(ChainParams::GradientLUTLeaf.name, program.lutLeafIndex);
  if (program.profile == AOTResourceProfile::TiledGraph) {
    for (size_t index = 0; index < TiledGraphResourceCapacity.tiledRecipes; ++index) {
      int modes[] = {-1, 0, 0, 0};
      if (index < program.tiledLeaves.size()) {
        const auto& tiled = program.tiledLeaves[index];
        modes[0] = tiled.leafIndex;
        modes[1] = static_cast<int>(tiled.recipe.shaderModeX);
        modes[2] = static_cast<int>(tiled.recipe.shaderModeY);
        modes[3] = tiled.recipe.strict ? 1 : 0;
        const auto& subset = tiled.recipe.shaderSubset;
        const auto& clamp = tiled.recipe.shaderClamp;
        float subsetData[] = {subset.left, subset.top, subset.right, subset.bottom};
        float clampData[] = {clamp.left, clamp.top, clamp.right, clamp.bottom};
        fragmentUniformData->setArrayElement(ChainParams::TiledRecipeSubsets.name, index,
                                             subsetData);
        fragmentUniformData->setArrayElement(ChainParams::TiledRecipeClamps.name, index, clampData);
        fragmentUniformData->setArrayElement(ChainParams::TiledRecipeDimensions.name, index,
                                             tiled.recipe.shaderDimensions);
      }
      fragmentUniformData->setArrayElement(ChainParams::TiledRecipeModes.name, index, modes);
    }
  } else {
    auto tiledLeaf = program.tiledLeaves.empty() ? -1 : program.tiledLeaves[0].leafIndex;
    auto tiledLeaf2 = program.tiledLeaves.size() < 2 ? -1 : program.tiledLeaves[1].leafIndex;
    fragmentUniformData->setDataOptional(ChainParams::TiledLeafIndex.name, tiledLeaf);
    fragmentUniformData->setDataOptional(ChainParams::TiledLeafIndex2.name, tiledLeaf2);
    for (size_t index = 0; index < program.tiledLeaves.size(); ++index) {
      UploadTiledRecipe(fragmentUniformData, program.tiledLeaves[index].recipe,
                        index == 0 ? ChainParams::TiledRecipe : ChainParams::TiledRecipe2);
    }
  }
  // Fill all declared slots (including inactive ones, whose OP_NONE marker protects the static
  // texture prefetch), but never probe the 16 absent indices of a base-chain shader.
  const auto* capacity = GetAOTResourceCapacity(program.profile);
  // The declared capacity exceeds the real slot count: the inactive tail carries the OP_NONE
  // marker (its zero selector protects the static texture prefetch), matching the fixed-size
  // storage the shell used to keep.
  AOTChainSlot inactiveSlot = {};
  for (size_t index = 0; index < capacity->instructions; ++index) {
    const auto& slot = index < slotCount ? slots[index] : inactiveSlot;
    UploadChainSlot(fragmentUniformData, index, slot);
  }
  // The color-space parameters are one shared chain-wide block, so the kernel supports at most one
  // color-space op per chain (enforced by the matcher). The helper writes the same field names the
  // standalone ColorSpaceXformEffect uses, prefixed with "Chain".
  for (size_t index = 0; index < slotCount; ++index) {
    if (slots[index].op == AOTChainOp::ColorSpaceXform) {
      ColorSpaceXformHelper("Chain").setData(fragmentUniformData,
                                             slots[index].colorSpaceXform.steps.get());
      break;
    }
  }
  // The gradient parameters are one shared chain-wide block (at most one gradient slot per chain,
  // enforced by the builder). The vertex-side coordinate transform travels through the GP-written
  // CoordTransformMatrix_0 instead.
  for (size_t index = 0; index < slotCount; ++index) {
    if (slots[index].op == AOTChainOp::Gradient) {
      const size_t blockBase =
          GetAOTResourceCapacity(program.profile)->instructions * ChainISA::PoolSlotWords +
          ChainISA::BlockGradientOffset;
      UploadGradientParams(fragmentUniformData, blockBase, slots[index].gradient);
      break;
    }
    if (slots[index].op == AOTChainOp::PerlinNoise) {
      const auto& noise = slots[index].noise;
      ChainISA::PerlinNoiseData data = {};
      data.frequencyStitch = {noise.baseFrequencyX, noise.baseFrequencyY,
                              static_cast<float>(noise.stitchWidth),
                              static_cast<float>(noise.stitchHeight)};
      UploadSlotData(fragmentUniformData, index, data, ChainISA::PackPerlinNoise);
      int modeOctaves[] = {noise.noiseType, noise.numOctaves, noise.stitchTiles ? 1 : 0,
                           program.noiseSamplerBase};
      fragmentUniformData->setDataOptional(ChainParams::NoiseModeOctaves.name, modeOctaves);
      break;
    }
  }
}

namespace {

// The subset semantics below replicate TextureEffect::needSubset / computeSubsetRect and the
// module bind's field writes from the binding's own data. The processor-side implementations stay
// until the shell retires and serve the un-migrated families meanwhile; byte-for-byte parity is
// what lets the binder take the chain's children over.
bool BindingNeedsSubset(const std::shared_ptr<TextureProxy>& proxy, const TextureView* view,
                        const std::optional<Rect>& subset) {
  auto bounds = Rect::MakeWH(proxy->width(), proxy->height());
  if (subset.has_value() && !(*subset).contains(bounds)) {
    return true;
  }
  if (proxy->width() != view->width() || proxy->height() != view->height()) {
    return true;
  }
  return view->getTexture()->type() == TextureType::External;
}

// The sampler kinds the two callers carry (SamplerState on the IR leaf, SamplingOptions on a
// chain binding) both spell the filter modes the same way, so one template serves both.
template <typename Sampling>
void LeafSubsetRect(const std::shared_ptr<TextureProxy>& proxy, const TextureView* view,
                    const std::optional<Rect>& subset, const Sampling& sampling, float rect[4]) {
  auto subsetRect = subset.value_or(Rect::MakeWH(proxy->width(), proxy->height()));
  if (BindingNeedsSubset(proxy, view, subset)) {
    if (sampling.magFilterMode == sampling.minFilterMode &&
        sampling.magFilterMode == FilterMode::Nearest) {
      subsetRect.roundOut();
    }
    auto type = view->getTexture()->type();
    // Normally this would just need to take 1/2 a texel off each end, but because the chroma
    // channels of YUV420 images are subsampled we may need to shrink the crop region by a whole
    // texel on each side for external textures.
    auto inset = type == TextureType::External ? 1.0f : 0.5f;
    subsetRect = subsetRect.makeInset(inset, inset);
  }
  rect[0] = subsetRect.left;
  rect[1] = subsetRect.top;
  rect[2] = subsetRect.right;
  rect[3] = subsetRect.bottom;
  if (view->origin() == ImageOrigin::BottomLeft) {
    auto height = static_cast<float>(view->height());
    rect[1] = height - rect[1];
    rect[3] = height - rect[3];
    std::swap(rect[1], rect[3]);
  }
  auto type = view->getTexture()->type();
  if (type != TextureType::Rectangle) {
    auto lt = view->getTextureCoord(rect[0], rect[1]);
    auto rb = view->getTextureCoord(rect[2], rect[3]);
    rect[0] = lt.x;
    rect[1] = lt.y;
    rect[2] = rb.x;
    rect[3] = rb.y;
  }
}

void BindingSubsetRect(const AOTBinding& binding, const TextureView* view, float rect[4]) {
  LeafSubsetRect(binding.textureProxy, view, binding.args.sampleArea, binding.args.sampling, rect);
}

// The device mask child's writes, replicating GLSLDeviceSpaceTextureEffect::onSetData.
void BindDeviceMaskBinding(const AOTBinding& binding, UniformData* uniformData) {
  auto textureView = binding.textureProxy->getTextureView();
  if (textureView == nullptr) {
    return;
  }
  auto deviceCoordMatrix = binding.uvMatrix;
  auto scale = textureView->getTextureCoord(1, 1);
  deviceCoordMatrix.postScale(scale.x, scale.y);
  uniformData->setData("DeviceCoordMatrix", deviceCoordMatrix);
  // Kernels with a runtime device mask read this flag; absent in compile-time-mask programs.
  uniformData->setDataOptional("HasDeviceMask", 1);
  // Shared precompiled kernels (a device mask alongside texture fills) declare a
  // DeviceMaskSubset decoy: the mask's Subset/AlphaOnly writes would clobber the leaf processor's
  // same-named fields, so they are routed there. The decoy is never read — these kernels sample
  // the mask unclamped, matching the runtime emission.
  bool sharedKernel = uniformData->hasField("DeviceMaskSubset");
  if (sharedKernel) {
    float rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    if (textureView->getTexture()->type() == TextureType::Rectangle) {
      rect[2] = static_cast<float>(textureView->width());
      rect[3] = static_cast<float>(textureView->height());
    }
    uniformData->setData("DeviceMaskSubset", rect);
  }
  if (!sharedKernel && uniformData->hasField("Subset")) {
    float rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    if (textureView->getTexture()->type() == TextureType::Rectangle) {
      rect[2] = static_cast<float>(textureView->width());
      rect[3] = static_cast<float>(textureView->height());
    }
    uniformData->setData("Subset", rect);
  }
  // AlphaOnly belongs to the draw's color source, not to the mask: the kernel samples the mask as
  // .r unconditionally, so writing it here would overwrite a color atlas's AlphaOnly=0.
  if (!sharedKernel && uniformData->hasField("Rect")) {
    // The shared coverage_output.inc always evaluates the AARect clip term when HAS_COVERAGE >= 1.
    // A bare device-space mask carries no RectEffect, so upload a rect that covers the whole plane.
    uniformData->setData("Rect", Rect::MakeLTRB(-1e9f, -1e9f, 1e9f, 1e9f));
  }
  if (!sharedKernel && uniformData->hasField("HasClip")) {
    uniformData->setData("HasClip", 0);
  }
}

}  // namespace

std::vector<CoordTransform> BuildChainCoordTransforms(const AOTProgram& program) {
  std::vector<CoordTransform> transforms = {};
  // The extended profiles register one chain-wide transform ahead of the children: the gradient's
  // coordinate matrix, or the noise source's, as the source module's input. It carries no texture
  // proxy, so its total matrix is the plain matrix. At most one of the two slots exists.
  if (IsAOTGraphProfile(program.profile)) {
    CoordTransform source = {};
    for (const auto& slot : program.slots) {
      if (slot.op == AOTChainOp::Gradient) {
        source = CoordTransform(slot.gradient.coordMatrix);
      } else if (slot.op == AOTChainOp::PerlinNoise) {
        source = CoordTransform(slot.noise.uvMatrix);
      }
    }
    transforms.push_back(source);
  }
  for (const auto& binding : program.bindings) {
    // The device mask samples at its DeviceLeafCoordMatrix * gl_FragCoord, so the device-space
    // effect registers no transform; the phantom mask slot is a plain texture child and does.
    if (binding.kind == AOTBinding::Kind::Mask) {
      continue;
    }
    transforms.emplace_back(binding.uvMatrix, binding.textureProxy.get(), binding.alphaStart);
  }
  return transforms;
}

// The IR-driven texture write (declared in Binder.h): the leaf carries the record-time facts,
// the proxy resolves the instance-time view, and the value gating mirrors
// TextureSourceModule::Bind field for field — byte-for-byte, because the leaf was lowered from
// that very processor.
void BindTextureSourceLeaf(const AOTTextureParameters& leaf, ParamWriter* writer) {
  auto textureView = leaf.textureProxy != nullptr ? leaf.textureProxy->getTextureView() : nullptr;
  if (textureView == nullptr) {
    // The texture is not instantiated (a failed or deferred upload): the processor-side module
    // bind returns without touching any field in this state, and so does this one.
    return;
  }
  float rect[4] = {};
  LeafSubsetRect(leaf.textureProxy, textureView.get(), leaf.subset, leaf.samplerState, rect);
  writer->set(TextureSourceModule::Subset, rect);
  // The two SubsetClampOn gates are deliberately different facts: this fill-leaf gate reads the
  // resolved view (a smaller-than-proxy view or an External texture needs the clamp whatever the
  // lowering decided), while the chain-binding gate below asks only whether the leaf carried a
  // sample area. Unifying the two onto one predicate changes bytes — do not "simplify" this.
  bool clampOn = BindingNeedsSubset(leaf.textureProxy, textureView.get(), leaf.subset);
  if (leaf.samplingKind == AOTTextureSamplingKind::Device ||
      leaf.samplingKind == AOTTextureSamplingKind::Tiled) {
    // Same forced-0 as the binding table's unbounded sample area (BuildTextureLeafBinding): the
    // runtime device-space effect is a bare texture lookup, and a hardware-wrapped leaf's wrap
    // must not be blunted by a GLSL clamp — neither clamps for any view fact, so these two kinds
    // never gate their clamp on, whatever needSubset() would say about their view.
    clampOn = false;
  }
  writer->set(TextureSourceModule::SubsetClampOn, clampOn ? 1 : 0);
  if (leaf.hasRGBAAA) {
    // Value-gated exactly like the module bind: single-plane draws leave the field at its
    // previous content, which the shader never reads (HasRgbaaa is 0).
    auto coord = textureView->getTextureCoord(leaf.alphaStart.x, leaf.alphaStart.y);
    const float alphaStart[2] = {coord.x, coord.y};
    writer->set(TextureSourceModule::AlphaStart, alphaStart);
  }
  writer->set(TextureSourceModule::AlphaOnly, leaf.isAlphaOnly ? 1 : 0);
  writer->set(TextureSourceModule::HasRgbaaa, leaf.hasRGBAAA ? 1 : 0);
  if (textureView->isYUV()) {
    auto* yuvView = static_cast<const YUVTextureView*>(textureView.get());
    writer->set(TextureSourceModule::YUVLimitedRange,
                IsLimitedYUVColorRange(yuvView->yuvColorSpace()) ? 1 : 0);
    if (auto conversion = YUVColorConversionMatrix(yuvView->yuvColorSpace())) {
      writer->set(TextureSourceModule::Mat3ColorConversion, *conversion);
    }
  }
}

// The IR-driven binds of the remaining feature modules (declared in Binder.h): each is the
// field-for-field twin of the module Bind it replaces, with the values read off the IR node
// the lowering recorded and the instance-time facts off the view the node's proxy resolves.

void BindTiledSourceLeaf(const AOTTextureParameters& leaf, ParamWriter* writer) {
  if (!leaf.tiledRecipe.has_value()) {
    return;
  }
  const auto& sampling = *leaf.tiledRecipe;
  if (sampling.usesShaderDimensions) {
    const float dimensions[2] = {sampling.shaderDimensions.x, sampling.shaderDimensions.y};
    writer->set(TiledSourceModule::Dimension, dimensions);
  }
  const float subset[4] = {sampling.shaderSubset.left, sampling.shaderSubset.top,
                           sampling.shaderSubset.right, sampling.shaderSubset.bottom};
  writer->set(TiledSourceModule::Subset, subset);
  const float clampRect[4] = {sampling.shaderClamp.left, sampling.shaderClamp.top,
                              sampling.shaderClamp.right, sampling.shaderClamp.bottom};
  writer->set(TiledSourceModule::Clamp, clampRect);
  writer->set(TiledSourceModule::ShaderModeX, static_cast<int32_t>(sampling.shaderModeX));
  writer->set(TiledSourceModule::ShaderModeY, static_cast<int32_t>(sampling.shaderModeY));
  writer->set(TiledSourceModule::AlphaOnly, sampling.alphaOnly ? 1 : 0);
}

void BindDeviceMaskLeaf(const AOTTextureParameters& mask, ParamWriter* writer) {
  auto textureView = mask.textureProxy != nullptr ? mask.textureProxy->getTextureView() : nullptr;
  if (textureView == nullptr) {
    return;
  }
  auto deviceCoordMatrix = mask.uvMatrix;
  auto scale = textureView->getTextureCoord(1, 1);
  deviceCoordMatrix.postScale(scale.x, scale.y);
  writer->set(DeviceMaskModule::DeviceCoordMatrix, deviceCoordMatrix);
  writer->set(DeviceMaskModule::HasDeviceMask, 1);
  float noOpRect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  if (textureView->getTexture()->type() == TextureType::Rectangle) {
    noOpRect[2] = static_cast<float>(textureView->width());
    noOpRect[3] = static_cast<float>(textureView->height());
  }
  if (writer->hasField("DeviceMaskSubset")) {
    writer->set(DeviceMaskModule::DeviceMaskSubset, noOpRect);
    return;
  }
  // AlphaOnly belongs to the draw's color source, not to the mask: the kernel samples the mask
  // as .r unconditionally. The whole-plane clip rect keeps the shared kernel's Subset/Rect
  // reading no-ops.
  writer->set(DeviceMaskModule::Subset, noOpRect);
  const float noClip[4] = {-1e9f, -1e9f, 1e9f, 1e9f};
  writer->set(AnalyticClipModule::Rect, noClip);
  writer->set(AnalyticClipModule::HasClip, 0);
}

void BindGradientNode(const AOTGradientParameters& params, ParamWriter* writer) {
  const float left[4] = {params.leftBorder[0], params.leftBorder[1], params.leftBorder[2],
                         params.leftBorder[3]};
  writer->set(GradientModule::LeftBorderColor, left);
  const float right[4] = {params.rightBorder[0], params.rightBorder[1], params.rightBorder[2],
                          params.rightBorder[3]};
  writer->set(GradientModule::RightBorderColor, right);
  writer->set(DeviceMaskModule::HasDeviceMask, 0);
  writer->set(GradientModule::LayoutType, params.layoutType);
  if (params.layoutType == 2) {
    // The conic layout child's fields; other layouts carry none.
    writer->set(GradientModule::Bias, params.bias);
    writer->set(GradientModule::Scale, params.scale);
  }
  if (params.colorizerKind == 0) {
    // The single-interval colorizer child's fields; other colorizers carry none. The kind
    // selector itself rides the chain-wide GradientColorizerKind block, which the chain upload
    // writes from the same record.
    const float start[4] = {params.start[0], params.start[1], params.start[2], params.start[3]};
    writer->set(GradientModule::Start, start);
    const float end[4] = {params.end[0], params.end[1], params.end[2], params.end[3]};
    writer->set(GradientModule::End, end);
  }
}

void BindRectClipNode(const AOTRectCoverageParameters& params, ParamWriter* writer) {
  const float localRect[4] = {params.rect[0], params.rect[1], params.rect[2], params.rect[3]};
  writer->set(AnalyticClipModule::LocalRect, localRect);
  // The RectCoverage node is the AA form: the lowering routes hard-edged rects through the
  // rrect node with a zero antiAlias.
  writer->set(AnalyticClipModule::AntiAlias, 1.0f);
  // An identity device-to-local is the device-space form's marker: the kernel reads the rect
  // directly, outset by half a texel for the falloff.
  if (!IsIdentityMat3RowMajor(params.deviceToLocal)) {
    writer->set(AnalyticClipModule::DeviceToLocal, Mat3ToStd140ColumnMajor(params.deviceToLocal));
  } else {
    const float deviceRect[4] = {params.rect[0] - 0.5f, params.rect[1] - 0.5f,
                                 params.rect[2] + 0.5f, params.rect[3] + 0.5f};
    writer->set(AnalyticClipModule::Rect, deviceRect);
  }
  writer->set(AnalyticClipModule::HasClip, IsIdentityMat3RowMajor(params.deviceToLocal) ? 1 : 3);
}

void BindRRectClipNode(const AOTRRectCoverageParameters& params, ParamWriter* writer) {
  const float localRect[4] = {params.rect[0], params.rect[1], params.rect[2], params.rect[3]};
  writer->set(AnalyticClipModule::LocalRect, localRect);
  const float radiiX[4] = {params.radiiX[0], params.radiiX[1], params.radiiX[2], params.radiiX[3]};
  writer->set(AnalyticClipModule::RadiiX, radiiX);
  const float radiiY[4] = {params.radiiY[0], params.radiiY[1], params.radiiY[2], params.radiiY[3]};
  writer->set(AnalyticClipModule::RadiiY, radiiY);
  writer->set(AnalyticClipModule::AntiAlias, params.antiAlias);
  writer->set(AnalyticClipModule::DeviceToLocal, Mat3ToStd140ColumnMajor(params.deviceToLocal));
  writer->set(AnalyticClipModule::HasClip, 2);
}

void BindBlurKernelNode(const AOTOpaqueKernelParameters& params, const AOTTextureParameters* child,
                        ParamWriter* writer) {
  writer->set(BlurKernelModule::Kernel, params.packedKernel);
  writer->set(BlurKernelModule::Radius, params.kernelRadius);
  Point stepVectors[] = {{0, 0}, {params.stepLength, 0}};
  if (params.vertical) {
    stepVectors[1] = {0, params.stepLength};
  }
  if (child != nullptr) {
    // The child's coordinate transform as it resolves now: the raw matrix the leaf recorded,
    // normalized by the view its proxy carries (CoordTransform::getTotalMatrix's math,
    // replicated — a record-time total would miss the normalization of a texture that had not
    // instantiated yet).
    auto view = child->textureProxy != nullptr ? child->textureProxy->getTextureView() : nullptr;
    auto matrix = child->uvMatrix;
    if (view != nullptr) {
      auto scale = view->getTextureCoord(1, 1);
      matrix.postScale(scale.x, scale.y);
      if (view->origin() == ImageOrigin::BottomLeft) {
        matrix.postScale(1, -1);
        auto translate = view->getTextureCoord(0, static_cast<float>(view->height()));
        matrix.postTranslate(translate.x, translate.y);
      }
    }
    matrix.mapPoints(stepVectors, 2);
  }
  Point step = stepVectors[1] - stepVectors[0];
  const float stepValues[2] = {step.x, step.y};
  writer->set(BlurKernelModule::Step, stepValues);
  writer->set(DeviceMaskModule::HasDeviceMask, 0);
  writer->set(BlurKernelModule::TiledChild,
              child != nullptr && child->tiledRecipe.has_value() ? 1 : 0);
  writer->set(BlurKernelModule::AlphaChild, child != nullptr && child->isAlphaOnly ? 1 : 0);
}

void BindPorterDuffXfer(int blendMode, const XferProcessor* processor, ParamWriter* writer) {
  writer->set(PorterDuffXPModule::XPBlendMode, blendMode);
  auto* xp = static_cast<const PorterDuffXferProcessor*>(processor);
  const auto& dstInfo = xp->getDstTextureInfo();
  if (dstInfo.textureProxy == nullptr) {
    return;
  }
  auto dstView = dstInfo.textureProxy->getTextureView();
  if (dstView == nullptr) {
    return;
  }
  const float upperLeft[2] = {dstInfo.offset.x, dstInfo.offset.y};
  writer->set(PorterDuffXPModule::DstTextureUpperLeft, upperLeft);
  int width = dstView->width();
  int height = dstView->height();
  if (dstView->getTexture()->type() == TextureType::Rectangle) {
    width = 1;
    height = 1;
  }
  const float scales[2] = {1.f / static_cast<float>(width), 1.f / static_cast<float>(height)};
  writer->set(PorterDuffXPModule::DstTextureCoordScale, scales);
}

void BindChainBindings(const AOTProgram& program, UniformData* fragmentUniformData) {
  if (fragmentUniformData == nullptr) {
    return;
  }
  ParamWriter writer(fragmentUniformData);
  // The TextureEffect-class children (leaves, the LUT gradient, the padding) share one
  // structural-ordinal sequence, exactly as the processor-tree walk numbered them; the device
  // mask is the only child of its class and keeps the base names.
  int textureOrdinal = 0;
  for (const auto& binding : program.bindings) {
    if (binding.kind == AOTBinding::Kind::Mask) {
      writer.setInstanceOrdinal(0);
      BindDeviceMaskBinding(binding, fragmentUniformData);
      continue;
    }
    writer.setInstanceOrdinal(static_cast<size_t>(textureOrdinal));
    ++textureOrdinal;
    auto textureView = binding.textureProxy->getTextureView();
    if (textureView == nullptr) {
      // The texture is not instantiated (a failed or deferred upload): the module writer returns
      // without touching any field in this state, and so does the binder.
      continue;
    }
    float rect[4] = {};
    BindingSubsetRect(binding, textureView.get(), rect);
    writer.set(TextureSourceModule::Subset, rect);
    // The lowering's own decision (a bounded sample area), not the fill-leaf gate's view facts:
    // a leaf can carry view facts that say "clamp" while this says off. Two gates, two facts —
    // deliberately.
    writer.set(TextureSourceModule::SubsetClampOn, binding.args.sampleArea.has_value() ? 1 : 0);
    if (binding.alphaStart != Point::Zero()) {
      // Value-gated exactly like the module bind: single-plane draws leave the field at its
      // previous content, which the shader never reads (HasRgbaaa is 0).
      auto coord = textureView->getTextureCoord(binding.alphaStart.x, binding.alphaStart.y);
      const float alphaStart[2] = {coord.x, coord.y};
      writer.set(TextureSourceModule::AlphaStart, alphaStart);
    }
    writer.set(TextureSourceModule::AlphaOnly, binding.textureProxy->isAlphaOnly() ? 1 : 0);
    writer.set(TextureSourceModule::HasRgbaaa, binding.alphaStart != Point::Zero() ? 1 : 0);
  }
}

}  // namespace tgfx

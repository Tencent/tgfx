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
#include "gpu/aot/write/ParamBuffer.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/TextureView.h"
#include "gpu/resources/YUVTextureView.h"
#include "gpu/shaders/KernelContract.h"
#include "gpu/shaders/contract/ChainOps.h"
#include "gpu/shaders/modules/Chain.h"
#include "gpu/shaders/modules/Xfer.h"
#include "gpu/shaders/modules/YUVChainTail.h"

namespace tgfx {
namespace {

// The Chain module's field indices: the declared module is the single source of the slot block's
// element order, so a writer addresses fields by index and a rename cannot drift.
constexpr uint8_t ChainElement(const char* name) {
  return UniformElementIndexOf(Chain, name);
}

// The YUV tail declares the same slot pool under its own module (a different element order).
constexpr uint8_t YUVElement(const char* name) {
  return UniformElementIndexOf(YUVChainTail, name);
}

// Every field the binder addresses, evaluated once as constants: a name missing from the
// declaration is a link error (Module.h's ElementNameNotFound), so the indices must be constant.
struct ChainIndices {
  uint8_t chainData = ChainElement("ChainData");
  uint8_t clipCoverageRegister = ChainElement("ClipCoverageRegister");
  uint8_t coverageRootIndex = ChainElement("CoverageRootIndex");
  uint8_t deviceLeafCoordMatrix = ChainElement("DeviceLeafCoordMatrix");
  uint8_t deviceLeafMask = ChainElement("DeviceLeafMask");
  uint8_t gradientColorizerKind = ChainElement("GradientColorizerKind");
  uint8_t gradientIntervalCount = ChainElement("GradientIntervalCount");
  uint8_t gradientLayoutType = ChainElement("GradientLayoutType");
  uint8_t gradientLUTLeaf = ChainElement("GradientLUTLeaf");
  uint8_t hasMaskTexture = ChainElement("HasMaskTexture");
  uint8_t leafReadSwizzles = ChainElement("LeafReadSwizzles");
  uint8_t noiseModeOctaves = ChainElement("NoiseModeOctaves");
  uint8_t rootIndex = ChainElement("RootIndex");
  uint8_t slotCount = ChainElement("SlotCount");
  uint8_t slotPacked = ChainElement("SlotPacked");
  uint8_t tiledClamp = ChainElement("TiledClamp");
  uint8_t tiledClamp2 = ChainElement("TiledClamp2");
  uint8_t tiledDimension = ChainElement("TiledDimension");
  uint8_t tiledDimension2 = ChainElement("TiledDimension2");
  uint8_t tiledLeafIndex = ChainElement("TiledLeafIndex");
  uint8_t tiledLeafIndex2 = ChainElement("TiledLeafIndex2");
  uint8_t tiledModeX = ChainElement("TiledModeX");
  uint8_t tiledModeX2 = ChainElement("TiledModeX2");
  uint8_t tiledModeY = ChainElement("TiledModeY");
  uint8_t tiledModeY2 = ChainElement("TiledModeY2");
  uint8_t tiledRecipeClamps = ChainElement("TiledRecipeClamps");
  uint8_t tiledRecipeDimensions = ChainElement("TiledRecipeDimensions");
  uint8_t tiledRecipeModes = ChainElement("TiledRecipeModes");
  uint8_t tiledRecipeSubsets = ChainElement("TiledRecipeSubsets");
  uint8_t tiledStrict = ChainElement("TiledStrict");
  uint8_t tiledStrict2 = ChainElement("TiledStrict2");
  uint8_t tiledSubset = ChainElement("TiledSubset");
  uint8_t tiledSubset2 = ChainElement("TiledSubset2");
};
constexpr ChainIndices ChainField = {};
struct YUVTailIndices {
  uint8_t chainData = YUVElement("ChainData");
  uint8_t slotCount = YUVElement("SlotCount");
  uint8_t slotPacked = YUVElement("SlotPacked");
};
constexpr YUVTailIndices YUVField = {};

// The chain-wide color-space block's element indices: the Chain and YUVChainTail modules spell
// the same ten fields, each through its own element order.
constexpr ColorSpaceXformHelper::FieldElements ChainCSFields = {
    ChainElement("ChainCSFlags"),   ChainElement("ChainSrcTF0"),  ChainElement("ChainSrcTF1"),
    ChainElement("ChainSrcTFType"), ChainElement("ChainSrcOOTF"), ChainElement("ChainColorXform"),
    ChainElement("ChainDstOOTF"),   ChainElement("ChainDstTF0"),  ChainElement("ChainDstTF1"),
    ChainElement("ChainDstTFType")};
constexpr ColorSpaceXformHelper::FieldElements YUVCSFields = {
    YUVElement("ChainCSFlags"),   YUVElement("ChainSrcTF0"),  YUVElement("ChainSrcTF1"),
    YUVElement("ChainSrcTFType"), YUVElement("ChainSrcOOTF"), YUVElement("ChainColorXform"),
    YUVElement("ChainDstOOTF"),   YUVElement("ChainDstTF0"),  YUVElement("ChainDstTF1"),
    YUVElement("ChainDstTFType")};

/// One chain instruction's per-slot parameter write. The packed word and its selector stay with
/// the caller's dialect (the chain encodes result registers, the YUV tail encodes the op's small
/// value), so the table carries only the parameter arrays: `dataElement` is the parameter pool's
/// element index in the caller's module, `index` is the slot's index (the per-op arrays),
/// `ordinal` is its ordinal into a per-kind array (the coverage capacities; the slot index for
/// every other op). A null row means the instruction writes no parameter block.
using AOTOpBinder = void (*)(SlotWriter* chainWriter, uint8_t dataElement, size_t index,
                             int ordinal, const AOTChainSlot& slot);

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

// One tiled recipe set's element indices: the first recipe's fields, then the "2"-suffixed ones.
struct TiledRecipeIndices {
  uint8_t modeX;
  uint8_t modeY;
  uint8_t subset;
  uint8_t clamp;
  uint8_t dimension;
  uint8_t strict;
};

constexpr TiledRecipeIndices TiledRecipe0 = {ChainField.tiledModeX,     ChainField.tiledModeY,
                                             ChainField.tiledSubset,    ChainField.tiledClamp,
                                             ChainField.tiledDimension, ChainField.tiledStrict};
constexpr TiledRecipeIndices TiledRecipe1 = {ChainField.tiledModeX2,     ChainField.tiledModeY2,
                                             ChainField.tiledSubset2,    ChainField.tiledClamp2,
                                             ChainField.tiledDimension2, ChainField.tiledStrict2};

// Uploads one tiled recipe into the kernel's element set (the first recipe, then the second),
// addressed through the derived layout.
void UploadTiledRecipe(SlotWriter* chainWriter, const AOTTiledTextureRecipe& recipe,
                       const TiledRecipeIndices& indices) {
  chainWriter->setIfPresent(indices.modeX, static_cast<int>(recipe.shaderModeX));
  chainWriter->setIfPresent(indices.modeY, static_cast<int>(recipe.shaderModeY));
  float subsetRect[] = {recipe.shaderSubset.left, recipe.shaderSubset.top,
                        recipe.shaderSubset.right, recipe.shaderSubset.bottom};
  chainWriter->setIfPresent(indices.subset, subsetRect);
  float clampRect[] = {recipe.shaderClamp.left, recipe.shaderClamp.top, recipe.shaderClamp.right,
                       recipe.shaderClamp.bottom};
  chainWriter->setIfPresent(indices.clamp, clampRect);
  if (recipe.usesShaderDimensions) {
    chainWriter->setIfPresent(indices.dimension, recipe.shaderDimensions);
  }
  chainWriter->setIfPresent(indices.strict, recipe.strict ? 1 : 0);
}

void UploadChainSlot(SlotWriter* chainWriter, uint8_t dataElement, size_t index,
                     const AOTChainSlot& slot) {
  // The packed record's selector is shared with the specialization values (Program.h).
  const int selector = ChainSlotSelector(slot);

  int packed[] = {static_cast<int>(slot.op), slot.in0, slot.in1, selector};
  chainWriter->setArrayElement(ChainField.slotPacked, index, packed);
  // The parameter block is the instruction contract's bind column: one function per op that
  // writes one, a null row for the instructions that carry none (they ride the packed word).
  if (auto bind = OpContractOf(slot.op).bind) {
    bind(chainWriter, dataElement, index, static_cast<int>(index), slot);
  }
}

// Writes one operator's parameter block into the slot's data-pool region. The block layout and
// the packer both come from contract/ChainOps.h, so a field cannot be written at one offset and
// read at another.
template <typename Data>
void UploadSlotData(SlotWriter* chainWriter, uint8_t dataElement, size_t index, const Data& data,
                    void (*pack)(const Data&, float*)) {
  float words[ChainISA::PoolSlotWords * 4] = {};
  pack(data, words);
  const size_t base = index * static_cast<size_t>(ChainISA::PoolSlotWords);
  for (size_t word = 0; word < static_cast<size_t>(ChainISA::PoolSlotWords); ++word) {
    const std::array<float, 4> word4 = {words[word * 4], words[word * 4 + 1], words[word * 4 + 2],
                                        words[word * 4 + 3]};
    chainWriter->setArrayElement(dataElement, base + word, word4);
  }
}

ChainISA::Float4 ToFloat4(const std::array<float, 4>& values) {
  return {values[0], values[1], values[2], values[3]};
}

// Writes the chain's gradient parameter block into the pool's chain-wide region. The three
// selectors stay runtime uniforms until the structural fields move to specialization constants.
void UploadGradientParams(SlotWriter* chainWriter, size_t blockBase,
                          const AOTGradientParameters& params) {
  chainWriter->setIfPresent(ChainField.gradientLayoutType, params.layoutType);
  chainWriter->setIfPresent(ChainField.gradientColorizerKind, params.colorizerKind);
  chainWriter->setIfPresent(ChainField.gradientIntervalCount, params.intervalCount);
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
    chainWriter->setArrayElement(ChainField.chainData, blockBase + word, word4);
  }
}

}  // namespace

// The instruction contract's bind column (declared in Binder.h): each function writes one op's
// per-slot parameter block into the chain arrays.
void BindColorMatrixSlot(SlotWriter* chainWriter, uint8_t dataElement, size_t index,
                         int /*ordinal*/, const AOTChainSlot& slot) {
  ChainISA::ColorMatrixData data = {};
  float translate[4] = {};
  PackAOTColorMatrix(slot.colorMatrix.matrix, data.linear.m, translate);
  data.translate = {translate[0], translate[1], translate[2], translate[3]};
  UploadSlotData(chainWriter, dataElement, index, data, ChainISA::PackColorMatrix);
}

void BindLumaSlot(SlotWriter* chainWriter, uint8_t dataElement, size_t index, int /*ordinal*/,
                  const AOTChainSlot& slot) {
  UploadSlotData(chainWriter, dataElement, index, slot.luma, ChainISA::PackLuma);
}

void BindAlphaThresholdSlot(SlotWriter* chainWriter, uint8_t dataElement, size_t index,
                            int /*ordinal*/, const AOTChainSlot& slot) {
  UploadSlotData(chainWriter, dataElement, index, slot.alphaThreshold,
                 ChainISA::PackAlphaThreshold);
}

void BindConstColorSlot(SlotWriter* chainWriter, uint8_t dataElement, size_t index, int /*ordinal*/,
                        const AOTChainSlot& slot) {
  const auto& color = slot.constColor.color;
  ChainISA::ConstColorData data = {};
  data.color = {color[0], color[1], color[2], color[3]};
  UploadSlotData(chainWriter, dataElement, index, data, ChainISA::PackConstColor);
}

void BindAARectCoverageSlot(SlotWriter* chainWriter, uint8_t dataElement, size_t index,
                            int /*ordinal*/, const AOTChainSlot& slot) {
  // The AA math evaluates to 0 at the rect coordinates, so outset by 0.5 to interpolate from 0
  // at a half-pixel inset to 1 at a half-pixel outset.
  const auto& rect = slot.rectCoverage.rect;
  ChainISA::AARectCoverageData data = {};
  data.rect = {rect[0] - 0.5f, rect[1] - 0.5f, rect[2] + 0.5f, rect[3] + 0.5f};
  UploadSlotData(chainWriter, dataElement, index, data, ChainISA::PackAARectCoverage);
}

void BindLocalRectCoverageSlot(SlotWriter* chainWriter, uint8_t dataElement, size_t index,
                               int /*ordinal*/, const AOTChainSlot& slot) {
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
  UploadSlotData(chainWriter, dataElement, index, data, ChainISA::PackLocalRectCoverage);
}

void BindRRectCoverageSlot(SlotWriter* chainWriter, uint8_t dataElement, size_t index,
                           int /*ordinal*/, const AOTChainSlot& slot) {
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
  UploadSlotData(chainWriter, dataElement, index, data, ChainISA::PackRRectCoverage);
}

// One YUV-tail instruction's records (see BindYUVProgram): the chain's uniform arrays with the
// linear-tail selector — the kernel's macro binding reads exactly these bits.
void UploadYUVTailSlot(SlotWriter* yuvWriter, uint8_t dataElement, size_t index,
                       const AOTChainSlot& slot) {
  int selector = 0;
  if (slot.op == AOTChainOp::ConstColor) {
    selector = slot.constColor.inputMode;
  } else if (slot.op == AOTChainOp::Blend) {
    selector = slot.blend.blendMode | (slot.blend.childType == 1 ? 0x100 : 0);
  }
  int packed[] = {static_cast<int>(slot.op), -2, -2, selector};
  yuvWriter->setArrayElement(YUVField.slotPacked, index, packed);
  // The parameter block comes from the instruction contract — except the linear tail's Blend,
  // whose operand is the const color (the chain's blend reads registers, so its row writes
  // nothing; the tail's dialect supplies the color instead).
  if (slot.op == AOTChainOp::Blend) {
    // The tail's blend operand is the const color, packed into the Blend block's color field.
    const auto& color = slot.constColor.color;
    ChainISA::BlendData data = {};
    data.color = {color[0], color[1], color[2], color[3]};
    UploadSlotData(yuvWriter, dataElement, index, data, ChainISA::PackBlend);
  } else if (auto bind = OpContractOf(slot.op).bind) {
    bind(yuvWriter, dataElement, index, static_cast<int>(index), slot);
  }
}

void BindYUVProgram(const AOTProgram& program, SlotWriter* yuvWriter) {
  if (yuvWriter == nullptr) {
    return;
  }
  const auto& slots = program.slots;
  yuvWriter->set(YUVField.slotCount, static_cast<int>(slots.size()));
  // The declared capacity exceeds the real count: the inactive tail carries the OP_NONE marker,
  // matching the fixed-size storage every multi-slot kernel keeps.
  AOTChainSlot inactiveSlot = {};
  for (size_t index = 0; index < YUVChainTailSlotCapacity; ++index) {
    UploadYUVTailSlot(yuvWriter, YUVField.chainData, index,
                      index < slots.size() ? slots[index] : inactiveSlot);
  }
  // The color-space parameters are one shared block (at most one transform per tail, enforced
  // by the matching walk), addressed through the tail module's element indices.
  for (const auto& slot : slots) {
    if (slot.op == AOTChainOp::ColorSpaceXform) {
      ColorSpaceXformHelper().setData(yuvWriter, YUVCSFields, slot.colorSpaceXform.steps.get());
      break;
    }
  }
}

void BindChainProgram(const AOTProgram& program, SlotWriter* chainWriter) {
  const auto& slots = program.slots;
  const auto slotCount = slots.size();
  if (chainWriter == nullptr) {
    return;
  }
  // The coord-source mask is the geometry module's field (ChainRectUVGeometry / ChainAtlasGeometry)
  // and its upload belongs to the geometry writer.
  // The mask sampler slot is always bound in four-leaf variants; this flag selects the
  // runtime application. Absent in leaf-free variants (compile-time mask there).
  chainWriter->setIfPresent(ChainField.hasMaskTexture, program.hasMaskTexture ? 1 : 0);
  chainWriter->set(ChainField.rootIndex, static_cast<int>(program.rootRegister));
  chainWriter->set(ChainField.coverageRootIndex, program.coverageRootRegister);
  chainWriter->set(ChainField.clipCoverageRegister, program.clipCoverageRegister);
  chainWriter->set(ChainField.slotCount, static_cast<int>(slotCount));
  // Written for every draw (absent in leaf-free variants): a stale mask from an earlier draw
  // would make an ordinary leaf sample in device space.
  uint32_t deviceMask = 0;
  for (const auto& deviceLeaf : program.deviceLeaves) {
    deviceMask |= 1u << static_cast<uint32_t>(deviceLeaf.leafIndex);
  }
  chainWriter->setIfPresent(ChainField.deviceLeafMask, static_cast<int>(deviceMask));
  for (const auto& deviceLeaf : program.deviceLeaves) {
    // The leaf's uniform: destination fragment coordinates to the texture's sampling space.
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
    chainWriter->setArrayElementIfPresent(ChainField.deviceLeafCoordMatrix,
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
    chainWriter->setIfPresent(ChainField.leafReadSwizzles, readSwizzles);
  }
  // Each selector names the leaf that reads the matching recipe set; the kernel never reads a set
  // whose selector is -1, so an absent recipe needs no upload.
  chainWriter->setIfPresent(ChainField.gradientLUTLeaf, program.lutLeafIndex);
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
        chainWriter->setArrayElement(ChainField.tiledRecipeSubsets, index, subsetData);
        chainWriter->setArrayElement(ChainField.tiledRecipeClamps, index, clampData);
        chainWriter->setArrayElement(ChainField.tiledRecipeDimensions, index,
                                     tiled.recipe.shaderDimensions);
      }
      chainWriter->setArrayElement(ChainField.tiledRecipeModes, index, modes);
    }
  } else {
    auto tiledLeaf = program.tiledLeaves.empty() ? -1 : program.tiledLeaves[0].leafIndex;
    auto tiledLeaf2 = program.tiledLeaves.size() < 2 ? -1 : program.tiledLeaves[1].leafIndex;
    chainWriter->setIfPresent(ChainField.tiledLeafIndex, tiledLeaf);
    chainWriter->setIfPresent(ChainField.tiledLeafIndex2, tiledLeaf2);
    for (size_t index = 0; index < program.tiledLeaves.size(); ++index) {
      UploadTiledRecipe(chainWriter, program.tiledLeaves[index].recipe,
                        index == 0 ? TiledRecipe0 : TiledRecipe1);
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
    UploadChainSlot(chainWriter, ChainField.chainData, index, slot);
  }
  // The color-space parameters are one shared chain-wide block, so the kernel supports at most one
  // color-space op per chain (enforced by the matcher), addressed through the Chain module's
  // element indices.
  for (size_t index = 0; index < slotCount; ++index) {
    if (slots[index].op == AOTChainOp::ColorSpaceXform) {
      ColorSpaceXformHelper().setData(chainWriter, ChainCSFields,
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
      UploadGradientParams(chainWriter, blockBase, slots[index].gradient);
      break;
    }
    if (slots[index].op == AOTChainOp::PerlinNoise) {
      const auto& noise = slots[index].noise;
      ChainISA::PerlinNoiseData data = {};
      data.frequencyStitch = {noise.baseFrequencyX, noise.baseFrequencyY,
                              static_cast<float>(noise.stitchWidth),
                              static_cast<float>(noise.stitchHeight)};
      UploadSlotData(chainWriter, ChainField.chainData, index, data, ChainISA::PackPerlinNoise);
      int modeOctaves[] = {noise.noiseType, noise.numOctaves, noise.stitchTiles ? 1 : 0,
                           program.noiseSamplerBase};
      chainWriter->setIfPresent(ChainField.noiseModeOctaves, modeOctaves);
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

// The device mask child's writes: its uv matrix scaled into the texture's sampling space.
void BindDeviceMaskBinding(const AOTBinding& binding, SlotWriter* chainWriter) {
  auto textureView = binding.textureProxy->getTextureView();
  if (textureView == nullptr) {
    return;
  }
  auto deviceCoordMatrix = binding.uvMatrix;
  auto scale = textureView->getTextureCoord(1, 1);
  deviceCoordMatrix.postScale(scale.x, scale.y);
  constexpr uint8_t DeviceCoordMatrix = UniformElementIndexOf(Chain, "DeviceCoordMatrix");
  constexpr uint8_t DeviceMaskSubset = UniformElementIndexOf(Chain, "DeviceMaskSubset");
  chainWriter->setIfPresent(DeviceCoordMatrix, deviceCoordMatrix);
  // The Chain module declares the mask's decoy subset (the leaves own the shared kernel's
  // Subset/Rect names); the fields the declared chain does not carry were silent no-ops in the
  // by-name writer and stay absent here.
  if (chainWriter->present(DeviceMaskSubset)) {
    float rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    if (textureView->getTexture()->type() == TextureType::Rectangle) {
      rect[2] = static_cast<float>(textureView->width());
      rect[3] = static_cast<float>(textureView->height());
    }
    chainWriter->set(DeviceMaskSubset, rect);
  }
}

}  // namespace

/// The declared-family writer reads the same subset facts the module binds compute; the helpers
/// above stay file-local, so these wrappers export them with the IR leaf's SamplerState.
bool LeafSubsetClampOn(const std::shared_ptr<TextureProxy>& proxy, const TextureView* view,
                       const std::optional<Rect>& subset) {
  return BindingNeedsSubset(proxy, view, subset);
}

void LeafSubsetRectForSampler(const std::shared_ptr<TextureProxy>& proxy, const TextureView* view,
                              const std::optional<Rect>& subset, const SamplerState& sampling,
                              float rect[4]) {
  LeafSubsetRect(proxy, view, subset, sampling, rect);
}

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

void BindPorterDuffXfer(int blendMode, const DstTextureInfo& dstInfo, SlotWriter* xferWriter) {
  constexpr uint8_t XPBlendMode = UniformElementIndexOf(Xfer, "XPBlendMode");
  constexpr uint8_t DstTextureUpperLeft = UniformElementIndexOf(Xfer, "DstTextureUpperLeft");
  constexpr uint8_t DstTextureCoordScale = UniformElementIndexOf(Xfer, "DstTextureCoordScale");
  xferWriter->setIfPresent(XPBlendMode, blendMode);
  if (dstInfo.textureProxy == nullptr) {
    return;
  }
  auto dstView = dstInfo.textureProxy->getTextureView();
  if (dstView == nullptr) {
    return;
  }
  const float upperLeft[2] = {dstInfo.offset.x, dstInfo.offset.y};
  xferWriter->setIfPresent(DstTextureUpperLeft, upperLeft);
  int width = dstView->width();
  int height = dstView->height();
  if (dstView->getTexture()->type() == TextureType::Rectangle) {
    width = 1;
    height = 1;
  }
  const float scales[2] = {1.f / static_cast<float>(width), 1.f / static_cast<float>(height)};
  xferWriter->setIfPresent(DstTextureCoordScale, scales);
}

void BindChainBindings(const AOTProgram& program, SlotWriter* chainWriter) {
  if (chainWriter == nullptr) {
    return;
  }
  // The leaf records live in the Chain module, one declared element per leaf slot: the legacy
  // ParamWriter composed "Subset_1".."Subset_3" from an instance ordinal, while the declared
  // module spells the same fields as elements, so the slot index selects the element directly.
  constexpr uint8_t Subsets[4] = {
      UniformElementIndexOf(Chain, "Subset"), UniformElementIndexOf(Chain, "Subset_1"),
      UniformElementIndexOf(Chain, "Subset_2"), UniformElementIndexOf(Chain, "Subset_3")};
  constexpr uint8_t Clamps[4] = {UniformElementIndexOf(Chain, "SubsetClampOn"),
                                 UniformElementIndexOf(Chain, "SubsetClampOn_1"),
                                 UniformElementIndexOf(Chain, "SubsetClampOn_2"),
                                 UniformElementIndexOf(Chain, "SubsetClampOn_3")};
  constexpr uint8_t AlphaStarts[4] = {
      UniformElementIndexOf(Chain, "AlphaStart"), UniformElementIndexOf(Chain, "AlphaStart_1"),
      UniformElementIndexOf(Chain, "AlphaStart_2"), UniformElementIndexOf(Chain, "AlphaStart_3")};
  constexpr uint8_t HasRgbaaas[4] = {
      UniformElementIndexOf(Chain, "HasRgbaaa"), UniformElementIndexOf(Chain, "HasRgbaaa_1"),
      UniformElementIndexOf(Chain, "HasRgbaaa_2"), UniformElementIndexOf(Chain, "HasRgbaaa_3")};
  // The TextureEffect-class children (leaves, the LUT gradient, the padding) share one
  // structural-ordinal sequence, exactly as the processor-tree walk numbered them; the device
  // mask is the only child of its class and keeps the base names.
  int textureOrdinal = 0;
  for (const auto& binding : program.bindings) {
    if (binding.kind == AOTBinding::Kind::Mask) {
      BindDeviceMaskBinding(binding, chainWriter);
      continue;
    }
    const int ordinal = textureOrdinal;
    ++textureOrdinal;
    auto textureView = binding.textureProxy->getTextureView();
    if (textureView == nullptr || ordinal >= 4) {
      // A deferred upload writes nothing; a fifth texture child has no declared element (its
      // ordinal-qualified name missed the block too).
      continue;
    }
    float rect[4] = {};
    BindingSubsetRect(binding, textureView.get(), rect);
    chainWriter->setIfPresent(Subsets[ordinal], rect);
    // The lowering's own decision (a bounded sample area), not the fill-leaf gate's view facts:
    // a leaf can carry view facts that say "clamp" while this says off. Two gates, two facts —
    // deliberately.
    chainWriter->setIfPresent(Clamps[ordinal], binding.args.sampleArea.has_value() ? 1 : 0);
    if (binding.alphaStart != Point::Zero()) {
      // Value-gated exactly like the module bind: single-plane draws leave the field at its
      // previous content, which the shader never reads (HasRgbaaa is 0).
      auto coord = textureView->getTextureCoord(binding.alphaStart.x, binding.alphaStart.y);
      const float alphaStart[2] = {coord.x, coord.y};
      chainWriter->setIfPresent(AlphaStarts[ordinal], alphaStart);
    }
    chainWriter->setIfPresent(HasRgbaaas[ordinal], binding.alphaStart != Point::Zero() ? 1 : 0);
  }
}

}  // namespace tgfx

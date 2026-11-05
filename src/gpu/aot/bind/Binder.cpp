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
#include "gpu/aot/bind/Binder.h"
#include "gpu/aot/ir/NodeContract.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/TextureView.h"
#include "gpu/resources/YUVTextureView.h"
#include "gpu/shaders/KernelContract.h"
#include "gpu/shaders/modules/AnalyticClipModule.h"
#include "gpu/shaders/modules/BlendParams.h"
#include "gpu/shaders/modules/BlurKernelModule.h"
#include "gpu/shaders/modules/ChainParams.h"
#include "gpu/shaders/modules/DeviceMaskModule.h"
#include "gpu/shaders/modules/GradientModule.h"
#include "gpu/shaders/modules/ParamWriter.h"
#include "gpu/shaders/modules/PointwiseOpModule.h"
#include "gpu/shaders/modules/PorterDuffXPModule.h"
#include "gpu/shaders/modules/TextureSourceModule.h"
#include "gpu/shaders/modules/TiledSourceModule.h"

namespace tgfx {
namespace {

// std140 column packing of a 3x3 matrix stored in Matrix::get9 row-major order, mirroring
// UniformData::setData(const Matrix&) so chain uploads match the same layout.
std::array<float, 12> MatrixColumnMajor(const std::array<float, 9>& values) {
  return {values[0], values[3], values[6], 0.0f,      values[1], values[4],
          values[7], 0.0f,      values[2], values[5], values[8], 0.0f};
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

// Pure slot encoding shared by the uniform upload and the runtime chain specializer: the packed
// instruction word (op, in0, in1, selector) exactly as UploadChainSlot writes it, so a
// specialized shader folds the same constants the interpreter would read at runtime.
static std::array<int, 4> EncodeChainSlotPacked(const AOTChainSlot& slot, int rectOrdinal,
                                                int localRectOrdinal, int rrectOrdinal) {
  int selector = slot.op == AOTChainOp::Blend
                     ? slot.blend.blendMode | (slot.blend.multiplyInputAlpha ? 0x100 : 0)
                     : 0;
  if (slot.op == AOTChainOp::ConstColor) {
    selector = slot.constColor.inputMode;
  }
  if (slot.op == AOTChainOp::Texture) {
    // Bit 0: modulate by the paint alpha. Bit 1: alpha-only leaf, splat .r into all channels.
    // Bit 2: coverage-root leaf, modulated by the coverage unit alpha. Bit 3: alpha-only leaf
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
  // The coverage capacities index their own parameter arrays by the per-kind ordinal, which the
  // selector also carries (bits 16-19); every other instruction's parameter block is indexed by
  // the slot.
  if (slot.op == AOTChainOp::AARectCoverage) {
    selector = rectOrdinal << 16;
  }
  if (slot.op == AOTChainOp::LocalRectCoverage) {
    selector = localRectOrdinal << 16;
  }
  if (slot.op == AOTChainOp::RRectCoverage) {
    selector = rrectOrdinal << 16;
  }
  // Bits 20-24: the result register + 1 (0 marks a dead instruction whose result nothing reads;
  // the kernel then skips the chainResults write).
  selector |= (slot.outRegister + 1) << 20;
  return {static_cast<int>(slot.op), slot.in0, slot.in1, selector};
}

void UploadChainSlot(UniformData* uniformData, size_t index, const AOTChainSlot& slot,
                     int rectOrdinal, int localRectOrdinal, int rrectOrdinal) {
  auto packed = EncodeChainSlotPacked(slot, rectOrdinal, localRectOrdinal, rrectOrdinal);
  uniformData->setArrayElementOptional(ChainParams::SlotPacked.name, index, packed);
  // The parameter block is the instruction contract's bind column: one function per op that
  // writes one, a null row for the instructions that carry none (they ride the packed word).
  // Coverage ops index their per-kind parameter arrays by the ordinal the selector carries.
  int ordinal = static_cast<int>(index);
  if (slot.op == AOTChainOp::AARectCoverage) {
    ordinal = rectOrdinal;
  } else if (slot.op == AOTChainOp::LocalRectCoverage) {
    ordinal = localRectOrdinal;
  } else if (slot.op == AOTChainOp::RRectCoverage) {
    ordinal = rrectOrdinal;
  }
  if (auto bind = OpContractOf(slot.op).bind) {
    bind(uniformData, index, ordinal, slot);
  }
}

// Writes the extended chain's gradient parameter block.
void UploadGradientParams(UniformData* uniformData, const AOTGradientParameters& params) {
  uniformData->setDataOptional(ChainParams::GradientLayoutType.name, params.layoutType);
  uniformData->setDataOptional(ChainParams::GradientBias.name, params.bias);
  uniformData->setDataOptional(ChainParams::GradientScale.name, params.scale);
  uniformData->setDataOptional(ChainParams::GradientColorizerKind.name, params.colorizerKind);
  uniformData->setDataOptional(ChainParams::GradientLeftBorder.name, params.leftBorder);
  uniformData->setDataOptional(ChainParams::GradientRightBorder.name, params.rightBorder);
  uniformData->setDataOptional(ChainParams::GradientStart.name, params.start);
  uniformData->setDataOptional(ChainParams::GradientEnd.name, params.end);
  uniformData->setDataOptional(ChainParams::GradientScale01.name, params.scale01);
  uniformData->setDataOptional(ChainParams::GradientBias01.name, params.bias01);
  uniformData->setDataOptional(ChainParams::GradientScale23.name, params.scale23);
  uniformData->setDataOptional(ChainParams::GradientBias23.name, params.bias23);
  uniformData->setDataOptional(ChainParams::GradientThreshold.name, params.threshold);
  uniformData->setDataOptional(ChainParams::GradientIntervalCount.name, params.intervalCount);
  uniformData->setDataOptional(ChainParams::GradientThresholds1_7.name, params.thresholds1_7);
  uniformData->setDataOptional(ChainParams::GradientThresholds9_13.name, params.thresholds9_13);
  for (size_t index = 0; index < 8; ++index) {
    const auto& pair = ChainParams::GradientPairs[index];
    uniformData->setDataOptional(pair.scale, params.scales[index]);
    uniformData->setDataOptional(pair.bias, params.biases[index]);
  }
}

}  // namespace

// The instruction contract's bind column (declared in Binder.h): each function writes one op's
// per-slot parameter block into the chain arrays.
void BindColorMatrixSlot(UniformData* uniformData, size_t index, int /*ordinal*/,
                         const AOTChainSlot& slot) {
  float matrixData[16];
  float vectorData[4];
  PackAOTColorMatrix(slot.colorMatrix.matrix, matrixData, vectorData);
  uniformData->setArrayElementOptional(ChainParams::SlotColorMatrix.name, index, matrixData);
  uniformData->setArrayElementOptional(ChainParams::SlotColorVector.name, index, vectorData);
}

void BindLumaSlot(UniformData* uniformData, size_t index, int /*ordinal*/,
                  const AOTChainSlot& slot) {
  float lumaThresh[] = {slot.luma.kr, slot.luma.kg, slot.luma.kb, 0.0f};
  uniformData->setArrayElementOptional(ChainParams::SlotLumaThresh.name, index, lumaThresh);
}

void BindAlphaThresholdSlot(UniformData* uniformData, size_t index, int /*ordinal*/,
                            const AOTChainSlot& slot) {
  float lumaThresh[] = {0.0f, 0.0f, 0.0f, slot.alphaThreshold.threshold};
  uniformData->setArrayElementOptional(ChainParams::SlotLumaThresh.name, index, lumaThresh);
}

void BindConstColorSlot(UniformData* uniformData, size_t index, int /*ordinal*/,
                        const AOTChainSlot& slot) {
  const auto& color = slot.constColor.color;
  float colorData[] = {color[0], color[1], color[2], color[3]};
  uniformData->setArrayElementOptional(ChainParams::SlotConstColor.name, index, colorData);
}

void BindAARectCoverageSlot(UniformData* uniformData, size_t /*index*/, int ordinal,
                            const AOTChainSlot& slot) {
  // The AA math evaluates to 0 at the rect coordinates, so outset by 0.5 to interpolate from 0
  // at a half-pixel inset to 1 at a half-pixel outset (same as GLSLRectEffect::onSetData).
  const auto& rect = slot.rectCoverage.rect;
  float rectData[] = {rect[0] - 0.5f, rect[1] - 0.5f, rect[2] + 0.5f, rect[3] + 0.5f};
  uniformData->setArrayElementOptional(ChainParams::CoverageRect.name, static_cast<size_t>(ordinal),
                                       rectData);
}

void BindLocalRectCoverageSlot(UniformData* uniformData, size_t /*index*/, int ordinal,
                               const AOTChainSlot& slot) {
  // The local rect keeps its exact coordinates; the kernel's AA ramp clamps each edge distance to
  // half a pixel after mapping through the device-to-local matrix.
  const auto& params = slot.localRectCoverage;
  float rectData[] = {params.rect[0], params.rect[1], params.rect[2], params.rect[3]};
  uniformData->setArrayElementOptional(ChainParams::CoverageLocalRect.name,
                                       static_cast<size_t>(ordinal), rectData);
  uniformData->setArrayElementOptional(ChainParams::CoverageLocalDeviceToLocal.name,
                                       static_cast<size_t>(ordinal),
                                       MatrixColumnMajor(params.deviceToLocal));
}

void BindRRectCoverageSlot(UniformData* uniformData, size_t /*index*/, int ordinal,
                           const AOTChainSlot& slot) {
  const auto& params = slot.rrectCoverage;
  float rectData[] = {params.rect[0], params.rect[1], params.rect[2], params.rect[3]};
  uniformData->setArrayElementOptional(ChainParams::CoverageRRectRect.name,
                                       static_cast<size_t>(ordinal), rectData);
  uniformData->setArrayElementOptional(ChainParams::CoverageRRectRadiiX.name,
                                       static_cast<size_t>(ordinal), params.radiiX);
  uniformData->setArrayElementOptional(ChainParams::CoverageRRectRadiiY.name,
                                       static_cast<size_t>(ordinal), params.radiiY);
  float antiAliasData[] = {params.antiAlias};
  uniformData->setArrayElementOptional(ChainParams::CoverageRRectAntiAlias.name,
                                       static_cast<size_t>(ordinal), antiAliasData);
  uniformData->setArrayElementOptional(ChainParams::CoverageRRectDeviceToLocal.name,
                                       static_cast<size_t>(ordinal),
                                       MatrixColumnMajor(params.deviceToLocal));
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
    BindConstColorSlot(uniformData, index, static_cast<int>(index), slot);
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
                                                 MatrixColumnMajor(values));
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
  // Coverage slots consume the CoverageRect / CoverageLocalRect arrays and the CoverageRRect*
  // arrays in slot order; the upload assigns the ordinals the same way, so the selector's bits
  // 16-19 match the array element written here. Fill all declared slots (including inactive ones,
  // whose OP_NONE marker protects the static texture prefetch), but never probe the 16 absent
  // indices of a base-chain shader.
  int rectOrdinal = 0;
  int localRectOrdinal = 0;
  int rrectOrdinal = 0;
  const auto* capacity = GetAOTResourceCapacity(program.profile);
  // The declared capacity exceeds the real slot count: the inactive tail carries the OP_NONE
  // marker (its zero selector protects the static texture prefetch), matching the fixed-size
  // storage the shell used to keep.
  AOTChainSlot inactiveSlot = {};
  for (size_t index = 0; index < capacity->instructions; ++index) {
    const auto& slot = index < slotCount ? slots[index] : inactiveSlot;
    UploadChainSlot(fragmentUniformData, index, slot, rectOrdinal, localRectOrdinal, rrectOrdinal);
    if (slot.op == AOTChainOp::AARectCoverage) {
      ++rectOrdinal;
    } else if (slot.op == AOTChainOp::LocalRectCoverage) {
      ++localRectOrdinal;
    } else if (slot.op == AOTChainOp::RRectCoverage) {
      ++rrectOrdinal;
    }
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
      UploadGradientParams(fragmentUniformData, slots[index].gradient);
      break;
    }
    if (slots[index].op == AOTChainOp::PerlinNoise) {
      const auto& noise = slots[index].noise;
      float frequencyStitch[] = {noise.baseFrequencyX, noise.baseFrequencyY,
                                 static_cast<float>(noise.stitchWidth),
                                 static_cast<float>(noise.stitchHeight)};
      int modeOctaves[] = {noise.noiseType, noise.numOctaves, noise.stitchTiles ? 1 : 0,
                           program.noiseSamplerBase};
      fragmentUniformData->setDataOptional(ChainParams::NoiseFrequencyStitch.name, frequencyStitch);
      fragmentUniformData->setDataOptional(ChainParams::NoiseModeOctaves.name, modeOctaves);
      break;
    }
  }
}

std::vector<std::array<int, 4>> EncodeChainSlots(const AOTProgram& program) {
  std::vector<std::array<int, 4>> packed;
  const auto* capacity = GetAOTResourceCapacity(program.profile);
  if (capacity == nullptr) {
    return packed;
  }
  // The declared capacity exceeds the real slot count: the inactive tail carries the OP_NONE
  // marker, exactly as BindChainProgram uploads it — a specialized shader folds the same words.
  int rectOrdinal = 0;
  int localRectOrdinal = 0;
  int rrectOrdinal = 0;
  AOTChainSlot inactiveSlot = {};
  packed.reserve(capacity->instructions);
  for (size_t index = 0; index < capacity->instructions; ++index) {
    const auto& slot = index < program.slots.size() ? program.slots[index] : inactiveSlot;
    packed.push_back(EncodeChainSlotPacked(slot, rectOrdinal, localRectOrdinal, rrectOrdinal));
    if (slot.op == AOTChainOp::AARectCoverage) {
      ++rectOrdinal;
    } else if (slot.op == AOTChainOp::LocalRectCoverage) {
      ++localRectOrdinal;
    } else if (slot.op == AOTChainOp::RRectCoverage) {
      ++rrectOrdinal;
    }
  }
  return packed;
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
  // The two SubsetClampOn gates are deliberately different facts, and
  // AOTUniformABTest.BlendTwoImageFills locks the difference: this fill-leaf gate reads the
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

namespace {

// The 12-float aligned form ParamWriter's Float3x3 overload uploads, built from an IR node's
// row-major get9 storage without going through a Matrix.
std::array<float, 12> AlignedFromRowMajor(const std::array<float, 9>& values) {
  return {values[0], values[3], values[6], 0,         values[1], values[4],
          values[7], 0,         values[2], values[5], values[8], 0};
}

bool IsIdentityRowMajor(const std::array<float, 9>& values) {
  return values[0] == 1.f && values[1] == 0.f && values[2] == 0.f && values[3] == 0.f &&
         values[4] == 1.f && values[5] == 0.f && values[6] == 0.f && values[7] == 0.f &&
         values[8] == 1.f;
}

}  // namespace

void BindTiledSourceLeaf(const AOTTextureParameters& leaf, ParamWriter* writer) {
  if (!leaf.tiledRecipe.has_value()) {
    return;
  }
  const auto& sampling = *leaf.tiledRecipe;
  auto view = leaf.textureProxy != nullptr ? leaf.textureProxy->getTextureView() : nullptr;
  if (view != nullptr) {
    writer->set(TiledSourceModule::ReadSwizzle, AOTReadSwizzleMode(view->getTexture()->format()));
  }
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
  writer->set(TiledSourceModule::Strict, sampling.strict ? 1 : 0);
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
    // The single-interval colorizer child's fields; other colorizers carry none.
    const float start[4] = {params.start[0], params.start[1], params.start[2], params.start[3]};
    writer->set(GradientModule::Start, start);
    const float end[4] = {params.end[0], params.end[1], params.end[2], params.end[3]};
    writer->set(GradientModule::End, end);
    writer->set(GradientModule::ColorizerKind, params.colorizerKind);
  }
}

void BindBlendNode(const AOTBlendParameters& params, ParamWriter* writer) {
  writer->setArrayElement(BlendParams::BlendModeValue, 0, params.blendMode);
  writer->set(BlendParams::ChildType, params.childType);
}

void BindAlphaThresholdNode(float threshold, ParamWriter* writer) {
  writer->setArrayElement(PointwiseOpModule::Threshold, 0, threshold);
  writer->setArrayElement(PointwiseOpModule::OpType, 0, Op::AlphaThreshold);
}

void BindRectClipNode(const AOTRectCoverageParameters& params, ParamWriter* writer) {
  const float localRect[4] = {params.rect[0], params.rect[1], params.rect[2], params.rect[3]};
  writer->set(AnalyticClipModule::LocalRect, localRect);
  // The RectCoverage node is the AA form: the lowering routes hard-edged rects through the
  // rrect node with a zero antiAlias.
  writer->set(AnalyticClipModule::AntiAlias, 1.0f);
  // An identity device-to-local is the device-space form's marker: the kernel reads the rect
  // directly, outset by half a texel for the falloff.
  if (!IsIdentityRowMajor(params.deviceToLocal)) {
    writer->set(AnalyticClipModule::DeviceToLocal, AlignedFromRowMajor(params.deviceToLocal));
  } else {
    const float deviceRect[4] = {params.rect[0] - 0.5f, params.rect[1] - 0.5f,
                                 params.rect[2] + 0.5f, params.rect[3] + 0.5f};
    writer->set(AnalyticClipModule::Rect, deviceRect);
  }
  writer->set(AnalyticClipModule::HasClip, IsIdentityRowMajor(params.deviceToLocal) ? 1 : 3);
}

void BindRRectClipNode(const AOTRRectCoverageParameters& params, ParamWriter* writer) {
  const float localRect[4] = {params.rect[0], params.rect[1], params.rect[2], params.rect[3]};
  writer->set(AnalyticClipModule::LocalRect, localRect);
  const float radiiX[4] = {params.radiiX[0], params.radiiX[1], params.radiiX[2], params.radiiX[3]};
  writer->set(AnalyticClipModule::RadiiX, radiiX);
  const float radiiY[4] = {params.radiiY[0], params.radiiY[1], params.radiiY[2], params.radiiY[3]};
  writer->set(AnalyticClipModule::RadiiY, radiiY);
  writer->set(AnalyticClipModule::AntiAlias, params.antiAlias);
  writer->set(AnalyticClipModule::DeviceToLocal, AlignedFromRowMajor(params.deviceToLocal));
  writer->set(AnalyticClipModule::HasClip, 2);
}

void BindColorMatrixNode(const AOTColorMatrixParameters& params, ParamWriter* writer) {
  float matrixData[16] = {};
  float vectorData[4] = {};
  PackAOTColorMatrix(params.matrix, matrixData, vectorData);
  writer->setArrayElement(PointwiseOpModule::ColorMatrix, 0, matrixData);
  writer->setArrayElement(PointwiseOpModule::ColorVector, 0, vectorData);
  writer->setArrayElement(PointwiseOpModule::OpType, 0, Op::ColorMatrix);
}

void BindLumaNode(const AOTLumaParameters& params, ParamWriter* writer) {
  writer->setArrayElement(PointwiseOpModule::Kr, 0, params.kr);
  writer->setArrayElement(PointwiseOpModule::Kg, 0, params.kg);
  writer->setArrayElement(PointwiseOpModule::Kb, 0, params.kb);
  writer->setArrayElement(PointwiseOpModule::OpType, 0, Op::Luma);
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
    // AOTUniformABTest.BlendTwoImageFills holds a leaf whose view facts say "clamp" while the
    // reference says off. Two gates, two facts — deliberately.
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

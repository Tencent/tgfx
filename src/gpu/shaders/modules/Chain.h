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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/Module.h"

namespace tgfx {

// The pointwise interpreter's body, declared. The one joint dimension is PROFILE: the legacy
// kernel's (TEXTURE_COUNT, HAS_MASK_TEXTURE, RESOURCE_KIND) product has exactly seven reachable
// cells, and several fields exist for a union of two of those dimensions at once
// (CoordTransformMatrix_0 spans "any resource source" and "four plain leaves"), which no per-dim
// gate could express. The seven values keep every gate a single mask:
//
//   PROFILE  0 = plain leaf-free        3 = gradient leaf-free        6 = noise four-leaf
//            1 = plain mask             4 = gradient mask
//            2 = plain four-leaf        5 = gradient four-leaf
//
// The masks below are the value sets of each conditional group. The names are bare (bareNames):
// the module reuses the legacy kernel's interface spellings verbatim, so the runtime's by-name
// writers (the GP's setData, BindChainProgram, the xfer bind) keep working unchanged.
inline constexpr ModuleDim ChainDims[] = {{"PROFILE", 7}};

// The two pool arrays' lengths per PROFILE: the legacy CHAIN_SLOT_CAPACITY was 16 for every plain
// and gradient variant and 32 for the noise profile; the block region is 32 words, so the data
// pool is 16 * 7 + 32 = 144 or 32 * 7 + 32 = 256 entries (the 256 is the one that outgrew a byte).
inline constexpr uint16_t ChainSlotPackedCounts[] = {16, 16, 16, 16, 16, 16, 32};
inline constexpr uint16_t ChainDataCounts[] = {144, 144, 144, 144, 144, 144, 256};

// The PROFILE value sets: leaf slots {2,5,6}, mask-or-leaf {1,2,4,5,6}, gradient {3,4,5},
// tiled2 {0,1,2,6}, noise {6}, resource source {3,4,5,6}, CTM_0 {2,3,4,5,6},
// CTM_1..4 {2,5,6}, CTM_5 {5,6}.
inline constexpr uint8_t ChainLeafSlotsMask = 0x64;
inline constexpr uint8_t ChainMaskOrLeafMask = 0x76;
inline constexpr uint8_t ChainGradientMask = 0x38;
inline constexpr uint8_t ChainTiled2Mask = 0x47;
inline constexpr uint8_t ChainNoiseMask = 0x40;
inline constexpr uint8_t ChainSourceMask = 0x78;
inline constexpr uint8_t ChainCtmZeroMask = 0x7C;
inline constexpr uint8_t ChainCtmOneToFourMask = 0x64;
inline constexpr uint8_t ChainCtmFiveMask = 0x60;

// The unrolled executor: chainRunSlotAt (the shared fragment main) per slot of the variant's own
// capacity, with the caller's three register locals forwarded verbatim.
inline constexpr UnrollSite ChainUnroll = {"chainRunSlotAt",
                                           "result, clipRegister, coverageRootRegister",
                                           /*capacityElement=*/1, /*countElement=*/0};

inline constexpr Element ChainElements[] = {
    // The specialization surface, in the legacy text's first-use order (the ids must line up):
    // SlotCount = id 1, SlotPacked = ids 2..(1 + 4 * capacity), then the three register indices.
    {Element::Kind::Param, ShaderType::Int, "SlotCount", 1, -1, 0, /*spec=*/true},
    {Element::Kind::Param, ShaderType::Int4, "SlotPacked", 1, -1, 0, /*spec=*/true, 0,
     ChainSlotPackedCounts},
    {Element::Kind::Param, ShaderType::Int, "CoverageRootIndex", 1, -1, 0, /*spec=*/true},
    {Element::Kind::Param, ShaderType::Int, "RootIndex", 1, -1, 0, /*spec=*/true},
    {Element::Kind::Param, ShaderType::Int, "ClipCoverageRegister", 1, -1, 0, /*spec=*/true},
    // The core block: the leaf-0 subset pair is unconditional; the color-space xform block is one
    // chain-wide region shared by every profile.
    {Element::Kind::Param, ShaderType::Float4, "Subset"},
    {Element::Kind::Param, ShaderType::Int, "SubsetClampOn"},
    {Element::Kind::Param, ShaderType::Int, "ChainCSFlags"},
    {Element::Kind::Param, ShaderType::Float4, "ChainSrcTF0"},
    {Element::Kind::Param, ShaderType::Float4, "ChainSrcTF1"},
    {Element::Kind::Param, ShaderType::Int, "ChainSrcTFType"},
    {Element::Kind::Param, ShaderType::Float4, "ChainSrcOOTF"},
    {Element::Kind::Param, ShaderType::Mat3, "ChainColorXform"},
    {Element::Kind::Param, ShaderType::Float4, "ChainDstOOTF"},
    {Element::Kind::Param, ShaderType::Float4, "ChainDstTF0"},
    {Element::Kind::Param, ShaderType::Float4, "ChainDstTF1"},
    {Element::Kind::Param, ShaderType::Int, "ChainDstTFType"},
    // The instruction pool and its parameter pool, lengths following PROFILE.
    {Element::Kind::Param, ShaderType::Float4, "ChainData", 1, -1, 0, false, 0, ChainDataCounts},
    // The remaining leaves 1..3 (a chain has either no leaf or all four).
    {Element::Kind::Param, ShaderType::Int4, "LeafReadSwizzles", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Param, ShaderType::Float4, "Subset_1", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Param, ShaderType::Int, "SubsetClampOn_1", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Param, ShaderType::Float4, "Subset_2", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Param, ShaderType::Int, "SubsetClampOn_2", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Param, ShaderType::Float4, "Subset_3", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Param, ShaderType::Int, "SubsetClampOn_3", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Param, ShaderType::Float2, "AlphaStart", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Param, ShaderType::Int, "HasRgbaaa", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Param, ShaderType::Float2, "AlphaStart_1", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Param, ShaderType::Int, "HasRgbaaa_1", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Param, ShaderType::Float2, "AlphaStart_2", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Param, ShaderType::Int, "HasRgbaaa_2", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Param, ShaderType::Float2, "AlphaStart_3", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Param, ShaderType::Int, "HasRgbaaa_3", 1, 0, ChainLeafSlotsMask},
    // The device-space mask: the matrix pair exists whenever the mask can apply (a mask-only
    // variant or the four-leaf slots), the runtime flag and the per-leaf device matrices only with
    // leaves.
    {Element::Kind::Param, ShaderType::Mat3, "DeviceCoordMatrix", 1, 0, ChainMaskOrLeafMask},
    {Element::Kind::Param, ShaderType::Float4, "DeviceMaskSubset", 1, 0, ChainMaskOrLeafMask},
    {Element::Kind::Param, ShaderType::Int, "HasMaskTexture", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Param, ShaderType::Int, "DeviceLeafMask", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Param, ShaderType::Mat3, "DeviceLeafCoordMatrix", 4, 0, ChainLeafSlotsMask},
    // The gradient source (one LUT gradient per chain; the three array recipes are its tiled form).
    {Element::Kind::Param, ShaderType::Int, "GradientLayoutType", 1, 0, ChainGradientMask},
    {Element::Kind::Param, ShaderType::Int, "GradientColorizerKind", 1, 0, ChainGradientMask},
    {Element::Kind::Param, ShaderType::Int, "GradientIntervalCount", 1, 0, ChainGradientMask},
    {Element::Kind::Param, ShaderType::Int, "GradientLUTLeaf", 1, 0, ChainGradientMask},
    {Element::Kind::Param, ShaderType::Int4, "TiledRecipeModes", 3, 0, ChainGradientMask},
    {Element::Kind::Param, ShaderType::Float4, "TiledRecipeSubsets", 3, 0, ChainGradientMask},
    {Element::Kind::Param, ShaderType::Float4, "TiledRecipeClamps", 3, 0, ChainGradientMask},
    {Element::Kind::Param, ShaderType::Float2, "TiledRecipeDimensions", 3, 0, ChainGradientMask},
    // The noise source keeps its fixed position-based subset selectors.
    {Element::Kind::Param, ShaderType::Int4, "NoiseModeOctaves", 1, 0, ChainNoiseMask},
    // The node-based tiling recipes (every profile except the gradient's array form).
    {Element::Kind::Param, ShaderType::Int, "TiledLeafIndex", 1, 0, ChainTiled2Mask},
    {Element::Kind::Param, ShaderType::Int, "TiledLeafIndex2", 1, 0, ChainTiled2Mask},
    {Element::Kind::Param, ShaderType::Int, "TiledModeX", 1, 0, ChainTiled2Mask},
    {Element::Kind::Param, ShaderType::Int, "TiledModeY", 1, 0, ChainTiled2Mask},
    {Element::Kind::Param, ShaderType::Float4, "TiledSubset", 1, 0, ChainTiled2Mask},
    {Element::Kind::Param, ShaderType::Float4, "TiledClamp", 1, 0, ChainTiled2Mask},
    {Element::Kind::Param, ShaderType::Float2, "TiledDimension", 1, 0, ChainTiled2Mask},
    {Element::Kind::Param, ShaderType::Int, "TiledStrict", 1, 0, ChainTiled2Mask},
    {Element::Kind::Param, ShaderType::Int, "TiledModeX2", 1, 0, ChainTiled2Mask},
    {Element::Kind::Param, ShaderType::Int, "TiledModeY2", 1, 0, ChainTiled2Mask},
    {Element::Kind::Param, ShaderType::Float4, "TiledSubset2", 1, 0, ChainTiled2Mask},
    {Element::Kind::Param, ShaderType::Float4, "TiledClamp2", 1, 0, ChainTiled2Mask},
    {Element::Kind::Param, ShaderType::Float2, "TiledDimension2", 1, 0, ChainTiled2Mask},
    {Element::Kind::Param, ShaderType::Int, "TiledStrict2", 1, 0, ChainTiled2Mask},
    // The leaf samplers (all four or none) and the mask slot (four-leaf variants always declare
    // it, a phantom when the chain has no mask; a mask-only variant owns binding 0).
    {Element::Kind::Texture, ShaderType::Float4, "TextureSampler_0", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Texture, ShaderType::Float4, "TextureSampler_1", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Texture, ShaderType::Float4, "TextureSampler_2", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Texture, ShaderType::Float4, "TextureSampler_3", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Texture, ShaderType::Float4, "MaskTextureSampler", 1, 0, ChainMaskOrLeafMask},
    // The leaf coordinates and the procedural source coordinate.
    {Element::Kind::Varying, ShaderType::Float3, "TransformedCoords_0", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Varying, ShaderType::Float3, "TransformedCoords_1", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Varying, ShaderType::Float3, "TransformedCoords_2", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Varying, ShaderType::Float3, "TransformedCoords_3", 1, 0, ChainLeafSlotsMask},
    {Element::Kind::Varying, ShaderType::Float3, "SourceCoords", 1, 0, ChainSourceMask},
    // The coordinate transforms: with a resource source the source owns matrix 0 and the leaves
    // shift to 1..4 (the fifth leaf matrix is declared alongside the four-leaf forms); without
    // one the leaves own 0..3 and 4 is the declared-but-unread spare the legacy block kept.
    {Element::Kind::VertexParam, ShaderType::Mat3, "CoordTransformMatrix_0", 1, -1,
     ChainCtmZeroMask},
    {Element::Kind::VertexParam, ShaderType::Mat3, "CoordTransformMatrix_1", 1, -1,
     ChainCtmOneToFourMask},
    {Element::Kind::VertexParam, ShaderType::Mat3, "CoordTransformMatrix_2", 1, -1,
     ChainCtmOneToFourMask},
    {Element::Kind::VertexParam, ShaderType::Mat3, "CoordTransformMatrix_3", 1, -1,
     ChainCtmOneToFourMask},
    {Element::Kind::VertexParam, ShaderType::Mat3, "CoordTransformMatrix_4", 1, -1,
     ChainCtmOneToFourMask},
    {Element::Kind::VertexParam, ShaderType::Mat3, "CoordTransformMatrix_5", 1, -1,
     ChainCtmFiveMask},
};

inline constexpr Module Chain = {
    "Chain",
    ChainDims,
    1,
    ChainElements,
    static_cast<uint8_t>(sizeof(ChainElements) / sizeof(ChainElements[0])),
    true,
    true,
    /*bareNames=*/true,
    &ChainUnroll};

}  // namespace tgfx

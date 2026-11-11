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

/// The bounded pointwise tail the YUV family carries (the former YUVTextureFillShader's
/// interpreter): the chain's instruction records at the tail's own capacity — the same slot
/// encoding, the same data pool layout — plus the program-level color-space block and the
/// analytic clip contract the composite folds in. bareNames because the shared slot loop and the
/// clip helper read the legacy spellings verbatim, exactly as they did inside the old kernel.
///
/// The tail's capacity is MaxPointwiseSlots (3); a draw with no operators leaves SlotCount at 0
/// and the loop never runs, so the fields cost a uniform block slot but no variant and no pass.
inline constexpr uint16_t YUVChainTailSlotCapacity = 3;

/// Per-slot footprint (see pipeline/chain/pointwise_chain_uniforms.inc): ChainData carries the
/// per-slot parameter blocks plus the shared block region.
inline constexpr uint16_t YUVChainTailBlockWords = 32;
inline constexpr uint16_t YUVChainTailSlotWords = 7;
inline constexpr uint16_t YUVChainTailDataCount =
    YUVChainTailSlotCapacity * YUVChainTailSlotWords + YUVChainTailBlockWords;

inline constexpr Element YUVChainTailElements[] = {
    // The slot loop's instructions and live count.
    {Element::Kind::Param, ShaderType::Int, "SlotCount"},
    {Element::Kind::Param, ShaderType::Int4, "SlotPacked", YUVChainTailSlotCapacity},
    // The program-level color-space block (at most one ColorSpaceXform per tail).
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
    // The per-slot parameter pool.
    {Element::Kind::Param, ShaderType::Float4, "ChainData", YUVChainTailDataCount},
    // The analytic clip contract (the old kernel declared the rect pair inline and the rounded
    // form through the shared include; the composite folds the product in before the XP pass).
    {Element::Kind::Param, ShaderType::Float4, "Rect"},
    {Element::Kind::Param, ShaderType::Int, "HasClip"},
    {Element::Kind::Param, ShaderType::Float4, "LocalRect"},
    {Element::Kind::Param, ShaderType::Float4, "RadiiX"},
    {Element::Kind::Param, ShaderType::Float4, "RadiiY"},
    {Element::Kind::Param, ShaderType::Float, "AntiAlias"},
    {Element::Kind::Param, ShaderType::Mat3, "DeviceToLocal"},
};

inline constexpr Module YUVChainTail = {"YUVChainTail",
                                        nullptr,
                                        0,
                                        YUVChainTailElements,
                                        std::size(YUVChainTailElements),
                                        /*hasVertex=*/false,
                                        /*hasFragment=*/false,
                                        /*bareNames=*/true,
                                        /*unroll=*/nullptr,
                                        /*chainContract=*/true};

}  // namespace tgfx

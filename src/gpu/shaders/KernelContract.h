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

namespace tgfx {

// Single source of truth for the cross-language kernel contracts, consumed by the runtime
// (onSetData writers, the chain op enum) and by shader_build_tool (which validates the GLSL side
// against these constants at bundle build time). Renaming a field or an op code anywhere fails
// the bundle build instead of silently leaving contract fields unwritten or misinterpreting a
// chain slot.
namespace ClipContract {

// The runtime clip contract fields (rrect_clip_uniforms.inc / clip_coverage.inc on the GLSL
// side; the rect/rrect effect writers on the C++ side). Every shader whose fragment uniform
// block carries HasClip must declare all of them.
constexpr const char* Rect = "Rect";
constexpr const char* HasClip = "HasClip";
constexpr const char* LocalRect = "LocalRect";
constexpr const char* RadiiX = "RadiiX";
constexpr const char* RadiiY = "RadiiY";
constexpr const char* AntiAlias = "AntiAlias";
constexpr const char* DeviceToLocal = "DeviceToLocal";

}  // namespace ClipContract

namespace ChainOp {

// The pointwise chain slot op codes, mirrored by the OP_* defines in
// pointwise_chain_eval.inc and the AOTChainOp enum (which anchors to these constants).
constexpr int ColorMatrix = 0;
constexpr int Luma = 1;
constexpr int AlphaThreshold = 2;
constexpr int ColorSpaceXform = 3;
constexpr int None = 4;
constexpr int Texture = 5;
constexpr int ConstColor = 6;
constexpr int Blend = 7;
constexpr int AARectCoverage = 8;
constexpr int Gradient = 9;
constexpr int LocalRectCoverage = 10;
constexpr int RRectCoverage = 11;
constexpr int TexModulate = 12;
constexpr int InputOpaque = 13;
constexpr int MulAlpha = 14;

}  // namespace ChainOp

}  // namespace tgfx

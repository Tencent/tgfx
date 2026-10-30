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

#include "gpu/aot/NodeContract.h"

namespace tgfx {
namespace {

// One row per AOTEffectKind, in enum order. The static_assert pins the row count to the enum, so
// adding a kind without adding a row fails the build instead of silently reading a stale row.
constexpr AOTKindTraits KindTraits[] = {
    {false, true, false, false},  // GeometryColor (graph root, designator)
    {true, false, false, true},   // TextureSource
    {true, false, true, false},   // ColorMatrix
    {true, false, true, false},   // Luma
    {true, false, true, false},   // AlphaThreshold
    {true, false, true, false},   // ColorSpaceXform
    {true, false, false, false},  // ConstColor
    {true, false, false, false},  // Blend
    {true, false, false, true},   // PerlinNoiseSource
    {true, false, false, false},  // RectCoverage
    {true, false, false, true},   // GradientSource
    {true, false, false, false},  // RRectCoverage
    {false, true, false, false},  // GeometryCoverage (graph root, designator)
    {true, true, false, false},   // GeometryColorOpaqueInput
    {true, true, false, false},   // GeometryWhiteInput
    {true, false, false, false},  // InputOpaque
    {true, false, false, false},  // MulAlpha
};

constexpr size_t KindTraitsCount = sizeof(KindTraits) / sizeof(AOTKindTraits);
static_assert(KindTraitsCount == 17,
              "every AOTEffectKind needs exactly one traits row in KindTraits");

}  // namespace

const AOTKindTraits& TraitsOf(AOTEffectKind kind) {
  static constexpr AOTKindTraits fallback = {};
  auto index = static_cast<size_t>(kind);
  return index < KindTraitsCount ? KindTraits[index] : fallback;
}

}  // namespace tgfx

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

#pragma once

#include "gpu/aot/EffectGraph.h"

namespace tgfx {

/**
 * Static facts about one AOTEffectKind. Every call site that used to switch or if-chain over node
 * kinds reads this table instead, so adding a kind means adding one row (a missing row fails the
 * table-size static_assert at compile time) rather than remembering N scattered switch sites.
 */
struct AOTKindTraits {
  /// May appear inside a fused chain pass. The two graph roots (GeometryColor, GeometryCoverage)
  /// are the only kinds excluded; a future non-pointwise kind defaults to false here until its
  /// contract row says otherwise, which keeps it out of fused passes until deliberately admitted.
  bool chainFusable = false;
  /// An input designator: never a chain slot, excluded from pass node lists. Covers all four
  /// designator kinds; the two root designators can never be copied or scheduled anyway.
  bool inputDesignator = false;
  /// One of the four unary pointwise operators sharing the OP_* ABI values.
  bool unaryPointwise = false;
  /// Carries a local-coordinate matrix that a cut-segment copy must precompose with the fill's
  /// coordToLocal (or deviceLeafRemap for a device-sampled texture).
  bool remapsLocalCoords = false;
};

/** Returns the traits row of one kind; the table is the single place a kind's facts live. */
const AOTKindTraits& TraitsOf(AOTEffectKind kind);

}  // namespace tgfx

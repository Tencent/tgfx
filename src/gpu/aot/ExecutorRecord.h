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

#include <optional>
#include "gpu/aot/AOTDrawFacts.h"

namespace tgfx {

/**
 * The values an executor record derives from a draw's facts: the axis sources its family's
 * Compose maps into the permutation domain. Today every migrated family fills its own Inputs
 * struct inside its ExtractFacts; a record fills this shared bag instead, so one matcher can
 * stand in for the pattern half (admission + axis derivation) and the family keeps only the
 * domain mapping.
 */
struct AOTExecutorInputs {
  bool hasUVCoord = false;
  bool hasSubset = false;
  bool hasLocalMask = false;
  int xpType = XP_UNSUPPORTED;
};

/**
 * One dedicated family's admission as data. The record describes the draw shapes the family
 * accepts (geometry kinds, the color expression's shape, the coverage kinds) and how the accepted
 * draw maps onto the family's variant axes; a single matcher interprets it, so adding a fast path
 * is a row instead of a hand-written predicate. The family keeps its Compose (the domain mapping)
 * and its GLSL; the record replaces only the acceptance function.
 *
 * Bit masks index the descriptor enums directly (AOTGeometryKind and CoverageKind are both small
 * enumerations), which keeps the row plain constant data with no ownership.
 */
struct AOTExecutorRecord {
  /// The family this record stands in for (the executor table's row name).
  const char* family = nullptr;
  /// Admitted geometry kinds, as bits of AOTGeometryKind.
  uint32_t geometryKindMask = 0;
  /// Mirrors the family's ServesAlphaOnly: a kernel that does not apply the write swizzle at
  /// output refuses alpha-only targets.
  bool servesAlphaOnly = false;
  /// The color expression must be one bare texture leaf (no operators above it).
  bool colorIsBareTexture = false;
  /// Admitted coverage kinds, as bits of CoverageKind; any other classification (including an
  /// unclassifiable one) is rejected.
  uint8_t coverageKindMask = 0;
  /// The local-mask coverage kind also raises the family's local-mask axis (the chain families'
  /// HAS_LOCAL_MASK); every other admitted kind rides runtime uniforms.
  bool localMaskRaisesAxis = false;
  /// Reject a GP subset over a projective transform: the precompiled subset path transforms the
  /// subset corners without a perspective divide, so such a draw would clamp to wrong bounds.
  bool rejectsPerspectiveSubset = false;
  /// The uv-coord axis is set when the geometry carries no uv matrix (the vertices then read the
  /// uvCoord attribute instead of a transform).
  bool uvCoordFromMissingMatrix = false;
};

/**
 * Runs one record's pattern over a draw's facts: the alpha-only gate, the geometry kinds, the
 * bare texture color shape, the coverage kinds (raising the local-mask axis where the record says
 * so), the perspective-subset guard and the XP axis. Returns the axis sources on acceptance.
 */
std::optional<AOTExecutorInputs> MatchExecutorRecord(const AOTExecutorRecord& record,
                                                     const AOTDrawFacts& facts);

/** The record registered for one family name, or null while the family still matches by hand. */
const AOTExecutorRecord* FindExecutorRecord(const char* family);

}  // namespace tgfx

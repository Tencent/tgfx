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
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/aot/DrawIR.h"
#include "gpu/shaders/ShaderPermutation.h"

namespace tgfx {

class ProgramInfo;

/**
 * A draw's matcher facts, extracted once per match: the geometry and xfer described off the made
 * processors (the matching runs at prepare, where they are the truth), the coverage
 * classification off the carried IR's terms, the target's alpha-only flag, and the live
 * processor-list sizes. The executor-table rows are predicates over this — one extraction
 * replaces every family re-deriving the same facts from the processor tree.
 *
 * The carried IR is the only expression source: record-time lowering makes it total, and the
 * prepare-grown coverage is appended to its terms. Facts that exist only at instance time are
 * read off the made processors (geometry, xfer) or off the IR node's own proxy (a pending
 * multi-plane upload's layout).
 */
struct AOTDrawFacts {
  const DrawIR* ir = nullptr;
  AOTGeometryDesc geometry = {};
  AOTXferKind xfer = AOTXferKind::Unsupported;
  bool alphaOnlyTarget = false;
  size_t colorFPCount = 0;
  size_t fragmentCount = 0;
  /// The draw's classified coverage (the one ClassifyCoverageTerms computes): nullopt when the
  /// terms do not classify. The rows' predicates derive from it — a direct analytic clip is
  /// AARect/RRect, the shared device-mask ABI bit is DeviceMask.
  std::optional<CoverageKind> coverage = std::nullopt;

  /// The position-only rect form the texture and noise fill kernels share: a DefaultGP, or a
  /// QuadPerEdgeAA whose coordinate comes from aPosition (a common color, a uv matrix, no
  /// per-quad subset).
  bool positionOnlyRect() const {
    return geometry.kind == AOTGeometryKind::Default ||
           (geometry.kind == AOTGeometryKind::QuadPerEdgeAA && geometry.hasCommonColor &&
            geometry.hasUVMatrix && !geometry.hasSubset);
  }

  /// The XP_* vocabulary value (the fill families' shared HAS_XP axis).
  int xpType() const {
    switch (xfer) {
      case AOTXferKind::None:
        return XP_NONE;
      case AOTXferKind::DstTexture:
        return XP_DST_TEXTURE;
      case AOTXferKind::FrameBufferFetch:
        return XP_FRAMEBUFFER_FETCH;
      case AOTXferKind::Unsupported:
        break;
    }
    return XP_UNSUPPORTED;
  }
};

AOTDrawFacts ExtractAOTDrawFacts(const ProgramInfo* programInfo);

}  // namespace tgfx

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
 * The live coverage port's raw texture facts — the third channel (see `info`): read off the sole
 * coverage processor, for draws whose coverage the record-time IR cannot express. A rasterized
 * shape's mask grows only at prepare (the staleness guard drops exactly that IR), so no term
 * exists for it; the MaskFill family reads these facts instead. Not a classification: it makes no
 * claim the IR side would have to mirror — the equivalence between the IR terms and the live tree
 * constrains classifications only, never raw instance facts.
 */
struct AOTLiveMaskFacts {
  bool present = false;
  bool alphaOnly = false;
  bool hasSubset = false;
  bool isYUV = false;
  bool hasRGBAAA = false;
  bool hasSampler = false;
};

/**
 * The live color port's gradient facts — the third channel's color-side twin: the sole color
 * processor read off the live tree, for the draws whose coverage grew at prepare (a rasterized
 * instanced shape), whose IR the staleness guard drops together with the color graph it carried.
 * The texture-coverage family reads these instead of the IR's gradient node, so the acceptance
 * has one description whether the draw carries an IR or not.
 */
struct AOTLiveGradientFacts {
  bool present = false;
  bool singleInterval = false;
  bool hasPerspective = false;
};

/**
 * A draw's matcher facts, extracted once per match: the geometry and xfer described off the made
 * processors (the matching runs at prepare, where they are the truth), the coverage
 * classification off the carried IR's terms, the target's alpha-only flag, and the live
 * processor-list sizes. The executor-table rows are predicates over this — one extraction
 * replaces every family re-deriving the same facts from the processor tree.
 *
 * `info` stays as the one instance-time escape hatch: a row reads it only for facts the
 * record-time IR cannot carry (a pending multi-plane upload's eventual layout). It shrinks
 * away as the node contracts absorb the instance facts.
 */
struct AOTDrawFacts {
  const DrawIR* ir = nullptr;
  const ProgramInfo* info = nullptr;
  AOTGeometryDesc geometry = {};
  AOTXferKind xfer = AOTXferKind::Unsupported;
  bool alphaOnlyTarget = false;
  size_t colorFPCount = 0;
  size_t fragmentCount = 0;
  /// The draw's classified coverage (the one ClassifyCoverageTerms computes): nullopt when the
  /// terms do not classify. The rows' predicates derive from it — a direct analytic clip is
  /// AARect/RRect, the shared device-mask ABI bit is DeviceMask.
  std::optional<CoverageKind> coverage = std::nullopt;
  /// Whether the color expression came from the paint (the IR's fact; a draw with an explicit
  /// ConstColorProcessor lowers to the same graph, so this is what keeps the two apart).
  bool colorFromPaint = false;
  /// The sole coverage processor's raw texture facts (the third channel — see
  /// AOTLiveMaskFacts). Extracted whenever the draw carries exactly one coverage FP, live tree or
  /// not: a recorded mask term carries the same facts on the IR side, and the row that reads this
  /// channel accepts exactly the draws the processor-tree walk did.
  AOTLiveMaskFacts liveMask = {};
  /// The sole color processor's gradient facts (the third channel — see AOTLiveGradientFacts),
  /// extracted whenever the draw carries exactly one color FP.
  AOTLiveGradientFacts liveGradient = {};

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

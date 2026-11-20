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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/Program.h"
#include "gpu/aot/plan/AOTPassIR.h"

namespace tgfx {

class ProgramInfo;

/**
 * The chain kernel's graph-side build and match entries: the fused-pass planning the record-time
 * route asks, the texture-tail shape walk the texture families' rules share, the cut-segment
 * builder the multi-pass planner runs, and the per-draw program resolution (the binding table
 * the assembled writer and the variant derivation read). BuildChainProgram (AOTChainInternal.h)
 * is the one build entry; this class owns its callers' entrances.
 */
class AOTChainBuilder {
 public:
  /** Whether the chain kernel's tiled path can execute the given shader wrap mode directly. */
  static bool IsChainCompatibleTiledMode(TiledTextureShaderMode mode);

  /**
   * Plans the single fused pass that carries the whole graph, or returns false when no fused pass
   * can express it. This is the only source of a single-pass plan: a caller that has this answer
   * serves the pass in place and never plans at all.
   */
  static bool PlanFusedPass(const AOTEffectGraph& graph, PassIR* pass);

  /**
   * The color graph's linear texture-tail shape: a single texture leaf fed by the geometry
   * color, with a chain of unary pointwise operators above it (application order, leaf-side
   * first). One walk serves the texture families' table rows — the shared shape authority. Fails
   * on any non-pointwise node (a blend is not unary), an operator the chain walk cannot express,
   * or a shape that is not linear (a second source, an unaccounted branch). Budget and leaf-kind
   * policies stay with the callers.
   */
  struct AOTTextureTailShape {
    const AOTTextureParameters* leaf = nullptr;
    std::vector<AOTNodeID> ops = {};
  };
  static bool CollectTextureTail(const AOTEffectGraph& graph, AOTTextureTailShape* shape);

  /**
   * The source a later pass of a multi-pass plan reads: its predecessor's RGBA intermediate,
   * sampled in device space through uvMatrix (destination fragment coordinates to the
   * intermediate's pixel space).
   */
  static AOTTextureParameters IntermediateSource(std::shared_ptr<TextureProxy> textureProxy,
                                                 const Matrix& uvMatrix);

  /**
   * Builds one pass of a cut plan as a self-contained segment: a copy of the pass's original-graph
   * nodes with every input edge pointing at a cut node (cutPass.materializedInputs) replaced by
   * that cut's texture leaf from intermediates (fed from the opaque-white designator so the
   * intermediate samples raw — the producing pass already applied its own modulation), skipping
   * the cut subtrees' own nodes. A split-off fill pass evaluates its local-coordinate parameters
   * (the gradient coordMatrix, the noise uvMatrix, plain and tiled texture uvMatrices) at top-left
   * device positions while the original draw evaluated them at its own local coordinates, so
   * those are precomposed with coordToLocal; its device-space texture leaves are precomposed with
   * deviceLeafRemap, the composition mapping the fill target's fragment coordinates back onto the
   * destination positions the leaves' uvMatrices address (the device-to-frag flip plus the target
   * placement). A terminal pass runs on the destination and passes identity for both. The
   * returned segment pass carries the profile and kernel its own graph resolves, which is the
   * pass's real fit, so this doubles as the plan-time admission: a plan may hand the cut
   * placeholders (isIntermediate texture parameters) to check the fit before any intermediate
   * exists.
   */
  static bool BuildCutSegment(const AOTEffectGraph& graph, const PassIR& cutPass,
                              const std::vector<AOTTextureParameters>& intermediates,
                              const Matrix& coordToLocal, const Matrix& deviceLeafRemap,
                              AOTEffectGraph* segment, PassIR* pass);

  /**
   * The draw's compiled chain program, resolved at most once: a plan pass or an earlier match
   * already carries it on the IR, and the first caller that needs it builds it there (the
   * single-pass decomposition plans the color side, the classified coverage terms merge into the
   * program) and memoizes it. The program is the graph layout's binding table — the sampler
   * children, the leaf records and the profile the writer and the variant derivation read; the
   * operators themselves are planned per draw by the assembled walk. Returns null when the
   * expression does not reduce onto the chain kernel (a missing IR, an over-budget shape, a
   * coverage form the chain cannot carry).
   */
  static const AOTProgram* ResolveChainProgram(const ProgramInfo* programInfo);
};
}  // namespace tgfx

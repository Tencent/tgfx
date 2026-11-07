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

#include <cstdint>
#include <string>
#include <vector>
#include "gpu/aot/AOTResourceProfile.h"
#include "gpu/aot/chain/AOTPassIR.h"
#include "gpu/aot/ir/EffectGraph.h"

namespace tgfx {
class FragmentProcessor;
class ProgramInfo;

/**
 * Result of statically analyzing whether one effect axis (color or coverage) can be reduced onto
 * the existing AOT kernel basis. This is a pure feasibility classification — it never renders and
 * never mutates state — used by the offline decomposition-coverage audit to quantify, per
 * mechanism, how many currently-unmatched draws each future decomposition step would recover.
 */
enum class AOTDecomposeOutcome {
  /// The axis has no fragment processors (e.g. an empty coverage chain). Nothing to decompose.
  Trivial,
  /// Lower + Decompose succeed (so AOTPlanner::Validate accepts the plan): a production
  /// route submits it (matching the precompiled artifact is a separate step).
  FusablePointwise,
  /// Some fragment processor in the tree has no AOT lowering yet. blockingProcessor names the first
  /// one reported by the actual lowering call chain — the actionable "add lowerToAOT for X" signal.
  BlockedByLowering,
  /// The chain lowers, but the current Decompose rules do not yet cover its
  /// shape — the "grow the decomposer" signal.
  UnsupportedShape,
};

/** Per-axis feasibility analysis produced by AOTEffectDecomposer::Analyze. */
struct AOTAxisAnalysis {
  AOTDecomposeOutcome outcome = AOTDecomposeOutcome::Trivial;
  /// Class name of the first fragment processor lacking AOT lowering (BlockedByLowering only).
  std::string blockingProcessor = {};
  /// Number of top-level fragment processors on this axis.
  int processorCount = 0;
  /// Number of texture-sampling leaves in the tree — the sampler-budget input for a fused kernel.
  int textureLeafCount = 0;
};

/** Feasibility analysis of both effect axes of a single draw. */
struct AOTDecomposeAnalysis {
  AOTAxisAnalysis color = {};
  AOTAxisAnalysis coverage = {};
};

class AOTEffectDecomposer {
 public:
  /**
   * Decomposes the graph into a multi-pass execution plan: the blend cut planner first, then the
   * linear chain planner. Both materialize, so the result always needs a task. Callers that serve
   * a fused pass in place must ask AOTChainBuilder::PlanFusedPass first.
   */
  static bool DecomposeMultiPass(const AOTEffectGraph& graph, Plan* plan);

  /**
   * Decomposes the graph into an execution plan, preferring fusion: a single fused pass is chosen
   * whenever the graph's shape allows it, otherwise the multi-pass planners answer.
   */
  static bool Decompose(const AOTEffectGraph& graph, Plan* plan);

  /**
   * Statically classifies whether the color and coverage effect axes of the given draw can be
   * reduced onto the existing AOT kernel basis. Pure analysis: it runs Lower / Decompose
   * but never renders, allocates GPU resources, or mutates the ProgramInfo. Used by the
   * offline decomposition-coverage audit to quantify recovery potential per mechanism and to name
   * the specific fragment processors whose AOT lowering is still missing.
   *
   * @param programInfo the draw to analyze; may be null (returns an all-Trivial analysis).
   * @return the per-axis feasibility classification.
   */
  static AOTDecomposeAnalysis Analyze(const ProgramInfo* programInfo);
};

}  // namespace tgfx

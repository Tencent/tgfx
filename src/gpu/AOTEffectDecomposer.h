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
#include "gpu/AOTEffect.h"
#include "gpu/AOTResourceProfile.h"

namespace tgfx {
class FragmentProcessor;
class ProgramInfo;

enum class AOTKernelKind {
  // A single fused pass that evaluates a pointwise DAG (texture/const-color leaves combined by
  // color-matrix, luma and blend ops) via a runtime opcode chain. Produced by the pointwise-DAG
  // planner; its kernel artifact is PointwiseChainShader.
  PointwiseChain,
  // A plain or device-space texture source followed by bounded pointwise operator slots, matching
  // PointwiseTailShader. Produced by DecomposeLinearPointwiseTail for linear chains the DAG
  // planner refuses (device-space sources, chains over the chain kernel's budgets); a chain over
  // the tail's slot budget is split into several passes joined by materialized intermediates.
  PointwiseTail,
  // A procedural-noise source (PerlinNoiseFragmentProcessor) optionally followed by up to three
  // folded pointwise/const/blend operator slots, matching PerlinNoiseFillShader. Produced by
  // DecomposePerlinNoiseChain; the plan is always a single pass — shapes beyond the slot budget
  // are rejected rather than split.
  PerlinNoiseFill,
  // A YUV video source (TextureEffect over an I420/NV12 proxy) optionally followed by up to
  // three bounded pointwise operator slots, matching YUVTextureFillShader's color-grade tail.
  // Produced by DecomposeYUVChain; single pass, over-budget shapes are rejected.
  YUVTextureFill,
  // Extended-resource DAGs use the independent PointwiseGraphShader profiles.
  PointwiseGraph,
};

inline bool IsAOTPointwiseDAGKernel(AOTKernelKind kernel) {
  return kernel == AOTKernelKind::PointwiseChain || kernel == AOTKernelKind::PointwiseGraph;
}

/**
 * One kernel invocation of a plan. A plan with several passes is a linear PointwiseTail chain
 * split at the tail kernel's slot budget: every pass but the last renders into an intermediate
 * texture (materializesOutput), and each later pass reads its predecessor's texture as its source
 * (dependencies holds that predecessor's index), so its nodes are operator slots only.
 */
struct AOTPassDescriptor {
  AOTKernelKind kernel = AOTKernelKind::PointwiseChain;
  std::vector<AOTNodeID> nodes = {};
  std::vector<uint32_t> dependencies = {};
  AOTNodeID output = AOTNodeID::Invalid();
  bool materializesOutput = false;
  AOTResourceProfile resourceProfile = AOTResourceProfile::Automatic;
};

struct AOTEffectPlan {
  std::vector<AOTPassDescriptor> passes = {};
  AOTNodeID output = AOTNodeID::Invalid();
};

/**
 * Result of statically analyzing whether one effect axis (color or coverage) can be reduced onto
 * the existing AOT kernel basis. This is a pure feasibility classification — it never renders and
 * never mutates state — used by the offline decomposition-coverage audit to quantify, per
 * mechanism, how many currently-unmatched draws each future decomposition step would recover.
 */
enum class AOTDecomposeOutcome {
  /// The axis has no fragment processors (e.g. an empty coverage chain). Nothing to decompose.
  Trivial,
  /// Lower + Decompose succeed (so AOTPlanExecutor::CanExecute accepts the plan): a production
  /// route submits it (matching the precompiled artifact is a separate step).
  FusablePointwise,
  /// Some fragment processor in the tree has no AOT lowering yet. blockingProcessor names the first
  /// one reported by the actual lowering call chain — the actionable "add lowerToAOT for X" signal.
  BlockedByLowering,
  /// The chain lowers, but the current Decompose rules do not yet cover its
  /// shape — the "grow the decomposer" signal.
  UnsupportedShape,
  /// The color chain plans, but the attempted route still refused the draw: it carries coverage
  /// that route cannot apply. Recorded by the on-screen color-only route only.
  NotRoutable,
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

/**
 * Static verdict on whether a NoMatchingRule draw would become matchable if its coverage FPs were
 * folded into the color chain at program-creation time (the decomposed-program route). Pure
 * feasibility classification: it never renders and never mutates state, and is used by the
 * offline audit to size that route before enabling it.
 */
enum class AOTFoldRouteOutcome {
  /// The draw has no coverage FPs, so there is nothing to fold.
  NotApplicable,
  /// Coverage FPs exist but the xfer processor blends with the destination (PorterDuff), where
  /// folding coverage into the color chain would change pixels, so the draw must stay as-is.
  FoldBlockedByXP,
  /// Some processor in the folded chain has no AOT lowering yet.
  BlockedByLowering,
  /// The folded chain lowers, but no planner rule covers its shape.
  UnsupportedShape,
  /// The plan is executable, but the fused kernels' matchers do not support this draw's GP.
  GPIncompatible,
  /// The draw would decompose, execute, and match if the route were enabled.
  Routable,
};

const char* AOTFoldRouteOutcomeName(AOTFoldRouteOutcome outcome);

class AOTEffectDecomposer {
 public:
  /** Lowers a processor chain and optionally reports the first processor missing an implementation. */
  static bool Lower(const std::vector<const FragmentProcessor*>& processors, AOTEffectGraph* graph,
                    std::string* blockingProcessor = nullptr);

  /**
   * Decomposes the graph into an execution plan, preferring fusion: a single fused pass is chosen
   * whenever the graph's shape allows it.
   */
  static bool Decompose(const AOTEffectGraph& graph, AOTEffectPlan* plan);

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

  /**
   * Pure analysis of a single color-processor chain (no ProgramInfo needed), classifying whether
   * the chain reduces onto the current kernel basis. Same classification the audit uses, exposed
   * separately so the runtime decomposition route can record why a draw fell back to the plain
   * route at op-construction time, before any ProgramInfo exists.
   */
  static AOTAxisAnalysis AnalyzeChain(const std::vector<const FragmentProcessor*>& processors);

  /**
   * Statically answers whether the given draw (assumed to have missed every matcher rule) would
   * become matchable if its coverage FPs were folded into the color chain at program-creation
   * time. Runs Lower / Decompose on the concatenated
   * color+coverage processor list and checks the GP against the fused kernels' supported set.
   * Pure analysis: no rendering, no GPU allocation, no ProgramInfo mutation.
   *
   * @param programInfo the draw to analyze; may be null (returns NotApplicable).
   * @return the fold-route verdict.
   */
  static AOTFoldRouteOutcome AnalyzeFoldRoute(const ProgramInfo* programInfo);
};

}  // namespace tgfx

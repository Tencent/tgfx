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
  // planner, and for every pass of a linear chain the multi-pass planner splits; its kernel
  // artifact is PointwiseChainShader.
  PointwiseChain,
  // A procedural-noise source (PerlinNoiseFragmentProcessor) optionally followed by up to three
  // folded pointwise/const/blend operator slots, matching PerlinNoiseFillShader. Produced by
  // DecomposePerlinNoiseChain; the plan is always a single pass — shapes beyond the slot budget
  // are rejected rather than split.
  PerlinNoiseFill,
  // A YUV video source (TextureEffect over an I420/NV12 proxy) optionally followed by up to
  // three bounded pointwise operator slots, matching YUVTextureFillShader's color-grade tail.
  // Produced by DecomposeYUVChain; single pass, over-budget shapes are rejected.
  YUVTextureFill,
  // Extended-resource DAGs use the merged PointwiseChainShader resource kinds.
  PointwiseGraph,
};

inline bool IsAOTPointwiseDAGKernel(AOTKernelKind kernel) {
  return kernel == AOTKernelKind::PointwiseChain || kernel == AOTKernelKind::PointwiseGraph;
}

// The pass cap of a cut plan: every pass but the last is one materialization edge, so this bounds
// both the intermediate memory and the planning work; a shape needing more passes is refused.
inline constexpr size_t MaxCutPlanPasses = 8;

/**
 * One kernel invocation of a plan. A plan with several passes is a linear chain split into chain
 * passes, each fitting one chain pass: every pass but the last renders into an intermediate RGBA8
 * texture (materializesOutput), and each later pass reads its predecessor's texture in device space
 * as its source (dependencies holds that predecessor's index), so its nodes are operators only.
 *
 * The cut form instead replaces over-budget blend operand subtrees one cut at a time: each pass is a
 * self-contained fused chain pass over its own original-graph nodes, reading the earlier cuts as
 * texture leaves — materializedInputs[i] names the original-graph node that dependencies[i]'s output
 * texture replaces. The passes are in topological order and the last one is the terminal draw (the
 * only pass that runs on the destination). A pass's kernel and profile are resolved by its segment
 * build; a plan-time fit check runs the same build over placeholder intermediate leaves.
 */
struct AOTPassDescriptor {
  AOTKernelKind kernel = AOTKernelKind::PointwiseChain;
  std::vector<AOTNodeID> nodes = {};
  std::vector<uint32_t> dependencies = {};
  AOTNodeID output = AOTNodeID::Invalid();
  bool materializesOutput = false;
  AOTResourceProfile resourceProfile = AOTResourceProfile::Automatic;
  std::vector<AOTNodeID> materializedInputs = {};
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
   * whenever the graph's shape allows it. singlePassOnly skips the cut planner — its plans always
   * start with a materializing pass and always need a task — for callers that only consume
   * single-pass plans (the prepare-time fused serving) and would otherwise run its iterative
   * segment builds only to discard the result.
   */
  static bool Decompose(const AOTEffectGraph& graph, AOTEffectPlan* plan,
                        bool singlePassOnly = false);

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

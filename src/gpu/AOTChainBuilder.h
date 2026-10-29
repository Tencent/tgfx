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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "gpu/AOTEffectDecomposer.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {

/**
 * Rebuilds concrete FragmentProcessors from a decomposed AOT plan: the fused chain kernel for
 * PointwiseChain/PointwiseGraph passes (including every pass of a multi-pass linear plan, through
 * BuildLinearSegment), the perlin kernel for PerlinNoiseFill and the YUV kernel for
 * YUVTextureFill. Used by AOTPlanExecutor's task assembly
 * and by the direct-draw decomposition route in StandardDrawOp::prepare.
 */
class AOTChainBuilder {
 public:
  /** Whether the chain kernel's tiled path can execute the given shader wrap mode directly. */
  static bool IsChainCompatibleTiledMode(TiledTextureShaderMode mode);

  /**
   * Pure admission of a PointwiseChain/PointwiseGraph pass without draw coverage. Runs the same
   * slot encoding, closure, budget, profile and register checks BuildChainProcessor runs, but
   * creates no processor. AOTPlanExecutor::CanExecute relies on it, so a pass admitted here can
   * only fail at construction when a resource cannot be wrapped (or when the draw's coverage adds
   * slots this check cannot see).
   */
  static bool PlanChainPass(const AOTEffectGraph& graph, const AOTPassDescriptor& pass);

  /**
   * Same planning as PlanChainPass, ignoring the pass kernel: reports the resource profile (and
   * so the kernel family) the pass needs. Used by the DAG planner to pick the kernel; returns
   * false when no profile can carry the pass.
   */
  static bool ResolveChainProfile(const AOTEffectGraph& graph, const AOTPassDescriptor& pass,
                                  AOTResourceProfile* profile);

  /**
   * Rebuilds the color processor of one self-contained plan pass: the fused chain/graph processor
   * without draw coverage, or the YUV or perlin processor with the pass's operators folded into
   * its slot records. Returns nullptr when the pass shape cannot be rebuilt. A pass of a
   * multi-pass plan is not self-contained (it reads its predecessor's output); build it through
   * BuildLinearSegment first.
   */
  static PlacementPtr<FragmentProcessor> BuildFPForPass(BlockAllocator* allocator,
                                                        const AOTEffectGraph& graph,
                                                        const AOTPassDescriptor& pass);

  /**
   * Builds the self-contained graph one pass of a multi-pass linear plan executes: a texture
   * source with the given parameters (the plan's source for the first pass, the predecessor's
   * intermediate thereafter), then copies of the unary operators. The first source consumes the
   * geometry color; later sources consume the white designator so the original geometry alpha is
   * not applied again at the terminal draw. On success *pass fits its selected kernel family.
   */
  static bool BuildLinearSegment(const AOTEffectGraph& graph,
                                 const std::vector<AOTNodeID>& operators,
                                 const AOTTextureParameters& source, AOTEffectGraph* segment,
                                 AOTPassDescriptor* pass, bool firstPass = true);

  /**
   * The source a later pass of a multi-pass linear plan reads: its predecessor's RGBA
   * intermediate, sampled in device space through uvMatrix (destination fragment coordinates to
   * the intermediate's pixel space).
   */
  static AOTTextureParameters IntermediateSource(std::shared_ptr<TextureProxy> textureProxy,
                                                 const Matrix& uvMatrix);

  /**
   * Builds the main pass of a materialized-operand plan: a copy of the original graph's DAG with
   * every input edge pointing at the materialized operand subtree replaced by one texture leaf
   * (fed from the opaque-white designator so the intermediate samples raw — the operand pass
   * already applied its own modulation), skipping the subtree's own nodes. The returned segment
   * pass carries the profile and kernel its own graph resolves, which is the pass's real fit.
   */
  static bool BuildDAGSegment(const AOTEffectGraph& graph, const std::vector<AOTNodeID>& mainNodes,
                              AOTNodeID materializedOperand,
                              const AOTTextureParameters& intermediateSource,
                              AOTEffectGraph* segment, AOTPassDescriptor* pass);

  /**
   * Builds the operand pass of a materialized-operand plan: a copy of the operand subtree with its
   * local-coordinate parameters (the gradient coordMatrix, the noise uvMatrix) precomposed with
   * the given mapping, because the split-off fill evaluates at top-left device positions while the
   * original draw evaluated at its own local coordinates. The mapping is the draw's
   * device-to-local matrix (identity for an offscreen fill, whose coordinates already match).
   * Coverage nodes keep their device-space parameters: the fill's positions are already the
   * device ones.
   */
  static bool BuildOperandSegment(const AOTEffectGraph& graph, const AOTPassDescriptor& operandPass,
                                  const Matrix& deviceToLocal, AOTEffectGraph* segment,
                                  AOTPassDescriptor* pass);

  /**
   * Flattens a single-pass PointwiseChain/PointwiseGraph plan into the shared processor, or returns
   * nullptr if the pass shape exceeds the selected kernel's capabilities. The processor carries
   * the whole DAG in uniforms and resolves to the corresponding independent shader family.
   *
   * coverageFPs, when non-empty, folds the draw's coverage into the chain. A single FP uses the
   * narrow forms first (a bare device-space AA RectEffect becomes an AARectCoverage slot, an alpha-only
   * DeviceSpaceTextureEffect becomes the mask child, Compose(mask, rect) becomes both); anything
   * else is lowered as a general pointwise coverage subtree rooted at the GP coverage, which the
   * kernel multiplies into the color result instead of the plain coverage modulation. Two FPs are
   * accepted in the form [lowerable subtree, alpha-only device mask], folding to subtree + mask
   * child. Any other coverage form returns nullptr.
   */
  static PlacementPtr<FragmentProcessor> BuildChainProcessor(
      BlockAllocator* allocator, const AOTEffectGraph& graph, const AOTPassDescriptor& pass,
      const std::vector<const FragmentProcessor*>& coverageFPs = {},
      bool coverageLeafFromUVCoord = false);
};
}  // namespace tgfx

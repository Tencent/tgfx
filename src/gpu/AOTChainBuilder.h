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
#include "gpu/aot/DrawIR.h"
#include "gpu/aot/Program.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {

class ProgramInfo;

/**
 * The compiled result of one chain pass: the data-only program plus the build-time leaf plan the
 * processor construction reads (graph node pointers for the texture leaves, the LUT gradient and
 * noise parameter records, and the device-space leaf indices). The shell-free serving path
 * replaces the leaf plan with the program's binding table.
 */
struct AOTChainCompile {
  AOTProgram program = {};
  // Texture leaves in sampler order: leafNodes[k] pairs with slot k and TextureSampler_k.
  std::vector<const AOTEffectNode*> leafNodes = {};
  const AOTGradientParameters* lutGradient = nullptr;
  const AOTPerlinNoiseParameters* noise = nullptr;
  // Indices into leafNodes of the device-space leaves, in leaf order.
  std::vector<int> deviceLeafIndices = {};
};

/**
 * Rebuilds concrete FragmentProcessors from a decomposed AOT plan: the fused chain kernel for
 * PointwiseChain/PointwiseGraph passes (a multi-pass plan's passes are built as cut segments
 * through BuildCutSegment), the perlin kernel for PerlinNoiseFill and the YUV kernel for
 * YUVTextureFill. Used by AOTPlanExecutor's task assembly and by the two re-expressing
 * kernels' per-draw views of an unmodified ProgramInfo (BuildChainProcessorForInfo /
 * BuildPerlinProcessorForInfo).
 */
class AOTChainBuilder {
 public:
  /** Whether the chain kernel's tiled path can execute the given shader wrap mode directly. */
  static bool IsChainCompatibleTiledMode(TiledTextureShaderMode mode);

  /**
   * Pure admission of a PointwiseChain/PointwiseGraph pass without draw coverage. Runs the same
   * slot encoding, closure, budget, profile and register checks BuildChainProcessor runs, but
   * creates no processor — so plan-time fit and construction fit cannot drift.
   *
   * checkKernel also verifies the pass's declared kernel against the profile the pass needs (the
   * admission form, which AOTPlanExecutor::CanExecute relies on); without it the pass kernel is
   * ignored and only the fit is checked. outProfile, when not null, receives the resource profile
   * the pass needs — the planner's form, which picks the kernel from the resolved profile.
   */
  static bool PlanChainPass(const AOTEffectGraph& graph, const AOTPassDescriptor& pass,
                            bool checkKernel, AOTResourceProfile* outProfile = nullptr);

  /**
   * Compiles one chain pass — the color graph's pass plus, when given, the draw's classified
   * coverage terms — into the register-allocated program. This is the single admission entry:
   * the plan-time checks (AOTPlanExecutor::CanExecute, the DAG planner through PlanChainPass)
   * and the processor construction run exactly this pipeline, so a plan-time fit and a
   * construction fit cannot drift. coverageTerms is null or empty for a plan pass (plan passes
   * carry no draw coverage). checkKernel additionally verifies the pass's declared kernel
   * against the profile the pass resolves.
   */
  static bool Compile(const AOTEffectGraph& graph, const AOTPassDescriptor& pass,
                      const std::vector<AOTCoverageTerm>* coverageTerms, bool checkKernel,
                      AOTChainCompile* out);

  /**
   * Rebuilds the fused chain processor of one plan pass from its pass IR: the cut segment graph
   * already carries the intermediate leaves and the carried pass descriptor is the one the
   * planner fit, so nothing is re-planned. This is the plan-pass serving path — the executor
   * stores each pass as this IR instead of a processor, and the fill passes and the terminal
   * draw materialise the processor here, once per prepare. Returns nullptr when the IR is not a
   * chain pass or the pass does not compile.
   */
  static PlacementPtr<FragmentProcessor> BuildPassProcessor(BlockAllocator* allocator,
                                                            const DrawIR& passIR);

  /**
   * Rebuilds the color processor of one self-contained plan pass: the fused chain/graph processor
   * without draw coverage, or the YUV or perlin processor with the pass's operators folded into
   * its slot records. Returns nullptr when the pass shape cannot be rebuilt. A pass of a
   * multi-pass plan is not self-contained (it reads its predecessors' outputs); build it through
   * BuildCutSegment first. Only the linear kernels (YUV, perlin) are served here: chain passes
   * are carried as pass IRs and rebuilt through BuildPassProcessor.
   */
  static PlacementPtr<FragmentProcessor> BuildFPForPass(BlockAllocator* allocator,
                                                        const AOTEffectGraph& graph,
                                                        const AOTPassDescriptor& pass);

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
  static bool BuildCutSegment(const AOTEffectGraph& graph, const AOTPassDescriptor& cutPass,
                              const std::vector<AOTTextureParameters>& intermediates,
                              const Matrix& coordToLocal, const Matrix& deviceLeafRemap,
                              AOTEffectGraph* segment, AOTPassDescriptor* pass);

  /**
   * Flattens a single-pass PointwiseChain/PointwiseGraph plan into the shared processor, or returns
   * nullptr if the pass shape exceeds the selected kernel's capabilities. The processor carries
   * the whole DAG in uniforms and resolves to the corresponding independent shader family.
   *
   * coverageFPs, when non-empty, is classified by the shared coverage classifier (the same one
   * LowerDraw uses): analytic rect/rrect leaves ride the clip-coverage register, an alpha-only
   * device-space texture becomes the mask child, and anything else lowers as a general coverage
   * subtree rooted at the GP coverage. coverageLeafFromUVCoord marks the trailing FP as the
   * AtlasText glyph leaf a caller synthesized from the geometry processor. More than two coverage
   * FPs returns nullptr.
   */
  static PlacementPtr<FragmentProcessor> BuildChainProcessor(
      BlockAllocator* allocator, const AOTEffectGraph& graph, const AOTPassDescriptor& pass,
      const std::vector<const FragmentProcessor*>& coverageFPs = {},
      bool coverageLeafFromUVCoord = false);

  /**
   * Re-expresses a draw's whole effect expression (its color processors, its coverage processors,
   * and the glyph leaf an AtlasText geometry processor contributes) as the fused chain processor,
   * without touching the draw: one LowerDraw produces the color graph and the classified coverage
   * terms, the single-pass decomposition plans the color side, and the terms merge into the
   * processor. This is the interpreter's view of an unmodified ProgramInfo: the matcher uses it to
   * decide whether the chain kernel serves the draw, and the chain program writer rebuilds it per
   * draw to upload the values. Returns nullptr when the expression does not reduce onto the chain
   * kernel (an unsupported processor, an over-budget shape, a coverage form the chain cannot
   * carry).
   */
  static PlacementPtr<FragmentProcessor> BuildChainProcessorForInfo(BlockAllocator* allocator,
                                                                    const ProgramInfo* programInfo);

  /**
   * Re-expresses a coverage-free draw's color expression as the perlin kernel processor with its
   * trailing operators (pointwise ops, const colors, const blends) folded into the slot records,
   * without touching the draw. This is the perlin kernel's view of an unmodified ProgramInfo: the
   * family's matcher uses it as the acceptance oracle and its program writer rebuilds it per draw
   * to upload the values. Returns nullptr when the expression does not reduce onto a single
   * perlin pass (a non-perlin root, an over-slot shape, any coverage processor).
   */
  static PlacementPtr<FragmentProcessor> BuildPerlinProcessorForInfo(
      BlockAllocator* allocator, const ProgramInfo* programInfo);
};
}  // namespace tgfx

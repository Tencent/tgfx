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
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/Program.h"
#include "gpu/aot/plan/AOTEffectDecomposer.h"
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
 * Compiles the interpreter's programs from a decomposed AOT plan: the fused chain kernel for
 * PointwiseChain/PointwiseGraph passes (a multi-pass plan's passes are built as cut segments
 * through BuildCutSegment) and the YUV family's color-grade tail. Used by AOTPlanner's
 * task assembly and by the re-expressing kernel's per-draw view of an unmodified ProgramInfo
 * (CompileChainProgramForInfo / CompileYUVProgramForInfo).
 */
class AOTChainBuilder {
 public:
  /** Whether the chain kernel's tiled path can execute the given shader wrap mode directly. */
  static bool IsChainCompatibleTiledMode(TiledTextureShaderMode mode);

  /**
   * A fit probe, not an admission: runs the chain planner (slot encoding, closure, budget,
   * profile, register allocation) without building the binding table. The decomposer uses it to
   * decide whether a cut fits before any program exists; every serving decision goes through
   * Compile below, so a probe success never stands in for a build success.
   *
   * checkKernel also verifies the pass's declared kernel against the profile the pass needs;
   * without it the pass kernel is ignored and only the fit is checked. outProfile, when not null,
   * receives the resource profile the pass needs — the planner's form, which picks the kernel from
   * the resolved profile.
   */
  static bool FitsChainPlan(const AOTEffectGraph& graph, const PassIR& pass, bool checkKernel,
                            AOTResourceProfile* outProfile = nullptr);

  /**
   * The one compile entry: the pass's register-allocated program plus, when given, the draw's
   * classified coverage terms and their sampler binding table. Admission
   * (AOTPlanner::Validate) and every build site call exactly this, so an admitted pass is
   * by construction a buildable one — the binding stage's resource failures surface at admission
   * instead of at prepare. coverageTerms is null or empty for a plan pass (plan passes carry no
   * draw coverage). checkKernel additionally verifies the pass's declared kernel against the
   * profile the pass resolves.
   */
  static bool Compile(const AOTEffectGraph& graph, const PassIR& pass,
                      const std::vector<AOTCoverageTerm>* coverageTerms, bool checkKernel,
                      AOTChainCompile* out);

  /**
   * Compiles one plan pass's IR into the fused chain program and its binding table, without
   * building the pass's processor: the AOT route resolves a pass draw from the program alone.
   * Returns false when the IR is not a chain pass or the pass does not compile.
   */
  static bool CompilePassProgram(const DrawIR& passIR, AOTProgram* out);

  /**
   * Validates an encoded instruction array (register domains, opcodes, selector fields, leaf
   * layout, per-kind parameter blocks) and resolves its resource profile against the profile
   * capacities, without looking at any child processor. The planner runs it while planning
   * (AOTPlanner::Validate) and the compile runs it again on the same data, so planning and
   * compilation cannot disagree on a slot rule or a budget. leafCount is the number of real DAG
   * texture leaves (slots [0, leafCount) must be OP_TEXTURE), hasLUT whether a LUT gradient child
   * follows them, and tiledLeafIndices the leaves carrying a shader-side tiling recipe. On success
   * *profile holds the resolved (non-Automatic) profile.
   */
  static bool ValidateSlots(const std::vector<AOTChainSlot>& slots, size_t rootSlot,
                            int coverageRootSlot, int clipCoverageRegister, size_t leafCount,
                            bool hasLUT, const std::vector<int>& tiledLeafIndices,
                            AOTResourceProfile* profile);

  /**
   * The color graph's linear texture-tail shape: a single texture leaf fed by the geometry
   * color, with a chain of unary pointwise operators above it (application order, leaf-side
   * first). One walk serves the texture families' table rows and the YUV tail's checks below —
   * the shared shape authority. Fails on any non-pointwise node (a blend is not unary), an
   * operator the slot encoding cannot express, or a shape that is not linear (a second source,
   * an unaccounted branch). Budget and leaf-kind policies stay with the callers.
   */
  struct AOTTextureTailShape {
    const AOTTextureParameters* leaf = nullptr;
    std::vector<AOTNodeID> ops = {};
  };
  static bool CollectTextureTail(const AOTEffectGraph& graph, AOTTextureTailShape* shape);

  /**
   * The servable YUV tail of a color graph: the multi-plane source leaf with the chain of
   * bounded unary pointwise operators above it (application order, leaf-side first). One walk
   * serves the family's matching, the plan's admission and the tail's compilation, so the three
   * cannot disagree about what a servable tail is. Fails on any non-pointwise node (a blend is
   * not unary), an operator the slot encoding cannot express, a second color-space transform
   * (the kernel's CS parameters are one shared block, like the interpreter's), a non-YUV /
   * non-Plain leaf, or a tail over MaxPointwiseSlots.
   */
  struct AOTYUVTailShape {
    const AOTTextureParameters* leaf = nullptr;
    std::vector<AOTNodeID> ops = {};
  };
  static bool CollectYUVTail(const AOTEffectGraph& graph, AOTYUVTailShape* shape);

  /**
   * Compiles the YUV family's color-grade tail from the draw's carried IR into chain
   * instruction records (the same slot encoding the interpreter reads): the operators above
   * the multi-plane source leaf, in application order. The terminal needs no rebuilt processor
   * — the family matches off the IR and this program drives the tail's uniform upload.
   * Returns false when the info carries no IR or the tail is not servable.
   */
  static bool CompileYUVProgramForInfo(const ProgramInfo* info, AOTProgram* out);

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
   * Flattens a single-pass PointwiseChain/PointwiseGraph plan into the fused chain program and its
   * binding table, or returns false if the pass shape exceeds the selected kernel's capabilities.
   *
   * coverageFPs, when non-empty, is classified by the shared coverage classifier (the same one
   * LowerDraw uses): analytic rect/rrect leaves ride the clip-coverage register, an alpha-only
   * device-space texture becomes the mask binding, and anything else lowers as a general coverage
   * subtree rooted at the GP coverage. coverageLeafFromUVCoord marks the trailing FP as the
   * AtlasText glyph leaf a caller synthesized from the geometry processor. More than two coverage
   * FPs returns false.
   */
  static bool CompileChainProgram(BlockAllocator* allocator, const AOTEffectGraph& graph,
                                  const PassIR& pass,
                                  const std::vector<const FragmentProcessor*>& coverageFPs,
                                  bool coverageLeafFromUVCoord, AOTProgram* out);

  /**
   * Compiles a draw's record-time IR (its color graph and classified coverage terms) into the
   * fused chain program and its binding table, without building processors: the single-pass
   * decomposition plans the color side and the terms merge into the program. This is the
   * interpreter's view of the draw: the matcher derives the variant from the program, and the
   * per-draw upload (uniforms, samplers and the vertex transforms) consumes it. Returns false when
   * the expression does not reduce onto the chain kernel (a missing IR, an over-budget shape, a
   * coverage form the chain cannot carry).
   */
  static bool CompileChainProgramForInfo(const ProgramInfo* programInfo, AOTProgram* out);
};
}  // namespace tgfx

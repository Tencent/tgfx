
#include "gpu/aot/AOTPlanExecutor.h"
#include <algorithm>
#include <utility>
#include <vector>
#include "gpu/AOTMaterializationPolicy.h"
#include "gpu/AOTShapeRules.h"
#include "gpu/DrawingManager.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/aot/AOTChainBuilder.h"
#include "gpu/ops/StandardDrawOp.h"
#include "gpu/processors/AOTYUVChainProcessor.h"
#include "gpu/processors/PerlinNoiseFragmentProcessor.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/tasks/AOTPlanRenderTask.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
namespace {

// The linear source, the unary pointwise set, the perlin/YUV source shapes and the const-operand
// blend are shared shape rules (gpu/AOTShapeRules.h) — the same authority the planners and the
// chain builder consume, so the admission checks below cannot drift from them.

// A perlin pass's source must also carry resolvable lookup tables: the shape rule admits the
// node, the kernel still needs the two views at bind time.
static bool HasValidNoiseViews(const AOTEffectGraph& graph, AOTNodeID nodeID) {
  auto node = graph.nodeAt(nodeID);
  auto parameters =
      node != nullptr ? std::get_if<AOTPerlinNoiseParameters>(&node->parameters) : nullptr;
  return parameters != nullptr && parameters->permutationsView != nullptr &&
         parameters->noiseView != nullptr;
}

// The operator slots after a linear pass's source: unary pointwise ops chained in order. The perlin
// kernel additionally carries a const-color op or a blend whose other operand is an input-
// independent constant (the constant itself stays out of pass.nodes).
static bool ValidateLinearSlots(const AOTEffectGraph& graph, const AOTPassDescriptor& pass,
                                bool allowConstOperands) {
  AOTNodeID expectedInput = pass.nodes[0];
  for (size_t opIndex = 1; opIndex < pass.nodes.size(); ++opIndex) {
    auto node = graph.nodeAt(pass.nodes[opIndex]);
    if (node == nullptr) {
      return false;
    }
    bool accepted = IsUnaryPointwiseOp(node, expectedInput);
    if (!accepted && allowConstOperands && node->kind == AOTEffectKind::ConstColor) {
      accepted = node->inputs.size() == 1 && node->inputs[0] == expectedInput;
    }
    if (!accepted && allowConstOperands) {
      accepted = IsConstOperandBlend(graph, node, expectedInput);
    }
    if (!accepted) {
      return false;
    }
    expectedInput = pass.nodes[opIndex];
  }
  return true;
}

// A cut plan: topologically ordered self-contained fused passes, each reading some earlier cuts'
// output textures as leaves (materializedInputs[i] names the node dependencies[i]'s output
// replaces), with the last pass running on the destination as the terminal draw. Each pass's own
// output is its node list's last (the graph is SSA in construction order, so a subtree's root is
// its highest node). The structure is validated here; the fits were checked by the planner through
// the same segment build construction runs. A linear chain split carries the same shape: every
// later pass's materializedInputs holds its predecessor's output. The pass count is the planners'
// own policy (the cut planner caps itself at MaxCutPlanPasses, the linear splitter is unbounded),
// not an executor contract, so it is not re-checked here.
static bool ValidateCutPlan(const AOTEffectGraph& graph, const AOTEffectPlan& plan) {
  if (plan.passes.size() < 2) {
    return false;
  }
  for (size_t index = 0; index < plan.passes.size(); ++index) {
    const auto& pass = plan.passes[index];
    bool isLast = index + 1 == plan.passes.size();
    if (!IsAOTPointwiseDAGKernel(pass.kernel) || pass.nodes.empty() ||
        pass.materializesOutput == isLast ||
        pass.materializedInputs.size() != pass.dependencies.size() ||
        pass.output != pass.nodes.back()) {
      return false;
    }
    if (isLast && pass.output != plan.output) {
      return false;
    }
    for (size_t dep = 0; dep < pass.dependencies.size(); ++dep) {
      auto dependency = pass.dependencies[dep];
      auto cut = pass.materializedInputs[dep];
      if (dependency >= index || !cut.isValid() || cut.index() >= graph.nodeCount()) {
        return false;
      }
      std::vector<bool> cutReachable(graph.nodeCount(), false);
      if (!CollectReachableNodes(graph, cut, &cutReachable)) {
        return false;
      }
      for (auto nodeID : pass.nodes) {
        if (cutReachable[nodeID.index()]) {
          return false;
        }
      }
    }
    // A pass boundary quantizes to the 1/255 grid, and an AlphaThreshold amplifies that rounding
    // into a step flip (see CanStartPassAt in AOTEffectDecomposer.cpp), so a threshold must share
    // its pass with the node producing its input: the input may only be a node of the same pass,
    // the geometry color, or a copied designator — never a value crossing a pass boundary.
    for (auto nodeID : pass.nodes) {
      const auto* node = graph.nodeAt(nodeID);
      if (node == nullptr || node->kind != AOTEffectKind::AlphaThreshold) {
        continue;
      }
      for (auto input : node->inputs) {
        if (input == AOTNodeID(0)) {
          continue;
        }
        if (std::find(pass.nodes.begin(), pass.nodes.end(), input) != pass.nodes.end()) {
          continue;
        }
        const auto* producer = graph.nodeAt(input);
        if (producer == nullptr || (producer->kind != AOTEffectKind::GeometryColorOpaqueInput &&
                                    producer->kind != AOTEffectKind::GeometryWhiteInput)) {
          return false;
        }
      }
    }
  }
  return true;
}

// Maps top-left device coordinates of the destination onto its fragment coordinates: identity for
// a top-left target, a vertical flip for a bottom-left one. It is its own inverse.
static Matrix DeviceToFragCoord(const RenderTargetProxy* destination) {
  if (destination->origin() != ImageOrigin::BottomLeft) {
    return Matrix::I();
  }
  auto matrix = Matrix::MakeScale(1.0f, -1.0f);
  matrix.postTranslate(0.0f, static_cast<float>(destination->height()));
  return matrix;
}

// One non-terminal pass of any multi-pass plan: the target allocation, the fill draw over it and
// the edge record. The fill's coordOffset keeps the pass's processors seeing the coordinate space
// they were planned for (the fill's FP-space offset plus the target placement for a pass that
// reads the geometry color, just the placement otherwise); inputAlpha carries the paint alpha a
// geometry-color-reading fill bakes in (an operand fill is a shader value, so it is always 1 — the
// terminal draw's epilogue applies the paint alpha exactly once).
static bool AppendMaterializedPass(Context* context, uint32_t renderFlags,
                                   PlacementPtr<DrawIR> passIR,
                                   const MaterializedGeometry& geometry, const Point& fillOffset,
                                   float inputAlpha, AOTEdgeReason reason,
                                   std::vector<AOTRenderPass>* passes) {
  auto materialized = AOTMaterializationPolicy::AllocateTarget(context, geometry);
  if (materialized.renderTarget == nullptr) {
    return false;
  }
  AOTRenderPass renderPass = {};
  renderPass.target = std::move(materialized.renderTarget);
  renderPass.drawOp = context->drawingManager()->makePassFillDrawOp(
      renderPass.target, std::move(passIR), renderFlags, fillOffset, inputAlpha);
  if (renderPass.drawOp == nullptr) {
    return false;
  }
  renderPass.edge = AOTMaterializedEdge{reason, geometry};
  passes->push_back(std::move(renderPass));
  return true;
}

// The reason of the edge one fill pass's output creates: the kind of the node that consumes the
// cut in the dependent pass — a blend operand is a materialized shader value, a unary pointwise
// operator is the linear planner's split.
static AOTEdgeReason EdgeReasonForCutConsumer(const AOTEffectGraph& graph,
                                              const AOTPassDescriptor& consumerPass,
                                              AOTNodeID cutRoot) {
  for (auto nodeID : consumerPass.nodes) {
    const auto* node = graph.nodeAt(nodeID);
    if (node == nullptr) {
      continue;
    }
    for (auto input : node->inputs) {
      if (input == cutRoot) {
        return IsUnaryPointwiseKind(node->kind) ? AOTEdgeReason::Split
                                                : AOTEdgeReason::OperandMaterialize;
      }
    }
  }
  return AOTEdgeReason::OperandMaterialize;
}

// Builds a multi-pass plan's passes: every pass is a copy of its own original-graph nodes with its
// cuts replaced by the producing passes' intermediate textures, and every pass but the last
// renders into its own intermediate (a fill at top-left device positions, with the local-space
// parameters remapped); the last pass's processor becomes the terminal draw's color chain over the
// destination. A fill pass that reads the geometry color bakes the draw's uniform input alpha
// (sourceAlpha) into its output — later passes and the terminal draw read through the opaque-white
// designator, so the epilogue never re-applies it; an operand-materializing fill carries a shader
// value with no alpha.
static bool BuildCutPlanPasses(Context* context, uint32_t renderFlags, const AOTEffectGraph& graph,
                               const AOTEffectPlan& plan, const Rect& deviceBounds,
                               const RenderTargetProxy* destination, const Point& fpCoordOffset,
                               const std::optional<Matrix>& deviceToLocal, float sourceAlpha,
                               AOTRenderPlan* renderPlan) {
  auto drawingManager = context->drawingManager();
  auto allocator = drawingManager->drawingAllocator();
  // No apron: the terminal pass reads each intermediate at its own device pixel
  // (DeviceSpaceTextureEffect), so producer and consumer stay pixel-aligned, and the fill passes
  // cover exactly the same snapped bounds — same rule the linear split passes follow.
  auto geometry = AOTMaterializationPolicy::PrepareGeometry(deviceBounds, 0.0f);
  if (geometry.isEmpty()) {
    return false;
  }
  auto deviceToFrag = DeviceToFragCoord(destination);
  // A fill pass's device-space leaves read gl_FragCoord — the fill target's pixel space, which is
  // the top-left device positions minus the shared placement — while their uvMatrices address the
  // destination's positions. The leaf remap is therefore the device-to-frag flip plus the
  // placement, the same composition the split-off fills apply; without the placement every leaf
  // samples O pixels off whenever the draw's bounds do not start at the origin.
  auto deviceLeafRemap = deviceToFrag;
  deviceLeafRemap.preConcat(Matrix::MakeTrans(geometry.coordOffset.x, geometry.coordOffset.y));
  // The fill passes' local-space remap: they evaluate their local-coordinate parameters at
  // top-left device positions, the original draw evaluated them at its own local coordinates —
  // the draw's device-to-local mapping for an on-screen draw, or the fill's own offset for an
  // offscreen fill (its local frame is the top-left device one plus fpCoordOffset). The terminal
  // pass runs on the destination and keeps the original spaces, so both of its remaps are
  // identity.
  auto coordToLocal = Matrix::I();
  if (deviceToLocal.has_value()) {
    coordToLocal = *deviceToLocal;
  } else if (fpCoordOffset != Point::Zero()) {
    coordToLocal = Matrix::MakeTrans(fpCoordOffset.x, fpCoordOffset.y);
  }
  for (size_t index = 0; index < plan.passes.size(); ++index) {
    const auto& pass = plan.passes[index];
    bool isTerminal = index + 1 == plan.passes.size();
    std::vector<AOTTextureParameters> intermediates = {};
    for (auto dependency : pass.dependencies) {
      if (dependency >= renderPlan->passes.size()) {
        return false;
      }
      auto target = renderPlan->passes[dependency].target;
      if (target == nullptr) {
        return false;
      }
      // A fill pass reads an earlier intermediate at the same target pixel (its fill covers the
      // target exactly, so gl_FragCoord is already the intermediate's pixel space). The terminal
      // draw reads its intermediates from the destination's fragment coordinates: back to
      // top-left device positions first, then into the target's pixel space.
      auto matrix = Matrix::I();
      if (isTerminal) {
        matrix = Matrix::MakeTrans(-geometry.coordOffset.x, -geometry.coordOffset.y);
        matrix.preConcat(deviceToFrag);
      }
      intermediates.push_back(
          AOTChainBuilder::IntermediateSource(target->asTextureProxy(), matrix));
    }
    AOTEffectGraph segment = {};
    AOTPassDescriptor segmentPass = {};
    if (!AOTChainBuilder::BuildCutSegment(
            graph, pass, intermediates, isTerminal ? Matrix::I() : coordToLocal,
            isTerminal ? Matrix::I() : deviceLeafRemap, &segment, &segmentPass)) {
      return false;
    }
    // The pass travels as its IR: the segment graph plus the descriptor the planner fit. The
    // processor is rebuilt from it once per prepare, never re-planned.
    DrawIR passIR = {};
    passIR.color = std::move(segment);
    passIR.passDescriptor = std::move(segmentPass);
    auto ownedIR = allocator->make<DrawIR>(std::move(passIR));
    if (ownedIR == nullptr) {
      return false;
    }
    if (isTerminal) {
      renderPlan->terminalIR = std::move(ownedIR);
      return true;
    }
    auto inputAlpha = PassReadsGeometryColor(graph, pass) ? sourceAlpha : 1.0f;
    auto edgeReason = EdgeReasonForCutConsumer(graph, plan.passes[index + 1], pass.output);
    if (!AppendMaterializedPass(context, renderFlags, std::move(ownedIR), geometry,
                                geometry.coordOffset, inputAlpha, edgeReason,
                                &renderPlan->passes)) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool AOTPlanExecutor::CanExecute(const AOTEffectGraph& graph, const AOTEffectPlan& plan) {
  if (plan.passes.empty() || !plan.output.isValid() || plan.output != graph.root()) {
    return false;
  }
  if (plan.passes.size() > 1) {
    return ValidateCutPlan(graph, plan);
  }
  const auto& pass = plan.passes[0];
  if (pass.nodes.empty() || pass.output != plan.output || !pass.dependencies.empty() ||
      pass.materializesOutput) {
    return false;
  }
  // PointwiseChain/PointwiseGraph passes fuse the whole DAG. The chain builder's own planner
  // decides: it runs the slot encoding (including the extra TEX_MODULATE instruction of a
  // computed-input texture and the closure of every input edge), the per-kind parameter blocks,
  // the resource profile and the register allocation that construction runs, without creating
  // processors. Clip slots appended from the draw's coverage FPs are not part of the plan; the
  // builder re-plans with them and refuses what no longer fits.
  if (IsAOTPointwiseDAGKernel(pass.kernel)) {
    return AOTChainBuilder::PlanChainPass(graph, pass, true);
  }
  // The linear kernels: a source node followed by a bounded run of operator slots.
  if (pass.resourceProfile != AOTResourceProfile::Automatic || pass.output != pass.nodes.back()) {
    return false;
  }
  switch (pass.kernel) {
    case AOTKernelKind::YUVTextureFill:
      return pass.nodes.size() <= 1 + MaxPointwiseSlots && IsYUVChainSource(graph, pass.nodes[0]) &&
             ValidateLinearSlots(graph, pass, false);
    case AOTKernelKind::PerlinNoiseFill:
      return pass.nodes.size() <= 1 + MaxPointwiseSlots &&
             IsPerlinNoiseSource(graph, pass.nodes[0]) &&
             HasValidNoiseViews(graph, pass.nodes[0]) && ValidateLinearSlots(graph, pass, true);
    default:
      return false;
  }
}

bool AOTPlanExecutor::NeedsLocalRemap(const AOTEffectGraph& graph, const AOTEffectPlan& plan) {
  if (plan.passes.size() <= 1) {
    return false;
  }
  for (size_t index = 0; index + 1 < plan.passes.size(); ++index) {
    if (PassNeedsLocalRemap(graph, plan.passes[index])) {
      return true;
    }
  }
  return false;
}

bool AOTPlanExecutor::BuildPasses(Context* context, uint32_t renderFlags,
                                  const AOTEffectGraph& graph, const AOTEffectPlan& plan,
                                  const Rect& deviceBounds, const RenderTargetProxy* destination,
                                  const Point& fpCoordOffset,
                                  const std::optional<Matrix>& deviceToLocal,
                                  AOTRenderPlan* renderPlan, float sourceAlpha) {
  auto drawingManager = context->drawingManager();
  auto allocator = drawingManager->drawingAllocator();
  if (plan.passes.size() == 1) {
    const auto& pass = plan.passes[0];
    if (IsAOTPointwiseDAGKernel(pass.kernel)) {
      // Production never reaches this branch (a fused single pass serves its draw in place, and
      // PlanRequiresTask refuses to build a task for it), but the executor's API contract admits
      // one: the terminal draw carries the pass IR like any other chain pass.
      DrawIR passIR = {};
      passIR.color = graph;
      passIR.passDescriptor = pass;
      renderPlan->terminalIR = allocator->make<DrawIR>(std::move(passIR));
      return renderPlan->terminalIR != nullptr;
    }
    // The linear kernels: a YUV or perlin processor as the terminal's color processor.
    auto processor = AOTChainBuilder::BuildFPForPass(allocator, graph, pass);
    if (processor == nullptr) {
      return false;
    }
    renderPlan->terminalColors.emplace_back(std::move(processor));
    return true;
  }
  return BuildCutPlanPasses(context, renderFlags, graph, plan, deviceBounds, destination,
                            fpCoordOffset, deviceToLocal, sourceAlpha, renderPlan);
}

PlacementPtr<RenderTask> AOTPlanExecutor::Make(Context* context, uint32_t renderFlags,
                                               const AOTEffectGraph& graph,
                                               const AOTEffectPlan& plan, const Rect& deviceBounds,
                                               std::shared_ptr<RenderTargetProxy> destination,
                                               PlacementPtr<DrawOp>* originalDraw,
                                               const Point& fpCoordOffset, bool clearDestination,
                                               const std::optional<Matrix>& deviceToLocal) {
  if (context == nullptr || destination == nullptr || destination->getContext() != context ||
      originalDraw == nullptr || *originalDraw == nullptr ||
      (*originalDraw)->type() == DrawOp::Type::StencilCoverPathDrawOp || !CanExecute(graph, plan) ||
      (deviceToLocal.has_value() && deviceToLocal->hasPerspective())) {
    return nullptr;
  }
  for (const auto& pass : plan.passes) {
    for (auto id : pass.nodes) {
      const auto* node = graph.nodeAt(id);
      const auto* noise =
          node != nullptr ? std::get_if<AOTPerlinNoiseParameters>(&node->parameters) : nullptr;
      if (noise != nullptr && (noise->permutationsView->getContext() != context ||
                               noise->noiseView->getContext() != context)) {
        return nullptr;
      }
    }
  }
  AOTRenderPlan renderPlan = {};
  float sourceAlpha = 1.0f;
  if (plan.passes.size() > 1 && PlanBakesGeometryAlpha(graph, plan)) {
    // Only a uniform geometry alpha can be baked into a fill pass that reads the geometry color.
    // A per-vertex alpha cannot be reconstructed by a device-aligned full-rect fill. A plan whose
    // passes only materialize operands keeps the paint alpha in the terminal draw's epilogue
    // instead, so it needs no uniform color at all.
    auto color = static_cast<StandardDrawOp*>(originalDraw->get())->uniformGeometryColor();
    if (!color.has_value()) {
      return nullptr;
    }
    sourceAlpha = color->alpha;
  }
  if (!BuildPasses(context, renderFlags, graph, plan, deviceBounds, destination.get(),
                   fpCoordOffset, deviceToLocal, &renderPlan, sourceAlpha) ||
      !renderPlan.isWellFormed()) {
    return nullptr;
  }
  // The plan's intermediates are temporary bytes of the flush cycle being recorded, so they join
  // the simultaneously-live set at task creation. Counting them at execute time (after this
  // cycle's settle) would push them into the next cycle's peak instead.
  auto* shaderCache = context->precompiledShaderCache();
  if (shaderCache != nullptr) {
    for (const auto& renderPass : renderPlan.passes) {
      if (renderPass.target != nullptr) {
        shaderCache->recordActiveTemporaryBytes(static_cast<uint64_t>(renderPass.target->width()) *
                                                static_cast<uint64_t>(renderPass.target->height()) *
                                                4);
      }
    }
  }
  auto allocator = context->drawingManager()->drawingAllocator();
  auto task =
      allocator->make<AOTPlanRenderTask>(allocator, std::move(renderPlan), std::move(destination));
  if (task == nullptr) {
    return nullptr;
  }
  task->setOriginalDraw(std::move(*originalDraw));
  task->setClearDestination(clearDestination);
  return task;
}
}  // namespace tgfx

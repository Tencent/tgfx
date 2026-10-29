
#include "AOTPlanExecutor.h"
#include <utility>
#include <vector>
#include "gpu/AOTChainBuilder.h"
#include "gpu/AOTMaterializationPolicy.h"
#include "gpu/AOTMaterializationRecorder.h"
#include "gpu/AOTShapeRules.h"
#include "gpu/DrawingManager.h"
#include "gpu/PrecompiledShaderCache.h"
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

// The operator nodes of one pass of a multi-pass linear plan: everything after the source in the
// first pass, the whole node list in a later one.
static std::vector<AOTNodeID> SegmentOperators(const AOTPassDescriptor& pass, bool firstPass) {
  return {pass.nodes.begin() + (firstPass ? 1 : 0), pass.nodes.end()};
}

// A linear chain split into chain-kernel passes: the first pass carries the source plus its
// operators, every later pass operators reading its predecessor's materialized output in device
// space, and all but the last pass materialize. Every pass must fit one chain pass as its own
// segment (AOTChainBuilder::BuildLinearSegment, the same check the planner cuts by), and no later
// pass starts with an AlphaThreshold (see CanStartPassAt in AOTEffectDecomposer.cpp).
static bool ValidateSegmentedPlan(const AOTEffectGraph& graph, const AOTEffectPlan& plan) {
  AOTNodeID expectedInput = AOTNodeID::Invalid();
  const AOTTextureParameters* source = nullptr;
  for (size_t index = 0; index < plan.passes.size(); ++index) {
    const auto& pass = plan.passes[index];
    bool isLast = index + 1 == plan.passes.size();
    if (!IsAOTPointwiseDAGKernel(pass.kernel) || pass.nodes.empty() ||
        pass.output != pass.nodes.back() || pass.materializesOutput == isLast) {
      return false;
    }
    size_t firstOp = 0;
    if (index == 0) {
      if (!pass.dependencies.empty() || !IsLinearMultiPassSource(graph, pass.nodes[0])) {
        return false;
      }
      source = std::get_if<AOTTextureParameters>(&graph.nodeAt(pass.nodes[0])->parameters);
      expectedInput = pass.nodes[0];
      firstOp = 1;
    } else {
      auto first = graph.nodeAt(pass.nodes[0]);
      if (pass.dependencies.size() != 1 || pass.dependencies[0] != index - 1 || first == nullptr ||
          first->kind == AOTEffectKind::AlphaThreshold) {
        return false;
      }
    }
    for (size_t opIndex = firstOp; opIndex < pass.nodes.size(); ++opIndex) {
      if (!IsUnaryPointwiseOp(graph.nodeAt(pass.nodes[opIndex]), expectedInput)) {
        return false;
      }
      expectedInput = pass.nodes[opIndex];
    }
    // A later pass's intermediate does not exist yet; its fit only depends on the source kind.
    auto segmentSource =
        index == 0 ? *source
                   : AOTChainBuilder::IntermediateSource(source->textureProxy, Matrix::I());
    AOTEffectGraph segment = {};
    AOTPassDescriptor segmentPass = {};
    if (!AOTChainBuilder::BuildLinearSegment(graph, SegmentOperators(pass, index == 0),
                                             segmentSource, &segment, &segmentPass, index == 0)) {
      return false;
    }
  }
  return plan.passes.back().output == plan.output;
}

static const AOTTextureParameters* FirstPassTexture(const AOTEffectGraph& graph,
                                                    const AOTEffectPlan& plan) {
  if (plan.passes.empty() || plan.passes[0].nodes.empty()) {
    return nullptr;
  }
  auto node = graph.nodeAt(plan.passes[0].nodes[0]);
  return node != nullptr && node->kind == AOTEffectKind::TextureSource
             ? std::get_if<AOTTextureParameters>(&node->parameters)
             : nullptr;
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

}  // namespace

bool AOTPlanExecutor::CanExecute(const AOTEffectGraph& graph, const AOTEffectPlan& plan) {
  if (plan.passes.empty() || !plan.output.isValid() || plan.output != graph.root()) {
    return false;
  }
  if (plan.passes.size() > 1) {
    return ValidateSegmentedPlan(graph, plan);
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
    return AOTChainBuilder::PlanChainPass(graph, pass);
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

bool AOTPlanExecutor::SamplesDeviceSpace(const AOTEffectGraph& graph, const AOTEffectPlan& plan) {
  if (plan.passes.size() <= 1) {
    return true;
  }
  auto texture = FirstPassTexture(graph, plan);
  return texture != nullptr && texture->samplingKind == AOTTextureSamplingKind::Device;
}

bool AOTPlanExecutor::BuildPasses(Context* context, uint32_t renderFlags,
                                  const AOTEffectGraph& graph, const AOTEffectPlan& plan,
                                  const Rect& deviceBounds, const RenderTargetProxy* destination,
                                  const Point& fpCoordOffset,
                                  const std::optional<Matrix>& deviceToLocal,
                                  std::vector<AOTRenderPass>* passes,
                                  DrawOp::ColorProcessorList* terminalColors, float sourceAlpha) {
  auto drawingManager = context->drawingManager();
  auto allocator = drawingManager->drawingAllocator();
  if (plan.passes.size() == 1) {
    auto processor = AOTChainBuilder::BuildFPForPass(allocator, graph, plan.passes[0]);
    if (processor == nullptr) {
      return false;
    }
    terminalColors->emplace_back(std::move(processor));
    return true;
  }
  // One geometry for the whole plan: every split-off pass renders into a target of exactly this
  // size over the draw's device bounds, and the terminal draw samples the last one back at the
  // same pixel. Every pass is pointwise (an operator reads its input at the output pixel only), so
  // no apron is needed and the targets stay pixel-aligned with each other.
  auto geometry = AOTMaterializationPolicy::PrepareGeometry(deviceBounds, 0.0f);
  if (geometry.isEmpty()) {
    return false;
  }
  // A split-off fill evaluates its processors at local coordinates fragCoord + offset, the same
  // values the original processors see at the matching destination pixel: the draw's own
  // FP-space translation (fpCoordOffset, the coordOffset of an offscreen fill) plus the target's
  // placement on the destination.
  auto intermediateOffset = geometry.coordOffset + fpCoordOffset;
  auto deviceToFragCoord = DeviceToFragCoord(destination);
  std::shared_ptr<TextureProxy> previousTexture = nullptr;
  for (size_t index = 0; index < plan.passes.size(); ++index) {
    const auto& pass = plan.passes[index];
    bool isTerminal = index + 1 == plan.passes.size();
    AOTTextureParameters source = {};
    if (index == 0) {
      // The source pass runs as a split-off fill, so its source must sample what the original
      // draw samples at the same destination pixel.
      auto texture = FirstPassTexture(graph, plan);
      if (texture == nullptr) {
        return false;
      }
      source = *texture;
      if (source.samplingKind == AOTTextureSamplingKind::Device) {
        // A device-space source reads fragment coordinates. In the split-off target the fragment
        // coordinate of a pixel is its top-left device position minus the target's placement;
        // map it back to the destination's fragment coordinates first.
        source.uvMatrix.preConcat(deviceToFragCoord);
        source.uvMatrix.preTranslate(geometry.coordOffset.x, geometry.coordOffset.y);
      } else if (deviceToLocal.has_value()) {
        // The fill's local coordinates are top-left device positions (plus fpCoordOffset); the
        // original draw maps those onto its own local coordinates first.
        source.uvMatrix.preConcat(*deviceToLocal);
      }
    } else {
      // Split-off passes read each other at the same target pixel. The terminal draw reads the
      // last one from the destination's fragment coordinates: back to top-left device positions,
      // then into the target's pixel space.
      auto matrix = Matrix::I();
      if (isTerminal) {
        matrix = Matrix::MakeTrans(-geometry.coordOffset.x, -geometry.coordOffset.y);
        matrix.preConcat(deviceToFragCoord);
      }
      source = AOTChainBuilder::IntermediateSource(previousTexture, matrix);
    }
    // Every pass runs on the chain kernel as its own segment: the source above, then this pass's
    // operators.
    AOTEffectGraph segment = {};
    AOTPassDescriptor segmentPass = {};
    PlacementPtr<FragmentProcessor> processor = nullptr;
    if (AOTChainBuilder::BuildLinearSegment(graph, SegmentOperators(pass, index == 0), source,
                                            &segment, &segmentPass, index == 0)) {
      processor = AOTChainBuilder::BuildFPForPass(allocator, segment, segmentPass);
    }
    if (processor == nullptr) {
      return false;
    }
    if (isTerminal) {
      terminalColors->emplace_back(std::move(processor));
      break;
    }
    auto materialized = AOTMaterializationPolicy::AllocateTarget(context, geometry);
    if (materialized.renderTarget == nullptr) {
      return false;
    }
    AOTRenderPass renderPass = {};
    renderPass.target = std::move(materialized.renderTarget);
    renderPass.drawOp = drawingManager->makeFillDrawOp(
        renderPass.target, std::move(processor), renderFlags, intermediateOffset,
        InvalidOffscreenFillKey, index == 0 ? sourceAlpha : 1.0f);
    previousTexture = renderPass.target->asTextureProxy();
    if (renderPass.drawOp == nullptr || previousTexture == nullptr) {
      return false;
    }
    renderPass.edge = {AOTEdgeSpace::Device, geometry, 0.0f};
    passes->push_back(std::move(renderPass));
  }
  return true;
}

PlacementPtr<RenderTask> AOTPlanExecutor::Make(
    Context* context, uint32_t renderFlags, const AOTEffectGraph& graph, const AOTEffectPlan& plan,
    const Rect& deviceBounds, std::shared_ptr<RenderTargetProxy> destination,
    PlacementPtr<DrawOp>* originalDraw, const Point& fpCoordOffset, bool clearDestination,
    const std::optional<Matrix>& deviceToLocal, AOTMaterializationRecorder* producers) {
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
  // Producers go first: the plan's own passes (and its terminal draw) may sample any of them,
  // including a split-off source pass whose source texture is a materialized blend operand.
  if (producers != nullptr && !producers->takeProducerPasses(&renderPlan.passes)) {
    return nullptr;
  }
  float sourceAlpha = 1.0f;
  if (plan.passes.size() > 1) {
    // Only a uniform geometry alpha can be moved into the split-off source pass. A per-vertex
    // alpha cannot be reconstructed by a device-aligned full-rect fill.
    auto color = static_cast<StandardDrawOp*>(originalDraw->get())->uniformGeometryColor();
    if (!color.has_value()) {
      return nullptr;
    }
    sourceAlpha = color->alpha;
  }
  if (!BuildPasses(context, renderFlags, graph, plan, deviceBounds, destination.get(),
                   fpCoordOffset, deviceToLocal, &renderPlan.passes, &renderPlan.terminalColors,
                   sourceAlpha) ||
      !renderPlan.isWellFormed()) {
    return nullptr;
  }
  // The plan's intermediates are temporary bytes of the flush cycle being recorded, so they join
  // the simultaneously-live set at task creation — the same moment recordMaterializedEdge counts
  // its own edges. Counting them at execute time (after this cycle's settle) would push them into
  // the next cycle's peak instead.
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

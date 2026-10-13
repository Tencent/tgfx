
#include "AOTPlanExecutor.h"
#include <utility>
#include "gpu/AOTChainBuilder.h"
#include "gpu/DrawingManager.h"
#include "gpu/processors/AOTPointwiseTailProcessor.h"
#include "gpu/processors/AOTYUVChainProcessor.h"
#include "gpu/processors/PerlinNoiseFragmentProcessor.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/tasks/AOTPlanRenderTask.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
namespace {

static bool ValidatePointwiseTailSource(const AOTEffectGraph& graph, AOTNodeID nodeID) {
  auto node = graph.nodeAt(nodeID);
  if (node == nullptr || node->kind != AOTEffectKind::TextureSource || node->inputs.size() != 1) {
    return false;
  }
  auto parameters = std::get_if<AOTTextureParameters>(&node->parameters);
  auto input = graph.nodeAt(node->inputs[0]);
  return parameters != nullptr && input != nullptr && input->kind == AOTEffectKind::GeometryColor &&
         (parameters->samplingKind == AOTTextureSamplingKind::Plain ||
          parameters->samplingKind == AOTTextureSamplingKind::Device) &&
         !parameters->isYUV && !parameters->isAlphaOnly && !parameters->hasRGBAAA &&
         !parameters->hasPerspective;
}

static bool ValidatePointwiseTailOp(const AOTEffectGraph& graph, AOTNodeID nodeID,
                                    AOTNodeID expectedInput) {
  auto node = graph.nodeAt(nodeID);
  return node != nullptr && node->inputs.size() == 1 && node->inputs[0] == expectedInput &&
         (node->kind == AOTEffectKind::ColorMatrix || node->kind == AOTEffectKind::Luma ||
          node->kind == AOTEffectKind::AlphaThreshold ||
          node->kind == AOTEffectKind::ColorSpaceXform);
}

static bool ValidatePerlinNoiseSource(const AOTEffectGraph& graph, AOTNodeID nodeID) {
  auto node = graph.nodeAt(nodeID);
  if (node == nullptr || node->kind != AOTEffectKind::PerlinNoiseSource ||
      node->inputs.size() != 1) {
    return false;
  }
  auto parameters = std::get_if<AOTPerlinNoiseParameters>(&node->parameters);
  auto input = graph.nodeAt(node->inputs[0]);
  return parameters != nullptr && parameters->permutationsView != nullptr &&
         parameters->noiseView != nullptr && input != nullptr &&
         input->kind == AOTEffectKind::GeometryColor;
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
    bool accepted = ValidatePointwiseTailOp(graph, pass.nodes[opIndex], expectedInput);
    if (!accepted && allowConstOperands && node->kind == AOTEffectKind::ConstColor) {
      accepted = node->inputs.size() == 1 && node->inputs[0] == expectedInput;
    }
    if (!accepted && allowConstOperands && node->kind == AOTEffectKind::Blend &&
        node->inputs.size() == 2) {
      auto blendParams = std::get_if<AOTBlendParameters>(&node->parameters);
      auto* srcNode = graph.nodeAt(node->inputs[0]);
      auto* dstNode = graph.nodeAt(node->inputs[1]);
      bool srcIsConst = srcNode != nullptr && srcNode->kind == AOTEffectKind::ConstColor;
      bool dstIsConst = dstNode != nullptr && dstNode->kind == AOTEffectKind::ConstColor;
      AOTNodeID chained = srcIsConst ? node->inputs[1] : node->inputs[0];
      accepted = blendParams != nullptr && blendParams->childType != 2 &&
                 srcIsConst != dstIsConst && chained == expectedInput &&
                 AOTConstColorIsInputIndependent(graph, srcIsConst ? srcNode : dstNode);
    }
    if (!accepted) {
      return false;
    }
    expectedInput = pass.nodes[opIndex];
  }
  return true;
}

static bool ValidateYUVSource(const AOTEffectGraph& graph, AOTNodeID nodeID) {
  auto node = graph.nodeAt(nodeID);
  auto parameters =
      node != nullptr ? std::get_if<AOTTextureParameters>(&node->parameters) : nullptr;
  return parameters != nullptr && node->kind == AOTEffectKind::TextureSource &&
         node->inputs.size() == 1 && node->inputs[0] == AOTNodeID(0) && parameters->isYUV &&
         !parameters->hasRGBAAA && parameters->samplingKind == AOTTextureSamplingKind::Plain;
}

}  // namespace

bool AOTPlanExecutor::CanExecute(const AOTEffectGraph& graph, const AOTEffectPlan& plan) {
  if (plan.passes.size() != 1 || !plan.output.isValid() || plan.output != graph.root()) {
    return false;
  }
  const auto& pass = plan.passes[0];
  if (pass.nodes.empty() || pass.output != plan.output) {
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
    case AOTKernelKind::PointwiseTail:
      return pass.nodes.size() <= 1 + AOTPointwiseTailProcessor::MaxSlots &&
             ValidatePointwiseTailSource(graph, pass.nodes[0]) &&
             ValidateLinearSlots(graph, pass, false);
    case AOTKernelKind::YUVTextureFill:
      return pass.nodes.size() <= 1 + AOTYUVChainProcessor::MaxPointwiseSlots &&
             ValidateYUVSource(graph, pass.nodes[0]) && ValidateLinearSlots(graph, pass, false);
    case AOTKernelKind::PerlinNoiseFill:
      return pass.nodes.size() <= 1 + PerlinNoiseFragmentProcessor::MaxPointwiseSlots &&
             ValidatePerlinNoiseSource(graph, pass.nodes[0]) &&
             ValidateLinearSlots(graph, pass, true);
    default:
      return false;
  }
}

PlacementPtr<RenderTask> AOTPlanExecutor::Make(Context* context, const AOTEffectGraph& graph,
                                               const AOTEffectPlan& plan,
                                               std::shared_ptr<RenderTargetProxy> destination,
                                               PlacementPtr<DrawOp>* originalDraw,
                                               bool clearDestination) {
  if (context == nullptr || destination == nullptr || destination->getContext() != context ||
      originalDraw == nullptr || *originalDraw == nullptr ||
      (*originalDraw)->type() == DrawOp::Type::StencilCoverPathDrawOp || !CanExecute(graph, plan)) {
    return nullptr;
  }
  const auto& pass = plan.passes[0];
  for (auto id : pass.nodes) {
    const auto* node = graph.nodeAt(id);
    const auto* noise =
        node != nullptr ? std::get_if<AOTPerlinNoiseParameters>(&node->parameters) : nullptr;
    if (noise != nullptr && (noise->permutationsView->getContext() != context ||
                             noise->noiseView->getContext() != context)) {
      return nullptr;
    }
  }
  auto allocator = context->drawingManager()->drawingAllocator();
  auto processor = AOTChainBuilder::BuildFPForPass(allocator, graph, pass);
  if (processor == nullptr) {
    return nullptr;
  }
  DrawOp::ColorProcessorList planColors = {};
  planColors.emplace_back(std::move(processor));
  auto task =
      allocator->make<AOTPlanRenderTask>(allocator, std::move(planColors), std::move(destination));
  if (task == nullptr) {
    return nullptr;
  }
  task->setOriginalDraw(std::move(*originalDraw));
  task->setClearDestination(clearDestination);
  return task;
}
}  // namespace tgfx

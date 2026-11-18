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

#include "layers/LayerDrawSession.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include "core/Matrix3DUtils.h"
#include "core/utils/Log.h"
#include "layers/BackgroundHandler.h"
#include "layers/BackgroundSnapshotMap.h"
#include "layers/BackgroundSource.h"
#include "layers/CanvasUtils.h"
#include "layers/DrawArgs.h"
#include "layers/LayerStyleSource.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/layerstyles/LayerStyle.h"

namespace tgfx {

namespace {

// Fixed background sampling density relative to the final output pixel grid. One output pixel is
// sampled by one background pixel, so every region of an export sees the same quality and the same
// sampling phase.
static constexpr float ExportBackgroundSurfaceScale = 1.0f;

// Automatic surface planning never grows beyond this edge even on devices with larger textures:
// every region surface and its intermediates stay bounded, and the count of regions stays sane.
static constexpr int DefaultExportSurfaceLimit = 2048;

static Rect ToRect(const PixelRect& rect) {
  auto left = static_cast<float>(rect.left);
  auto top = static_cast<float>(rect.top);
  auto right = static_cast<float>(rect.right);
  auto bottom = static_cast<float>(rect.bottom);
  return Rect::MakeLTRB(
      static_cast<double>(left) > rect.left ? std::nextafter(left, -INFINITY) : left,
      static_cast<double>(top) > rect.top ? std::nextafter(top, -INFINITY) : top,
      static_cast<double>(right) < rect.right ? std::nextafter(right, INFINITY) : right,
      static_cast<double>(bottom) < rect.bottom ? std::nextafter(bottom, INFINITY) : bottom);
}

// An exactly representable origin avoids losing a pixel even for geometry submitted directly
// in distant scene coordinates. If one pixel and its halo cannot fit from that origin, keep the
// exact integer window and let the canvas rebase individual draws.
static int AlignedOrigin(int start, int end, int coreSize, int limit) {
  auto origin = static_cast<float>(start);
  if (static_cast<double>(origin) > start) {
    origin = std::nextafter(origin, -INFINITY);
  }
  auto aligned = static_cast<int>(origin);
  auto minimumSize = static_cast<int64_t>(end) - aligned - (coreSize - 1);
  return minimumSize <= limit ? aligned : start;
}

// Region planning maps output pixels through the capture-root matrices. Rect is float-based and
// cannot hold every integer pixel coordinate of a large output exactly (16777217 is already
// rounded to 16777216), which would shift the region grid of the last tiles. The planning math is
// therefore done in double precision and only the final region rects are converted to integers.
struct PreciseRect {
  double left = 0.0;
  double top = 0.0;
  double right = 0.0;
  double bottom = 0.0;

  bool isFinite() const {
    return std::isfinite(left) && std::isfinite(top) && std::isfinite(right) &&
           std::isfinite(bottom);
  }

  void outset(double dx, double dy) {
    left -= dx;
    top -= dy;
    right += dx;
    bottom += dy;
  }

  void roundOut() {
    left = std::floor(left);
    top = std::floor(top);
    right = std::ceil(right);
    bottom = std::ceil(bottom);
  }
};

static PreciseRect ToPreciseRect(const PixelRect& rect) {
  PreciseRect result = {};
  result.left = rect.left;
  result.top = rect.top;
  result.right = rect.right;
  result.bottom = rect.bottom;
  return result;
}

static bool IsSafePixelRect(const PreciseRect& rect) {
  constexpr double MaxPixelCoordinate = static_cast<double>(INT32_MAX / 2);
  return rect.isFinite() && rect.left >= -MaxPixelCoordinate && rect.top >= -MaxPixelCoordinate &&
         rect.right <= MaxPixelCoordinate && rect.bottom <= MaxPixelCoordinate;
}

static PixelRect ToPixelRect(const PreciseRect& rect) {
  PixelRect result = {};
  result.left = static_cast<int>(rect.left);
  result.top = static_cast<int>(rect.top);
  result.right = static_cast<int>(rect.right);
  result.bottom = static_cast<int>(rect.bottom);
  return result;
}

static float MaxAbsOutset(const Rect& rect) {
  auto outset = std::max(std::max(-rect.left, -rect.top), std::max(rect.right, rect.bottom));
  if (!std::isfinite(outset) || outset <= 0.0f) {
    return 0.0f;
  }
  return std::ceil(outset);
}

static bool FitsLimit(const PixelRect& rect, int limitWidth, int limitHeight) {
  return rect.width() > 0 && rect.height() > 0 && rect.width() <= limitWidth &&
         rect.height() <= limitHeight;
}

}  // namespace

float LayerDrawSession::FilterOutset(Layer* layer, float scale) {
  if (!std::isfinite(scale) || scale <= 0.0f) {
    return 0.0f;
  }
  return MaxAbsOutset(layer->mapOutputBoundsToInput(Rect::MakeEmpty(), scale));
}

float LayerDrawSession::AncestorFilterOutset(Layer* target, Layer* captureRoot,
                                            const Matrix& rootToOutput) {
  auto outset = 0.0f;
  for (auto ancestor = target->parent(); ancestor != nullptr && ancestor != captureRoot;
       ancestor = ancestor->parent()) {
    if (ancestor->filters().empty()) {
      continue;
    }
    auto ancestorToRoot = ancestor->getRelativeMatrix3D(captureRoot).asMatrix();
    ancestorToRoot.postConcat(rootToOutput);
    outset += FilterOutset(ancestor, ancestorToRoot.getMaxScale());
  }
  return outset;
}

bool LayerDrawSession::IsSupported2DSubtree(Layer* layer, std::vector<Layer*>& activeMasks) {
  if (layer->canPreserve3D()) {
    return false;
  }
  auto matrix = layer->matrix3D();
  if (!Matrix3DUtils::IsMatrix3DAffine(matrix) || !matrix.asMatrix().isFinite()) {
    return false;
  }
  for (const auto& child : layer->children()) {
    if (!child->visible() || child->alpha() <= 0 || child->maskOwner) {
      continue;
    }
    if (!IsSupported2DSubtree(child.get(), activeMasks)) {
      return false;
    }
  }
  if (!layer->hasValidMask()) {
    return true;
  }
  auto* mask = layer->mask().get();
  if (std::find(activeMasks.begin(), activeMasks.end(), mask) != activeMasks.end()) {
    return false;
  }
  activeMasks.push_back(mask);
  auto supported = IsSupported2DSubtree(mask, activeMasks);
  activeMasks.pop_back();
  return supported;
}

bool LayerDrawSession::IsSupportedAncestorChain(Layer* target, Layer* captureRoot) {
  auto layer = target;
  while (layer != nullptr) {
    if (layer->canPreserve3D()) {
      return false;
    }
    if (layer == captureRoot) {
      return true;
    }
    layer = layer->parent();
  }
  return false;
}

void LayerDrawSession::CollectLayerEffects(Layer* layer, const Matrix& localToOutput,
                                           float filterOutset, LayerStylePosition position,
                                           std::vector<BackgroundEffect>* effects) {
  for (const auto& style : layer->layerStyles()) {
    if (style->position() != position || !NeedsBackgroundSource(style->extraSourceType())) {
      continue;
    }
    auto localBounds = layer->getBounds();
    if (localBounds.isEmpty()) {
      continue;
    }
    auto bounds = localToOutput.mapRect(localBounds);
    // Round coverage outwards when a float ULP exceeds one pixel at a distant placement.
    if (std::max(std::abs(bounds.left), std::abs(bounds.right)) >= (1 << 24)) {
      bounds.left = std::nextafter(bounds.left, -INFINITY);
      bounds.right = std::nextafter(bounds.right, INFINITY);
    }
    if (std::max(std::abs(bounds.top), std::abs(bounds.bottom)) >= (1 << 24)) {
      bounds.top = std::nextafter(bounds.top, -INFINITY);
      bounds.bottom = std::nextafter(bounds.bottom, INFINITY);
    }
    bounds.outset(filterOutset, filterOutset);
    auto dependency = style->filterBackground(Rect::MakeEmpty(), localToOutput.getMaxScale());
    effects->push_back({bounds, filterOutset + MaxAbsOutset(dependency)});
  }
}

void LayerDrawSession::CollectSubtreeEffects(Layer* layer, const Matrix& localToOutput,
                                             float ancestorOutset,
                                             std::vector<BackgroundEffect>* effects) {
  auto scale = localToOutput.getMaxScale();
  if (!std::isfinite(scale) || scale <= 0.0f) {
    return;
  }
  auto filterOutset = ancestorOutset + FilterOutset(layer, scale);
  CollectLayerEffects(layer, localToOutput, filterOutset, LayerStylePosition::Below, effects);
  for (const auto& child : layer->children()) {
    if (!child->visible() || child->alpha() <= 0 || child->maskOwner) {
      continue;
    }
    auto childMatrix = localToOutput;
    childMatrix.preConcat(child->getMatrixWithScrollRect().asMatrix());
    CollectSubtreeEffects(child.get(), childMatrix, filterOutset, effects);
  }
  CollectLayerEffects(layer, localToOutput, filterOutset, LayerStylePosition::Above, effects);
}

void LayerDrawSession::collectPrecedingEffects() {
  auto* hiddenBranch = BackgroundCapturer::HiddenBranch(request.captureRoot, request.target);
  std::vector<Layer*> path = {};
  for (auto* layer = request.target; layer != request.captureRoot; layer = layer->parent()) {
    path.push_back(layer);
  }
  auto* ancestor = request.captureRoot;
  auto localToOutput = rootToOutput;
  auto filterOutset = 0.0f;
  for (auto it = path.rbegin(); it != path.rend(); ++it) {
    if (ancestor == hiddenBranch) {
      break;
    }
    filterOutset += FilterOutset(ancestor, localToOutput.getMaxScale());
    CollectLayerEffects(ancestor, localToOutput, filterOutset, LayerStylePosition::Below,
                        &precedingEffects);
    for (const auto& sibling : ancestor->children()) {
      if (sibling.get() == *it) {
        break;
      }
      if (!sibling->visible() || sibling->alpha() <= 0 || sibling->maskOwner) {
        continue;
      }
      auto siblingToOutput = localToOutput;
      siblingToOutput.preConcat(sibling->getMatrixWithScrollRect().asMatrix());
      CollectSubtreeEffects(sibling.get(), siblingToOutput, filterOutset, &precedingEffects);
    }
    ancestor = *it;
    localToOutput.preConcat(ancestor->getMatrixWithScrollRect().asMatrix());
  }
}

void DrawControl::fail(DrawFailure failure) {
  if (failure.code == DrawFailureCode::None || failed()) {
    return;
  }
  firstFailure = std::move(failure);
}

void DrawControl::fail(DrawFailureCode code, DrawStage stage, Layer* layer, ISize surfaceSize,
                       std::string message) {
  DrawFailure failure = {};
  failure.code = code;
  failure.stage = stage;
  failure.layer = layer;
  failure.surfaceSize = surfaceSize;
  failure.message = std::move(message);
  fail(std::move(failure));
}

class LayerDrawBatch::Impl {
 public:
  std::vector<LayerRegionPlan> regions = {};
  std::vector<Rect> renderRects = {};
  BackgroundSnapshotMap snapshots = {};
  bool needsBackground = false;
  bool hasSnapshots = false;
};

LayerDrawBatch::LayerDrawBatch() : impl(new Impl()) {
}

LayerDrawBatch::~LayerDrawBatch() = default;

size_t LayerDrawBatch::regionCount() const {
  return impl->regions.size();
}

ISize LayerDrawSession::ResolveSurfaceLimit(const LayerDrawLimits& limits) {
  auto deviceLimit = limits.gpuLimits.maxTextureDimension2D;
  if (deviceLimit <= 0) {
    return {};
  }
  ISize result = {std::min(deviceLimit, DefaultExportSurfaceLimit),
                  std::min(deviceLimit, DefaultExportSurfaceLimit)};
  if (limits.outputSurfaceLimit.width > 0 && limits.outputSurfaceLimit.height > 0) {
    result.width = std::min(deviceLimit, limits.outputSurfaceLimit.width);
    result.height = std::min(deviceLimit, limits.outputSurfaceLimit.height);
  }
  return result;
}

std::unique_ptr<LayerDrawSession> LayerDrawSession::Prepare(Context* context,
                                                            const LayerDrawRequest& request,
                                                            DrawControl* control) {
  if (request.target == nullptr || request.captureRoot == nullptr) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Validation, nullptr, {},
                    "missing target or capture root");
    }
    return nullptr;
  }
  if (!request.captureRoot->doContains(request.target)) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Validation, request.target, {},
                    "capture root does not contain the target");
    }
    return nullptr;
  }
  auto sceneRoot = request.target->root();
  bool validCaptureRoot = sceneRoot != nullptr ? request.captureRoot == sceneRoot
                                               : request.captureRoot == request.target ||
                                                     !request.captureRoot->parent();
  if (request.samplingMode == LayerSamplingMode::ExportFixed && !validCaptureRoot) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Validation, request.target, {},
                    "the scene root must be the real root of the target");
    }
    return nullptr;
  }
  if (request.samplingMode == LayerSamplingMode::ExportFixed &&
      (!std::isfinite(request.alpha) || request.alpha < 0.0f || request.alpha > 1.0f)) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Validation, request.target, {},
                    "alpha is not a finite value in [0, 1]");
    }
    return nullptr;
  }

  std::unique_ptr<LayerDrawSession> session(new LayerDrawSession());
  session->request = request;

  if (request.samplingMode == LayerSamplingMode::CanvasDefault) {
    auto localToRoot = request.target->getGlobalMatrix();
    Matrix3D rootToLocal = {};
    session->canvasCanInvert = localToRoot.invert(&rootToLocal);
    if (request.canvasRooted && session->canvasCanInvert) {
      request.captureRoot->updateRenderBounds();
      session->canvasRenderRect = localToRoot.mapRect(request.canvasDrawBounds);
      session->canvasRenderRects = {session->canvasRenderRect};
    }
    session->needsBackground = session->canvasCanInvert && !request.target->canPreserve3D() &&
                               request.target->hasBackgroundStyle();
    if (session->needsBackground && request.context != nullptr) {
      auto bounds = request.target->getBounds();
      auto fullLayer = !(request.target->maxBackgroundOutset > 0.0f);
      auto rectForDraw = fullLayer ? bounds : request.canvasDrawBounds;
      session->canvasFullLayerBackground = rectForDraw == bounds;
      session->canvasBackgroundDrawRect = request.canvasMatrix.mapRect(rectForDraw);
      session->canvasBackgroundMatrix = request.canvasMatrix;
      if (request.canvasRooted) {
        session->canvasBackgroundMatrix.preConcat(rootToLocal.asMatrix());
      }
      session->canvasCaptureCullRect =
          request.canvasRooted ? session->canvasRenderRect : request.canvasDrawBounds;
    }
    return session;
  }

  if (context == nullptr || context->gpu() == nullptr) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Validation, request.target, {},
                    "a usable GPU context is required");
    }
    return nullptr;
  }
  if (!Matrix3DUtils::IsMatrix3DAffine(request.target->getRelativeMatrix3D(request.captureRoot))) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::UnsupportedContent, DrawStage::Validation, request.target, {},
                    "target is inside a 3D rendering context");
    }
    return nullptr;
  }
  auto targetToRoot = request.target->getRelativeMatrix3D(request.captureRoot).asMatrix();
  if (!targetToRoot.isFinite()) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Validation, request.target, {},
                    "the target to capture-root matrix is not finite");
    }
    return nullptr;
  }
  auto outputMatrix = request.targetLocalToOutput;
  if (!outputMatrix.isFinite() || outputMatrix.hasPerspective()) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::UnsupportedContent, DrawStage::Validation, request.target, {},
                    "the output matrix is not a finite 2D affine transform");
    }
    return nullptr;
  }
  if (!outputMatrix.invertible()) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Validation, request.target, {},
                    "the output matrix is not invertible");
    }
    return nullptr;
  }
  if (request.outputSize.width <= 0 || request.outputSize.height <= 0) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Validation, request.target, {},
                    "the output size is invalid");
    }
    return nullptr;
  }
  std::vector<Layer*> activeMasks = {};
  if (!IsSupportedAncestorChain(request.target, request.captureRoot) ||
      !IsSupported2DSubtree(request.target, activeMasks)) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::UnsupportedContent, DrawStage::Validation, request.target, {},
                    "the tiled export does not support perspective, preserve-3D or cyclic masks");
    }
    return nullptr;
  }

  session->outputPixelBounds = {0, 0, request.outputSize.width, request.outputSize.height};
  session->useSceneBounds =
      request.target->_scrollRect == nullptr &&
      BackgroundCapturer::HiddenBranch(request.captureRoot, request.target) == nullptr;

  auto actualRoot = request.captureRoot->root();
  if (actualRoot != nullptr) {
    actualRoot->updateRenderBounds();
  }
  if (request.alpha > 0.0f) {
    CollectSubtreeEffects(request.target, outputMatrix, 0.0f, &session->targetEffects);
    session->needsBackground = !session->targetEffects.empty();
  }
  // The export never keeps a borrowed context beyond the lock it was created in; every later call
  // receives the context it must use.
  session->request.context = nullptr;
  if (!session->needsBackground) {
    return session;
  }

  Matrix rootToTarget = Matrix::I();
  if (!targetToRoot.invert(&rootToTarget)) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Validation, request.target, {},
                    "the target to capture-root matrix is not invertible");
    }
    return nullptr;
  }
  auto rootToOutput = outputMatrix;
  rootToOutput.preConcat(rootToTarget);
  if (!rootToOutput.isFinite() || rootToOutput.hasPerspective() || !rootToOutput.invertible()) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::UnsupportedContent, DrawStage::Validation, request.target, {},
                    "the capture-root to output matrix is not a finite 2D affine transform");
    }
    return nullptr;
  }
  session->rootToOutput = rootToOutput;
  if (!rootToOutput.invert(&session->outputToRoot)) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Validation, request.target, {},
                    "the capture-root to output matrix is not invertible");
    }
    return nullptr;
  }
  auto captureRootToGlobal = request.captureRoot->getGlobalMatrix().asMatrix();
  if (!captureRootToGlobal.isFinite() || !captureRootToGlobal.invertible()) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::UnsupportedContent, DrawStage::Validation, request.target, {},
                    "the capture-root to scene matrix is not a finite 2D affine transform");
    }
    return nullptr;
  }
  session->outputToGlobal = captureRootToGlobal;
  session->outputToGlobal.preConcat(session->outputToRoot);
  auto outset = session->useSceneBounds
                    ? AncestorFilterOutset(request.target, request.captureRoot, rootToOutput)
                    : 0.0f;
  for (auto& effect : session->targetEffects) {
    effect.outputBounds.outset(outset, outset);
    effect.outset += outset;
  }
  session->collectPrecedingEffects();
  return session;
}

RegionPlanResult LayerDrawSession::planRegion(const PixelRect& core, const LayerDrawLimits& limits,
                                              DrawControl* control) const {
  if (request.samplingMode == LayerSamplingMode::CanvasDefault) {
    return planCanvasRegion(control);
  }
  return planExportRegion(core, limits, control);
}

RegionPlanResult LayerDrawSession::planCanvasRegion(DrawControl* control) const {
  RegionPlanResult result = {};
  if (control != nullptr && control->cancelled()) {
    result.code = RegionPlanCode::Cancelled;
    return result;
  }
  result.code = RegionPlanCode::Ready;
  if (needsBackground) {
    auto dependencies = std::make_shared<RegionDependencyPlan>();
    dependencies->needsBackground = true;
    result.region.dependencies = std::move(dependencies);
  }
  return result;
}

RegionPlanResult LayerDrawSession::planExportRegion(const PixelRect& core,
                                                    const LayerDrawLimits& limits,
                                                    DrawControl* control) const {
  RegionPlanResult result = {};
  if (control != nullptr && control->cancelled()) {
    result.code = RegionPlanCode::Cancelled;
    return result;
  }
  if (core.isEmpty() || core.left < outputPixelBounds.left || core.top < outputPixelBounds.top ||
      core.right > outputPixelBounds.right || core.bottom > outputPixelBounds.bottom) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Planning, request.target, {},
                    "core region is empty or outside the output bounds");
    }
    result.code = RegionPlanCode::Failed;
    return result;
  }
  auto surfaceLimit = ResolveSurfaceLimit(limits);
  // Keep one pixel of margin around the core region so antialiasing and sampling at the delivery
  // edge stay inside the drawn surface. A background-sourced effect also needs the same margin
  // inside the drawn surface: its input comes from the shared capture, and a nested offscreen
  // clipped exactly at the delivery edge would truncate that input. The capture region and the
  // drawn region therefore cover the same dependency window, while only the core is delivered.
  auto outputRect = ToPreciseRect(core);
  outputRect.outset(1, 1);
  bool requiresBackground = false;
  for (const auto* effects : {&targetEffects, &precedingEffects}) {
    if (effects == &precedingEffects && !requiresBackground) {
      break;
    }
    for (auto it = effects->rbegin(); it != effects->rend(); ++it) {
      const auto& bounds = it->outputBounds;
      if (outputRect.right <= bounds.left || outputRect.left >= bounds.right ||
          outputRect.bottom <= bounds.top || outputRect.top >= bounds.bottom) {
        continue;
      }
      // Each style reads earlier drawing, including the output of earlier backdrop styles.
      // Limit secondary expansion to the part of the style visible to the later dependency.
      if (!requiresBackground) {
        outputRect.outset(it->outset, it->outset);
      } else {
        auto overlap = outputRect;
        overlap.left = std::max(overlap.left, static_cast<double>(bounds.left));
        overlap.top = std::max(overlap.top, static_cast<double>(bounds.top));
        overlap.right = std::min(overlap.right, static_cast<double>(bounds.right));
        overlap.bottom = std::min(overlap.bottom, static_cast<double>(bounds.bottom));
        overlap.outset(it->outset, it->outset);
        outputRect.left = std::min(outputRect.left, overlap.left);
        outputRect.top = std::min(outputRect.top, overlap.top);
        outputRect.right = std::max(outputRect.right, overlap.right);
        outputRect.bottom = std::max(outputRect.bottom, overlap.bottom);
      }
      requiresBackground = true;
    }
  }
  outputRect.roundOut();
  if (!IsSafePixelRect(outputRect)) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::SurfaceLimit, DrawStage::Planning, request.target, {},
                    "the region exceeds the supported pixel range");
    }
    result.code = RegionPlanCode::Failed;
    return result;
  }
  auto renderRect = ToPixelRect(outputRect);
  renderRect.left =
      AlignedOrigin(renderRect.left, renderRect.right, core.width(), surfaceLimit.width);
  renderRect.top =
      AlignedOrigin(renderRect.top, renderRect.bottom, core.height(), surfaceLimit.height);
  if (!FitsLimit(renderRect, surfaceLimit.width, surfaceLimit.height)) {
    if (core.width() <= 1 && core.height() <= 1) {
      if (control != nullptr) {
        control->fail(
            DrawFailureCode::SurfaceLimit, DrawStage::Planning, request.target,
            ISize::Make(renderRect.width(), renderRect.height()),
            "a single output pixel needs a larger drawing surface than the device allows");
      }
      result.code = RegionPlanCode::Failed;
      return result;
    }
    result.code = RegionPlanCode::RequiresSubdivision;
    return result;
  }
  result.region.coreRect = core;
  result.region.renderRect = renderRect;
  if (requiresBackground) {
    auto dependencies = std::make_shared<RegionDependencyPlan>();
    dependencies->needsBackground = true;
    dependencies->captureRect = renderRect;
    result.region.dependencies = std::move(dependencies);
  }
  result.code = RegionPlanCode::Ready;
  return result;
}

BatchPlanResult LayerDrawSession::planBatch(const std::vector<LayerRegionPlan>& regions,
                                            const LayerDrawLimits& limits,
                                            DrawControl* control) const {
  BatchPlanResult result = {};
  if (control != nullptr && control->cancelled()) {
    result.code = BatchPlanCode::Cancelled;
    return result;
  }
  auto plan = std::make_shared<LayerDrawBatchPlan>();
  plan->regions = regions;
  for (const auto& region : regions) {
    if (region.dependencies == nullptr || !region.dependencies->needsBackground) {
      continue;
    }
    auto& captureRect = plan->captureRect;
    auto& dependencyRect = region.dependencies->captureRect;
    if (!plan->needsBackground) {
      captureRect = dependencyRect;
      plan->needsBackground = true;
      continue;
    }
    captureRect.left = std::min(captureRect.left, dependencyRect.left);
    captureRect.top = std::min(captureRect.top, dependencyRect.top);
    captureRect.right = std::max(captureRect.right, dependencyRect.right);
    captureRect.bottom = std::max(captureRect.bottom, dependencyRect.bottom);
  }
  auto deviceLimit = limits.gpuLimits.maxTextureDimension2D;
  if (request.samplingMode == LayerSamplingMode::ExportFixed && plan->needsBackground &&
      (deviceLimit <= 0 || plan->captureRect.width() > deviceLimit ||
       plan->captureRect.height() > deviceLimit)) {
    if (regions.size() <= 1) {
      if (control != nullptr) {
        control->fail(DrawFailureCode::SurfaceLimit, DrawStage::Planning, request.target,
                      ISize::Make(plan->captureRect.width(), plan->captureRect.height()),
                      "the shared background capture exceeds the device texture limit");
      }
      result.code = BatchPlanCode::Failed;
      return result;
    }
    result.code = BatchPlanCode::RequiresSplit;
    return result;
  }
  result.code = BatchPlanCode::Ready;
  result.plan = std::move(plan);
  return result;
}

std::unique_ptr<LayerDrawBatch> LayerDrawSession::prepareBatch(Context* context,
                                                               const LayerDrawBatchPlan& plan,
                                                               DrawControl* control) {
  if (request.samplingMode == LayerSamplingMode::CanvasDefault) {
    return prepareCanvasBatch(context, control);
  }
  return prepareExportBatch(context, plan, control);
}

std::unique_ptr<LayerDrawBatch> LayerDrawSession::prepareCanvasBatch(Context* context,
                                                                     DrawControl* control) {
  (void)context;
  auto batch = std::unique_ptr<LayerDrawBatch>(new LayerDrawBatch());
  batch->impl->needsBackground = needsBackground;
  if (!needsBackground || request.context == nullptr) {
    return batch;
  }
  auto bgSource = request.target->createBackgroundSource(
      request.context, canvasBackgroundDrawRect, canvasBackgroundMatrix, canvasFullLayerBackground,
      request.canvasColorSpace);
  if (bgSource == nullptr) {
    // The regular draw path keeps its existing behavior: the consumer then synthesizes the
    // backdrop on the fly, or skips the style on a picture-only canvas.
    return batch;
  }
  DrawArgs args(request.canvasCacheEnabled ? request.context : nullptr);
  args.dstColorSpace = request.canvasColorSpace;
  args.drawControl = control;
  BackgroundCapturer::Run(request.captureRoot, args, std::move(bgSource), &batch->impl->snapshots,
                          {canvasCaptureCullRect});
  batch->impl->hasSnapshots = true;
  return batch;
}

std::unique_ptr<LayerDrawBatch> LayerDrawSession::prepareExportBatch(Context* context,
                                                                     const LayerDrawBatchPlan& plan,
                                                                     DrawControl* control) {
  auto batch = std::unique_ptr<LayerDrawBatch>(new LayerDrawBatch());
  batch->impl->regions = plan.regions;
  batch->impl->needsBackground = plan.needsBackground;
  // Style sources stay as lazy pictures: rasterizing a full-content recording would allocate a
  // texture the size of the whole layer, while every output region samples only its own part.
  batch->impl->snapshots.rasterizeStyleSources = false;
  if (control != nullptr && control->failed()) {
    return nullptr;
  }
  if (!plan.needsBackground) {
    return batch;
  }
  auto& captureRect = plan.captureRect;
  if (captureRect.isEmpty()) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::MissingDependency, DrawStage::Background, request.target, {},
                    "background capture has no output region");
    }
    return nullptr;
  }
  ISize captureSize = {captureRect.width(), captureRect.height()};
  BackgroundRasterGeometry geometry = {};
  geometry.size = captureSize;
  geometry.surfaceScale = ExportBackgroundSurfaceScale;
  geometry.sceneRootToOutput = rootToOutput;
  geometry.originX = captureRect.left;
  geometry.originY = captureRect.top;

  auto bgSource = BackgroundSource::MakeWithGeometry(context, geometry, request.renderColorSpace);
  if (bgSource == nullptr) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::AllocationFailure, DrawStage::Background, request.target,
                    captureSize, "failed to allocate the background capture surface");
    }
    return nullptr;
  }
  if (request.clearColor != Color::Transparent()) {
    bgSource->getCanvas()->drawColor(request.clearColor, BlendMode::SrcOver);
  }
  DrawArgs args(context);
  args.dstColorSpace = request.renderColorSpace;
  args.drawControl = control;
  // The capture and consume traversals must cull with the same rect, otherwise a style culled
  // during capture would still be dispatched while consuming and find no snapshot. renderBounds
  // already carries every ancestor effect outset, so this rect is safe for the consume pass too.
  auto cullRect = outputToGlobal.mapRect(ToRect(captureRect));
  cullRect.roundOut();
  cullRect.outset(1.0f, 1.0f);
  batch->impl->renderRects = {cullRect};
  BackgroundCapturer::RunForTarget(request.captureRoot, request.target, request.alpha,
                                   request.blendMode, args, std::move(bgSource),
                                   &batch->impl->snapshots, batch->impl->renderRects);
  if (control != nullptr && control->failed()) {
    return nullptr;
  }
  batch->impl->hasSnapshots = true;
  return batch;
}

DrawOutcome LayerDrawSession::drawRegion(Context* context, LayerDrawBatch& batch,
                                         size_t regionIndex, const LayerDrawTarget& target,
                                         DrawControl* control) {
  if (control != nullptr && control->cancelled()) {
    return DrawOutcome::Cancelled;
  }
  if (control != nullptr && control->failed()) {
    return DrawOutcome::Failed;
  }
  if (request.samplingMode == LayerSamplingMode::CanvasDefault) {
    return drawCanvasRegion(batch, target, control);
  }
  return drawExportRegion(context, batch, regionIndex, target, control);
}

DrawOutcome LayerDrawSession::drawCanvasRegion(LayerDrawBatch& batch, const LayerDrawTarget& target,
                                               DrawControl* control) {
  if (target.canvas == nullptr) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Rendering, request.target, {},
                    "the draw target has no canvas");
    }
    return DrawOutcome::Failed;
  }
  auto canvas = target.canvas;
  AutoCanvasRestore autoRestore(canvas);
  DrawArgs args(request.canvasCacheEnabled ? request.context : nullptr);
  args.dstColorSpace = request.canvasColorSpace;
  args.drawControl = control;
  if (!canvasRenderRects.empty()) {
    args.renderRects = &canvasRenderRects;
  }
  BackgroundConsumer consumer(batch.impl->hasSnapshots ? &batch.impl->snapshots : nullptr, false);
  args.backgroundHandler = batch.impl->needsBackground ? static_cast<BackgroundHandler*>(&consumer)
                                                       : BackgroundHandler::NoOp();
  if (request.target->canPreserve3D()) {
    request.target->drawByStarting3DContext(args, canvas, request.target->getMatrixWithScrollRect(),
                                            &Layer::drawLayer, request.alpha);
  } else {
    request.target->drawLayer(args, canvas, request.alpha, request.blendMode);
  }
  if (control != nullptr) {
    if (control->failed()) {
      return DrawOutcome::Failed;
    }
    if (control->cancelled()) {
      return DrawOutcome::Cancelled;
    }
  }
  return DrawOutcome::Success;
}

DrawOutcome LayerDrawSession::drawExportRegion(Context* context, LayerDrawBatch& batch,
                                               size_t regionIndex, const LayerDrawTarget& target,
                                               DrawControl* control) {
  if (target.canvas == nullptr || context == nullptr) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Rendering, request.target, {},
                    "the draw target or context is missing");
    }
    return DrawOutcome::Failed;
  }
  if (regionIndex >= batch.impl->regions.size()) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Rendering, request.target, {},
                    "region index is outside the batch");
    }
    return DrawOutcome::Failed;
  }
  auto canvas = target.canvas;
  AutoCanvasRestore autoRestore(canvas);
  if (target.clearBeforeDraw) {
    canvas->clear(request.clearColor);
  }
  if (auto surface = canvas->getSurface()) {
    canvas->clipRect(
        Rect::MakeWH(static_cast<float>(surface->width()), static_cast<float>(surface->height())),
        false);
  }
  const auto& region = batch.impl->regions[regionIndex];
  canvas->setMatrixForTile(request.targetLocalToOutput, region.renderRect.left,
                           region.renderRect.top);

  DrawArgs args(context);
  args.dstColorSpace = request.renderColorSpace;
  args.drawControl = control;
  bool needsRegionBackground = region.dependencies != nullptr;
  if (needsRegionBackground) {
    args.useSceneBounds = useSceneBounds;
    args.renderRects = &batch.impl->renderRects;
  }
  BackgroundConsumer consumer(batch.impl->hasSnapshots ? &batch.impl->snapshots : nullptr, false);
  args.backgroundHandler = needsRegionBackground ? static_cast<BackgroundHandler*>(&consumer)
                                                 : BackgroundHandler::NoOp();
  // Layer::draw() returns before drawing for a zero alpha. Skipping the draw here keeps that
  // semantic, so a Clear or Src blend mode cannot erase the cleared output.
  if (request.alpha > 0.0f) {
    request.target->drawLayer(args, canvas, request.alpha, request.blendMode);
  }
  if (control != nullptr) {
    if (control->failed()) {
      return DrawOutcome::Failed;
    }
    if (control->cancelled()) {
      return DrawOutcome::Cancelled;
    }
  }
  return DrawOutcome::Success;
}

}  // namespace tgfx

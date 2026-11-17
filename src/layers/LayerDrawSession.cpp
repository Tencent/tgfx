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
#include "tgfx/layers/filters/LayerFilter.h"
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

static bool IsFiniteRect(const Rect& rect) {
  return std::isfinite(rect.left) && std::isfinite(rect.top) && std::isfinite(rect.right) &&
         std::isfinite(rect.bottom);
}

static bool IsSafePixelRect(const Rect& rect) {
  constexpr float MaxPixelDimension = static_cast<float>(INT32_MAX >> 2);
  return IsFiniteRect(rect) && rect.width() <= MaxPixelDimension &&
         rect.height() <= MaxPixelDimension;
}

static Rect ToRect(const PixelRect& rect) {
  return Rect::MakeLTRB(static_cast<float>(rect.left), static_cast<float>(rect.top),
                        static_cast<float>(rect.right), static_cast<float>(rect.bottom));
}

static PixelRect ToPixelRect(const Rect& rect) {
  auto rounded = rect;
  rounded.roundOut();
  PixelRect result;
  result.left = static_cast<int>(rounded.left);
  result.top = static_cast<int>(rounded.top);
  result.right = static_cast<int>(rounded.right);
  result.bottom = static_cast<int>(rounded.bottom);
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

bool LayerDrawSession::IsSupported2DSubtree(Layer* layer) {
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
    if (!IsSupported2DSubtree(child.get())) {
      return false;
    }
  }
  auto mask = layer->mask();
  if (mask != nullptr && !IsSupported2DSubtree(mask.get())) {
    return false;
  }
  return true;
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

void LayerDrawSession::ScanBackgroundOutset(Layer* layer, const Matrix& localToRoot,
                                            float* maxOutset, bool* needsBackground) {
  auto scale = localToRoot.getMaxScale();
  if (!std::isfinite(scale) || scale <= 0.0f) {
    return;
  }
  float backOutset = 0.0f;
  for (const auto& style : layer->layerStyles()) {
    if (style == nullptr || !NeedsBackgroundSource(style->extraSourceType())) {
      continue;
    }
    *needsBackground = true;
    auto bounds = style->filterBackground(Rect::MakeEmpty(), scale);
    backOutset = std::max(backOutset, MaxAbsOutset(bounds));
  }
  if (backOutset > 0.0f && !layer->filters().empty()) {
    auto baseBounds = layer->mapOutputBoundsToInput(Rect::MakeEmpty(), scale);
    backOutset += MaxAbsOutset(baseBounds);
  }
  *maxOutset = std::max(*maxOutset, backOutset);
  for (const auto& child : layer->children()) {
    if (!child->visible() || child->alpha() <= 0 || child->maskOwner) {
      continue;
    }
    auto childMatrix = localToRoot;
    childMatrix.preConcat(child->getMatrixWithScrollRect().asMatrix());
    ScanBackgroundOutset(child.get(), childMatrix, maxOutset, needsBackground);
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
  if (!request.captureRoot->contains(request.target->shared_from_this())) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Validation, request.target, {},
                    "capture root does not contain the target");
    }
    return nullptr;
  }
  // A middle ancestor would place renderBounds and the capture geometry in different coordinate
  // spaces, so the export accepts only the real root or the detached target itself.
  if (request.samplingMode == LayerSamplingMode::ExportFixed &&
      request.captureRoot->root() != nullptr &&
      request.captureRoot->root() != request.captureRoot) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Validation, request.target, {},
                    "the scene root must be the real root of the target");
    }
    return nullptr;
  }
  if (!std::isfinite(request.alpha) || request.alpha < 0.0f || request.alpha > 1.0f) {
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
    auto deviceBounds = request.canvasMatrix.mapRect(request.canvasDrawBounds);
    deviceBounds.roundOut();
    session->outputPixelBounds = ToPixelRect(deviceBounds);
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
  if (!targetToRoot.isFinite() || !targetToRoot.invertible()) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Validation, request.target, {},
                    "the target to capture-root matrix is not invertible");
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
  auto logicalBounds = request.logicalOutputBounds;
  if (!std::isfinite(logicalBounds.left) || !std::isfinite(logicalBounds.top) ||
      !std::isfinite(logicalBounds.right) || !std::isfinite(logicalBounds.bottom) ||
      logicalBounds.isEmpty()) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Validation, request.target, {},
                    "the logical output bounds are invalid");
    }
    return nullptr;
  }
  if (!IsSupportedAncestorChain(request.target, request.captureRoot) ||
      !IsSupported2DSubtree(request.target)) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::UnsupportedContent, DrawStage::Validation, request.target, {},
                    "perspective or preserve-3D content is not supported by the tiled export");
    }
    return nullptr;
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
  Matrix globalToCaptureRoot = Matrix::I();
  captureRootToGlobal.invert(&globalToCaptureRoot);
  session->globalToOutput = session->rootToOutput;
  session->globalToOutput.preConcat(globalToCaptureRoot);
  session->outputPixelBounds = ToPixelRect(logicalBounds);

  auto actualRoot = request.captureRoot->root();
  if (actualRoot != nullptr) {
    // renderBounds are defined in the real root's space, so update them from there even when the
    // requested capture root is a smaller subtree.
    actualRoot->updateRenderBounds();
    session->needsBackground = request.target->hasBackgroundStyle();
    session->backgroundOutset = request.target->maxBackgroundOutset;
  } else {
    float outset = 0.0f;
    bool needs = false;
    ScanBackgroundOutset(request.captureRoot, Matrix::I(), &outset, &needs);
    session->needsBackground = needs;
    session->backgroundOutset = outset;
  }
  if (!std::isfinite(session->backgroundOutset) || session->backgroundOutset < 0.0f) {
    session->backgroundOutset = 0.0f;
  }
  // The export never keeps a borrowed context beyond the lock it was created in; every later call
  // receives the context it must use.
  session->request.context = nullptr;
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
  result.region.coreRect = outputPixelBounds;
  result.region.renderRect = outputPixelBounds;
  if (needsBackground) {
    auto dependencies = std::make_shared<RegionDependencyPlan>();
    dependencies->needsBackground = true;
    dependencies->captureRect = ToPixelRect(canvasBackgroundDrawRect);
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
  auto globalCore = outputToGlobal.mapRect(ToRect(core));
  if (!IsSafePixelRect(globalCore)) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Planning, request.target, {},
                    "the core region exceeds the supported pixel range");
    }
    result.code = RegionPlanCode::Failed;
    return result;
  }
  PixelRect renderRect = {};
  PixelRect captureRect = {};
  if (needsBackground) {
    auto globalInput = globalCore;
    globalInput.outset(backgroundOutset + 1.0f, backgroundOutset + 1.0f);
    globalInput.roundOut();
    if (!IsSafePixelRect(globalInput)) {
      if (control != nullptr) {
        control->fail(DrawFailureCode::SurfaceLimit, DrawStage::Planning, request.target, {},
                      "the background dependency exceeds the supported pixel range");
      }
      result.code = RegionPlanCode::Failed;
      return result;
    }
    auto outputRect = globalToOutput.mapRect(globalInput);
    outputRect.roundOut();
    if (!IsSafePixelRect(outputRect)) {
      if (control != nullptr) {
        control->fail(DrawFailureCode::SurfaceLimit, DrawStage::Planning, request.target, {},
                      "the background dependency exceeds the supported pixel range");
      }
      result.code = RegionPlanCode::Failed;
      return result;
    }
    renderRect = ToPixelRect(outputRect);
    captureRect = renderRect;
  } else {
    renderRect = ToPixelRect(ToRect(core).makeOutset(1.0f, 1.0f));
  }
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
  if (needsBackground) {
    auto dependencies = std::make_shared<RegionDependencyPlan>();
    dependencies->needsBackground = true;
    auto deviceLimit = limits.gpuLimits.maxTextureDimension2D;
    if (deviceLimit <= 0 || captureRect.width() > deviceLimit ||
        captureRect.height() > deviceLimit) {
      if (core.width() <= 1 && core.height() <= 1) {
        if (control != nullptr) {
          control->fail(DrawFailureCode::SurfaceLimit, DrawStage::Planning, request.target,
                        ISize::Make(captureRect.width(), captureRect.height()),
                        "the background dependency needs a larger texture than the device allows");
        }
        result.code = RegionPlanCode::Failed;
        return result;
      }
      result.code = RegionPlanCode::RequiresSubdivision;
      return result;
    }
    dependencies->captureRect = captureRect;
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
  Matrix sceneRootToSurface = Matrix::MakeTrans(-static_cast<float>(captureRect.left),
                                                -static_cast<float>(captureRect.top));
  sceneRootToSurface.preConcat(rootToOutput);
  geometry.sceneRootToSurface = sceneRootToSurface;
  Matrix imageToSceneRoot = Matrix::I();
  if (!sceneRootToSurface.invert(&imageToSceneRoot)) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Background, request.target,
                    captureSize, "the background sampling matrix is not invertible");
    }
    return nullptr;
  }
  geometry.imageToSceneRoot = imageToSceneRoot;
  auto coverage = imageToSceneRoot.mapRect(
      Rect::MakeWH(static_cast<float>(captureSize.width), static_cast<float>(captureSize.height)));
  coverage.roundOut();
  geometry.sourceCoverage = coverage;

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
  BackgroundCapturer::Run(request.captureRoot, args, std::move(bgSource), &batch->impl->snapshots,
                          batch->impl->renderRects);
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
  auto matrix = Matrix::MakeTrans(-target.outputOrigin.x, -target.outputOrigin.y);
  matrix.preConcat(request.targetLocalToOutput);
  canvas->setMatrix(matrix);

  DrawArgs args(context);
  args.dstColorSpace = request.renderColorSpace;
  args.drawControl = control;
  if (!batch.impl->renderRects.empty()) {
    args.renderRects = &batch.impl->renderRects;
  }
  BackgroundConsumer consumer(batch.impl->hasSnapshots ? &batch.impl->snapshots : nullptr, false);
  args.backgroundHandler = batch.impl->needsBackground ? static_cast<BackgroundHandler*>(&consumer)
                                                       : BackgroundHandler::NoOp();
  request.target->drawLayer(args, canvas, request.alpha, request.blendMode);
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

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

static Rect ToRect(const PixelRect& rect) {
  return Rect::MakeLTRB(static_cast<float>(rect.left), static_cast<float>(rect.top),
                        static_cast<float>(rect.right), static_cast<float>(rect.bottom));
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

static PreciseRect MapPreciseRect(const Matrix& matrix, const PreciseRect& rect) {
  const double xs[] = {rect.left, rect.right};
  const double ys[] = {rect.top, rect.bottom};
  PreciseRect result = {};
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 2; ++j) {
      auto x = xs[i];
      auto y = ys[j];
      auto mappedX = matrix.getScaleX() * x + matrix.getSkewX() * y + matrix.getTranslateX();
      auto mappedY = matrix.getSkewY() * x + matrix.getScaleY() * y + matrix.getTranslateY();
      if (i == 0 && j == 0) {
        result.left = result.right = mappedX;
        result.top = result.bottom = mappedY;
        continue;
      }
      result.left = std::min(result.left, mappedX);
      result.right = std::max(result.right, mappedX);
      result.top = std::min(result.top, mappedY);
      result.bottom = std::max(result.bottom, mappedY);
    }
  }
  return result;
}

static bool IsSafePixelRect(const PreciseRect& rect) {
  constexpr double MaxPixelCoordinate = static_cast<double>(INT32_MAX >> 2);
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

static float FilterOutset(Layer* layer, float scale) {
  if (!std::isfinite(scale) || scale <= 0.0f) {
    return 0.0f;
  }
  auto& filters = layer->filters();
  auto bounds = Rect::MakeEmpty();
  for (auto index = filters.size(); index > 0; --index) {
    auto& filter = filters[index - 1];
    if (filter != nullptr) {
      bounds = filter->filterBounds(bounds, scale, MapDirection::Reverse);
    }
  }
  return MaxAbsOutset(bounds);
}

static float AncestorFilterOutset(Layer* target, Layer* captureRoot) {
  auto outset = 0.0f;
  for (auto ancestor = target->parent(); ancestor != nullptr && ancestor != captureRoot;
       ancestor = ancestor->parent()) {
    if (ancestor->filters().empty()) {
      continue;
    }
    auto ancestorToRoot = ancestor->getRelativeMatrix3D(captureRoot).asMatrix();
    outset += FilterOutset(ancestor, ancestorToRoot.getMaxScale());
  }
  return outset;
}

static bool FitsLimit(const PixelRect& rect, int limitWidth, int limitHeight) {
  return rect.width() > 0 && rect.height() > 0 && rect.width() <= limitWidth &&
         rect.height() <= limitHeight;
}

}  // namespace

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

void LayerDrawSession::ScanBackgroundOutset(Layer* layer, const Matrix& localToRoot,
                                            float ancestorOutset, float* maxOutset,
                                            bool* needsBackground) {
  auto scale = localToRoot.getMaxScale();
  if (!std::isfinite(scale) || scale <= 0.0f) {
    return;
  }
  // A background style reads the backdrop after every enclosing filter has spread it, so the input
  // it needs covers the filter outsets of the whole ancestor chain, not only the style's own
  // outset. Missing them truncates the capture and leaves band seams.
  auto filterOutset = ancestorOutset + FilterOutset(layer, scale);
  float backOutset = 0.0f;
  for (const auto& style : layer->layerStyles()) {
    if (style == nullptr || !NeedsBackgroundSource(style->extraSourceType())) {
      continue;
    }
    *needsBackground = true;
    auto bounds = style->filterBackground(Rect::MakeEmpty(), scale);
    backOutset = std::max(backOutset, filterOutset + MaxAbsOutset(bounds));
  }
  *maxOutset = std::max(*maxOutset, backOutset);
  for (const auto& child : layer->children()) {
    if (!child->visible() || child->alpha() <= 0 || child->maskOwner) {
      continue;
    }
    auto childMatrix = localToRoot;
    childMatrix.preConcat(child->getMatrixWithScrollRect().asMatrix());
    ScanBackgroundOutset(child.get(), childMatrix, filterOutset, maxOutset, needsBackground);
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
  session->outputPixelBounds = {0, 0, request.outputSize.width, request.outputSize.height};

  auto actualRoot = request.captureRoot->root();
  if (actualRoot != nullptr) {
    // renderBounds are defined in the real root's space, so update them from there even when the
    // requested capture root is a smaller subtree.
    actualRoot->updateRenderBounds();
    session->needsBackground = request.target->hasBackgroundStyle();
    auto ancestorOutset = AncestorFilterOutset(request.target, request.captureRoot);
    float outset = 0.0f;
    bool needs = false;
    ScanBackgroundOutset(request.target, targetToRoot, ancestorOutset, &outset, &needs);
    session->needsBackground = session->needsBackground || needs;
    session->backgroundOutset =
        std::max(outset, request.target->maxBackgroundOutset + ancestorOutset);
  } else {
    float outset = 0.0f;
    bool needs = false;
    ScanBackgroundOutset(request.captureRoot, Matrix::I(), 0.0f, &outset, &needs);
    session->needsBackground = needs;
    session->backgroundOutset = outset;
  }
  if (!std::isfinite(session->backgroundOutset) || session->backgroundOutset < 0.0f) {
    session->backgroundOutset = 0.0f;
  }
  // Layer::draw() returns before drawing anything for a zero alpha, so the export must not need a
  // backdrop either: nothing is sampled and the delivered band stays the clear color.
  if (request.alpha <= 0.0f) {
    session->needsBackground = false;
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
  PixelRect renderRect = {};
  PixelRect captureRect = {};
  if (needsBackground) {
    auto globalInput = MapPreciseRect(outputToGlobal, ToPreciseRect(core));
    if (!globalInput.isFinite()) {
      if (control != nullptr) {
        control->fail(DrawFailureCode::InvalidRequest, DrawStage::Planning, request.target, {},
                      "the core region has nonfinite scene coordinates");
      }
      result.code = RegionPlanCode::Failed;
      return result;
    }
    auto margin = static_cast<double>(backgroundOutset) + 1.0;
    globalInput.outset(margin, margin);
    globalInput.roundOut();
    if (!globalInput.isFinite()) {
      if (control != nullptr) {
        control->fail(DrawFailureCode::SurfaceLimit, DrawStage::Planning, request.target, {},
                      "the background dependency has nonfinite scene coordinates");
      }
      result.code = RegionPlanCode::Failed;
      return result;
    }
    auto outputRect = MapPreciseRect(globalToOutput, globalInput);
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
    renderRect = core;
    renderRect.left--;
    renderRect.top--;
    renderRect.right++;
    renderRect.bottom++;
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

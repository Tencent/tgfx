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
#include "layers/BackgroundHandler.h"
#include "layers/BackgroundSnapshotMap.h"
#include "layers/BackgroundSource.h"
#include "layers/DrawArgs.h"
#include "layers/LayerStyleSource.h"
#include "layers/contents/LayerContent.h"
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

// Bound the output drawing surfaces even on devices with larger textures.
static constexpr int DefaultExportSurfaceLimit = 2048;

// The capture surface is single-sample RGBA_8888 without mipmaps.
static constexpr int MaxBackgroundCaptureBytes = 32 * 1024 * 1024;
static constexpr int BackgroundCaptureBytesPerPixel = 4;

static constexpr char UnsupportedCoordinates[] =
    "tiled export coordinates must be within [-2^23, 2^23] pixels";
static constexpr char Unsupported3D[] = "the tiled export does not support 3D scenes";

static bool IsSupportedCoordinate(double coordinate) {
  return std::abs(coordinate) <= MaxLayerExportCoordinate;
}

static bool HasSupportedTranslation(const Matrix& matrix) {
  return IsSupportedCoordinate(matrix.getTranslateX()) &&
         IsSupportedCoordinate(matrix.getTranslateY());
}

static bool HasSupportedBounds(const Rect& bounds) {
  return IsSupportedCoordinate(bounds.left) && IsSupportedCoordinate(bounds.top) &&
         IsSupportedCoordinate(bounds.right) && IsSupportedCoordinate(bounds.bottom);
}

static Rect ToRect(const PixelRect& rect) {
  return Rect::MakeLTRB(static_cast<float>(rect.left), static_cast<float>(rect.top),
                        static_cast<float>(rect.right), static_cast<float>(rect.bottom));
}

static Matrix MakeTileMatrix(Matrix localToOutput, const PixelRect& region) {
  localToOutput.postTranslate(-static_cast<float>(region.left), -static_cast<float>(region.top));
  return localToOutput;
}

// Keep dependency outsets exact while adding them to the integer output grid before rounding.
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
  // The sampling margin can extend beyond the output's right and bottom edges. Those integral
  // edges still convert exactly to float up to 2^24.
  constexpr double MaxExactPixelEdge = 1 << 24;
  return rect.isFinite() && IsSupportedCoordinate(rect.left) && IsSupportedCoordinate(rect.top) &&
         rect.right <= MaxExactPixelEdge && rect.bottom <= MaxExactPixelEdge;
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

static const char* BackgroundCaptureLimitReason(const PixelRect& rect, int deviceLimit) {
  if (!FitsLimit(rect, deviceLimit, deviceLimit)) {
    return "the background capture exceeds the device texture limit";
  }
  auto pixelCount = static_cast<int64_t>(rect.width()) * rect.height();
  if (pixelCount > MaxBackgroundCaptureBytes / BackgroundCaptureBytesPerPixel) {
    return "the background capture exceeds the 32 MiB budget";
  }
  return nullptr;
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

bool LayerDrawSession::IsSupported2DLayer(const Layer* layer) {
  return !layer->canPreserve3D() && layer->bitFields.matrix3DIsAffine &&
         layer->matrix3D().asMatrix().isFinite();
}

bool LayerDrawSession::IsSupported2DScene(const Layer* layer) {
  if (!IsSupported2DLayer(layer)) {
    return false;
  }
  for (const auto& child : layer->children()) {
    if (!IsSupported2DScene(child.get())) {
      return false;
    }
  }
  return true;
}

const char* LayerDrawSession::UnsupportedSubtreeReason(Layer* layer,
                                                       std::vector<Layer*>& activeMasks) {
  if (!IsSupported2DLayer(layer)) {
    return Unsupported3D;
  }
  if (auto content = layer->getContent()) {
    if (!HasSupportedBounds(content->getBounds(layer->hasGeometryStyle()))) {
      return UnsupportedCoordinates;
    }
  }
  for (const auto& child : layer->children()) {
    if (!child->visible() || child->alpha() <= 0 || child->maskOwner) {
      continue;
    }
    if (!HasSupportedTranslation(child->getMatrixWithScrollRect().asMatrix()) ||
        (child->_scrollRect && !HasSupportedBounds(*child->_scrollRect))) {
      return UnsupportedCoordinates;
    }
    if (auto reason = UnsupportedSubtreeReason(child.get(), activeMasks)) {
      return reason;
    }
  }
  if (!layer->hasValidMask()) {
    return nullptr;
  }
  auto* mask = layer->mask().get();
  if (std::find(activeMasks.begin(), activeMasks.end(), mask) != activeMasks.end()) {
    return "the tiled export does not support cyclic masks";
  }
  auto maskToLayer = mask->getRelativeMatrix3D(layer);
  if (!Matrix3DUtils::IsMatrix3DAffine(maskToLayer)) {
    return Unsupported3D;
  }
  if (!HasSupportedTranslation(maskToLayer.asMatrix())) {
    return UnsupportedCoordinates;
  }
  activeMasks.push_back(mask);
  auto reason = UnsupportedSubtreeReason(mask, activeMasks);
  activeMasks.pop_back();
  return reason;
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
  auto* captureRoot = sceneRoot ? sceneRoot.get() : request.target;
  std::vector<Layer*> path = {};
  for (auto* layer = request.target; layer != captureRoot; layer = layer->parent()) {
    path.push_back(layer);
  }
  auto* ancestor = captureRoot;
  auto localToOutput = rootToOutput;
  auto filterOutset = 0.0f;
  for (auto it = path.rbegin(); it != path.rend(); ++it) {
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
  if (request.target == nullptr) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Validation, nullptr, {},
                    "missing target");
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

  if (context == nullptr || context->gpu() == nullptr) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::InvalidRequest, DrawStage::Validation, request.target, {},
                    "a usable GPU context is required");
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
  if (request.outputSize.width > MaxLayerExportCoordinate ||
      request.outputSize.height > MaxLayerExportCoordinate ||
      !HasSupportedTranslation(outputMatrix)) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::UnsupportedContent, DrawStage::Validation, request.target, {},
                    UnsupportedCoordinates);
    }
    return nullptr;
  }
  auto* sceneRoot = request.target->root();
  auto* captureRoot = sceneRoot != nullptr ? sceneRoot : request.target;
  // The DisplayList root has no parent and its own visibility attributes do not affect drawing.
  for (auto* layer = request.target; layer != sceneRoot; layer = layer->parent()) {
    if (!layer->visible() || layer->alpha() <= 0.0f || layer->maskOwner != nullptr) {
      if (control != nullptr) {
        control->fail(DrawFailureCode::UnsupportedContent, DrawStage::Validation, layer, {},
                      "the tiled export does not support hidden target branches");
      }
      return nullptr;
    }
    if (layer == captureRoot) {
      break;
    }
  }
  if (!IsSupported2DScene(captureRoot)) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::UnsupportedContent, DrawStage::Validation, request.target, {},
                    Unsupported3D);
    }
    return nullptr;
  }
  std::vector<Layer*> activeMasks = {};
  if (auto reason = UnsupportedSubtreeReason(request.target, activeMasks)) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::UnsupportedContent, DrawStage::Validation, request.target, {},
                    reason);
    }
    return nullptr;
  }

  std::unique_ptr<LayerDrawSession> session(new LayerDrawSession());
  session->request = request;
  session->outputPixelBounds = {0, 0, request.outputSize.width, request.outputSize.height};
  session->useSceneBounds = sceneRoot != nullptr && request.target->_scrollRect == nullptr;

  if (sceneRoot != nullptr) {
    session->sceneRoot = sceneRoot->shared_from_this();
    sceneRoot->updateRenderBounds();
  }
  if (request.alpha > 0.0f) {
    CollectSubtreeEffects(request.target, outputMatrix, 0.0f, &session->targetEffects);
  }
  if (session->targetEffects.empty()) {
    return session;
  }

  auto targetToRoot = request.target->getRelativeMatrix3D(captureRoot).asMatrix();
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
  if (!rootToOutput.isFinite() || rootToOutput.hasPerspective() ||
      !rootToOutput.invert(&session->outputToRoot)) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::UnsupportedContent, DrawStage::Validation, request.target, {},
                    "the capture-root to output matrix is not a finite 2D affine transform");
    }
    return nullptr;
  }
  if (!HasSupportedTranslation(rootToOutput)) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::UnsupportedContent, DrawStage::Validation, request.target, {},
                    UnsupportedCoordinates);
    }
    return nullptr;
  }
  session->rootToOutput = rootToOutput;
  auto outset = session->useSceneBounds
                    ? AncestorFilterOutset(request.target, captureRoot, rootToOutput)
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
  // Keep one pixel for antialiasing and sampling at the delivery edge. Background styles sample
  // their input from snapshots of a separately sized capture surface.
  PixelRect renderRect = {core.left - 1, core.top - 1, core.right + 1, core.bottom + 1};
  auto captureBounds = ToPreciseRect(renderRect);
  bool requiresBackground = false;
  for (const auto* effects : {&targetEffects, &precedingEffects}) {
    if (effects == &precedingEffects && !requiresBackground) {
      break;
    }
    for (auto it = effects->rbegin(); it != effects->rend(); ++it) {
      const auto& bounds = it->outputBounds;
      if (captureBounds.right <= bounds.left || captureBounds.left >= bounds.right ||
          captureBounds.bottom <= bounds.top || captureBounds.top >= bounds.bottom) {
        continue;
      }
      // Each style reads earlier drawing, including the output of earlier backdrop styles.
      // Limit secondary expansion to the part of the style visible to the later dependency.
      if (!requiresBackground) {
        captureBounds.outset(it->outset, it->outset);
      } else {
        auto overlap = captureBounds;
        overlap.left = std::max(overlap.left, static_cast<double>(bounds.left));
        overlap.top = std::max(overlap.top, static_cast<double>(bounds.top));
        overlap.right = std::min(overlap.right, static_cast<double>(bounds.right));
        overlap.bottom = std::min(overlap.bottom, static_cast<double>(bounds.bottom));
        overlap.outset(it->outset, it->outset);
        captureBounds.left = std::min(captureBounds.left, overlap.left);
        captureBounds.top = std::min(captureBounds.top, overlap.top);
        captureBounds.right = std::max(captureBounds.right, overlap.right);
        captureBounds.bottom = std::max(captureBounds.bottom, overlap.bottom);
      }
      requiresBackground = true;
    }
  }
  captureBounds.roundOut();
  if (!IsSafePixelRect(captureBounds)) {
    if (control != nullptr) {
      control->fail(DrawFailureCode::UnsupportedContent, DrawStage::Planning, request.target, {},
                    UnsupportedCoordinates);
    }
    result.code = RegionPlanCode::Failed;
    return result;
  }
  auto captureRect = ToPixelRect(captureBounds);
  auto deviceLimit = limits.gpuLimits.maxTextureDimension2D;
  bool renderFits = FitsLimit(renderRect, surfaceLimit.width, surfaceLimit.height);
  const auto* captureLimitReason =
      requiresBackground ? BackgroundCaptureLimitReason(captureRect, deviceLimit) : nullptr;
  if (!renderFits || captureLimitReason != nullptr) {
    if (core.width() <= 1 && core.height() <= 1) {
      if (control != nullptr) {
        const auto& failedRect = renderFits ? captureRect : renderRect;
        const auto* message = renderFits
                                  ? captureLimitReason
                                  : "a single output pixel exceeds the drawing surface limit";
        control->fail(DrawFailureCode::SurfaceLimit, DrawStage::Planning, request.target,
                      ISize::Make(failedRect.width(), failedRect.height()), message);
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
  const auto* captureLimitReason =
      plan->needsBackground ? BackgroundCaptureLimitReason(plan->captureRect, deviceLimit)
                            : nullptr;
  if (captureLimitReason != nullptr) {
    if (regions.size() <= 1) {
      if (control != nullptr) {
        control->fail(DrawFailureCode::SurfaceLimit, DrawStage::Planning, request.target,
                      ISize::Make(plan->captureRect.width(), plan->captureRect.height()),
                      captureLimitReason);
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
  auto batch = std::unique_ptr<LayerDrawBatch>(new LayerDrawBatch());
  batch->impl->regions = plan.regions;
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
  if (context != nullptr && context->gpu() != nullptr) {
    auto deviceLimit = context->gpu()->limits()->maxTextureDimension2D;
    if (const auto* reason = BackgroundCaptureLimitReason(captureRect, deviceLimit)) {
      if (control != nullptr) {
        control->fail(DrawFailureCode::SurfaceLimit, DrawStage::Background, request.target,
                      captureSize, reason);
      }
      return nullptr;
    }
  }
  BackgroundRasterGeometry geometry = {};
  geometry.size = captureSize;
  geometry.surfaceScale = ExportBackgroundSurfaceScale;
  geometry.sceneRootToSurface = MakeTileMatrix(rootToOutput, captureRect);

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
  auto cullRect = outputToRoot.mapRect(ToRect(captureRect));
  cullRect.roundOut();
  cullRect.outset(1.0f, 1.0f);
  batch->impl->renderRects = {cullRect};
  auto* captureRoot = sceneRoot ? sceneRoot.get() : request.target;
  BackgroundCapturer::RunForTarget(captureRoot, request.target, request.alpha, request.blendMode,
                                   args, std::move(bgSource), &batch->impl->snapshots,
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
  const auto& region = batch.impl->regions[regionIndex];
  auto* surface = canvas->getSurface();
  const int width = region.renderRect.width();
  const int height = region.renderRect.height();
  const auto bounds = Rect::MakeWH(width, height);
  const bool reusesLargerSurface =
      surface != nullptr && (surface->width() > width || surface->height() > height);
  canvas->resetMatrix();
  if (target.clearBeforeDraw) {
    canvas->clear(request.clearColor);
  }
  if (surface != nullptr) {
    canvas->clipRect(bounds, false);
  }
  canvas->setMatrix(MakeTileMatrix(request.targetLocalToOutput, region.renderRect));

  const DrawViewport viewport = {canvas, bounds};
  DrawArgs args(context);
  args.dstColorSpace = request.renderColorSpace;
  args.drawControl = control;
  args.viewport = reusesLargerSurface ? &viewport : nullptr;
  bool needsRegionBackground = region.dependencies != nullptr;
  if (needsRegionBackground && useSceneBounds) {
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

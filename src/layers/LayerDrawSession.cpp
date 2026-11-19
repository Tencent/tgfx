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
#include "layers/BackgroundHandler.h"
#include "layers/BackgroundSource.h"
#include "layers/CanvasUtils.h"
#include "layers/DrawArgs.h"
#include "layers/LayerStyleSource.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/layerstyles/LayerStyle.h"

namespace tgfx {

namespace {

// Bound the output drawing surfaces even on devices with larger textures.
static constexpr int DefaultExportSurfaceLimit = 2048;

// The capture surface is single-sample RGBA_8888 without mipmaps.
static constexpr int MaxBackgroundCaptureBytes = 32 * 1024 * 1024;
static constexpr int BackgroundCaptureBytesPerPixel = 4;

static bool IsSupportedCoordinate(double coordinate) {
  return std::abs(coordinate) <= MaxLayerExportCoordinate;
}

static bool HasSupportedTranslation(const Matrix& matrix) {
  return IsSupportedCoordinate(matrix.getTranslateX()) &&
         IsSupportedCoordinate(matrix.getTranslateY());
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

static bool FitsBackgroundCapture(const PixelRect& rect, int deviceLimit) {
  return FitsLimit(rect, deviceLimit, deviceLimit) &&
         static_cast<int64_t>(rect.width()) * rect.height() <=
             MaxBackgroundCaptureBytes / BackgroundCaptureBytesPerPixel;
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

bool LayerDrawSession::IsSupported2DScene(const Layer* layer,
                                          std::vector<const Layer*>& activeMasks) {
  if (layer->canPreserve3D() || !layer->bitFields.matrix3DIsAffine ||
      !layer->matrix3D().asMatrix().isFinite()) {
    return false;
  }
  for (const auto& child : layer->children()) {
    if (!child->visible() || child->alpha() <= 0 || child->maskOwner != nullptr) {
      continue;
    }
    if (!IsSupported2DScene(child.get(), activeMasks)) {
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
  const bool supported = IsSupported2DScene(mask, activeMasks);
  activeMasks.pop_back();
  return supported;
}

void LayerDrawSession::CollectLayerEffects(Layer* layer, const Matrix& localToOutput,
                                           float filterOutset, LayerStylePosition position,
                                           std::vector<BackgroundEffect>* effects) {
  if (layer->layerStyles().empty()) {
    return;
  }
  auto localBounds = layer->getBounds();
  if (localBounds.isEmpty()) {
    return;
  }
  auto bounds = localToOutput.mapRect(localBounds);
  bounds.outset(filterOutset, filterOutset);
  for (const auto& style : layer->layerStyles()) {
    if (style->position() != position || !NeedsBackgroundSource(style->extraSourceType())) {
      continue;
    }
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

std::unique_ptr<LayerDrawSession> LayerDrawSession::Prepare(const LayerDrawRequest& request,
                                                            DrawControl& control) {
  const auto& outputMatrix = request.targetLocalToOutput;
  if (request.target == nullptr || request.outputSize.isEmpty() || !std::isfinite(request.alpha) ||
      request.alpha < 0.0f || request.alpha > 1.0f || !outputMatrix.isFinite() ||
      !outputMatrix.invertible()) {
    control.fail(LayerImageErrorCode::InvalidInput);
    return nullptr;
  }
  if (outputMatrix.hasPerspective() || request.outputSize.width > MaxLayerExportCoordinate ||
      request.outputSize.height > MaxLayerExportCoordinate ||
      !HasSupportedTranslation(outputMatrix)) {
    control.fail(LayerImageErrorCode::UnsupportedContent);
    return nullptr;
  }
  auto* sceneRoot = request.target->root();
  auto* captureRoot = sceneRoot != nullptr ? sceneRoot : request.target;
  // The DisplayList root has no parent and its own visibility attributes do not affect drawing.
  for (auto* layer = request.target; layer != sceneRoot; layer = layer->parent()) {
    if (!layer->visible() || layer->alpha() <= 0.0f || layer->maskOwner != nullptr) {
      control.fail(LayerImageErrorCode::UnsupportedContent);
      return nullptr;
    }
    if (layer == captureRoot) {
      break;
    }
  }
  std::vector<const Layer*> activeMasks;
  if (!IsSupported2DScene(captureRoot, activeMasks)) {
    control.fail(LayerImageErrorCode::UnsupportedContent);
    return nullptr;
  }

  std::unique_ptr<LayerDrawSession> session(new LayerDrawSession());
  session->request = request;
  session->outputPixelBounds = {0, 0, request.outputSize.width, request.outputSize.height};
  if (sceneRoot != nullptr && request.target != sceneRoot) {
    for (auto* ancestor = request.target->parent(); ancestor != sceneRoot;
         ancestor = ancestor->parent()) {
      if (ancestor->_scrollRect != nullptr || ancestor->hasValidMask()) {
        session->clippedAncestors.push_back(ancestor);
      }
    }
  }
  const bool captureInScene = sceneRoot != nullptr && request.target->_scrollRect == nullptr;
  session->useSceneBounds = captureInScene && session->clippedAncestors.empty();

  if (sceneRoot != nullptr) {
    session->sceneRoot = sceneRoot->shared_from_this();
    sceneRoot->updateRenderBounds();
  }
  if (request.alpha > 0.0f) {
    CollectSubtreeEffects(request.target, outputMatrix, 0.0f, &session->targetEffects);
  }
  if (session->targetEffects.empty() && session->clippedAncestors.empty()) {
    return session;
  }

  auto targetToRoot = request.target->getRelativeMatrix3D(captureRoot).asMatrix();
  Matrix rootToTarget = Matrix::I();
  if (!targetToRoot.invert(&rootToTarget) || !rootToTarget.isFinite()) {
    control.fail(LayerImageErrorCode::InvalidInput);
    return nullptr;
  }
  if (session->targetEffects.empty()) {
    return session;
  }
  for (auto* ancestor : session->clippedAncestors) {
    if (ancestor->_scrollRect == nullptr) {
      continue;
    }
    auto ancestorToOutput = outputMatrix;
    ancestorToOutput.preConcat(ancestor->getRelativeMatrix(request.target));
    auto bounds = ancestorToOutput.mapRect(*ancestor->_scrollRect);
    bounds.roundOut();
    if (!session->ancestorScrollBounds.has_value()) {
      session->ancestorScrollBounds = bounds;
    } else if (!session->ancestorScrollBounds->intersect(bounds)) {
      session->ancestorScrollBounds->setEmpty();
    }
  }
  auto rootToOutput = outputMatrix;
  rootToOutput.preConcat(rootToTarget);
  if (!rootToOutput.isFinite() || rootToOutput.hasPerspective() ||
      !HasSupportedTranslation(rootToOutput) || !rootToOutput.invert(&session->outputToRoot)) {
    control.fail(LayerImageErrorCode::UnsupportedContent);
    return nullptr;
  }
  session->rootToOutput = rootToOutput;
  auto outset =
      captureInScene ? AncestorFilterOutset(request.target, captureRoot, rootToOutput) : 0.0f;
  for (auto& effect : session->targetEffects) {
    effect.outputBounds.outset(outset, outset);
    effect.outset += outset;
  }
  session->collectPrecedingEffects();
  return session;
}

RegionPlanResult LayerDrawSession::planRegion(const PixelRect& core, const LayerDrawLimits& limits,
                                              DrawControl& control) const {
  RegionPlanResult result = {};
  if (control.cancelled()) {
    result.code = RegionPlanCode::Cancelled;
    return result;
  }
  if (core.isEmpty() || core.left < outputPixelBounds.left || core.top < outputPixelBounds.top ||
      core.right > outputPixelBounds.right || core.bottom > outputPixelBounds.bottom) {
    control.fail(LayerImageErrorCode::InvalidInput);
    result.code = RegionPlanCode::Failed;
    return result;
  }
  auto surfaceLimit = ResolveSurfaceLimit(limits);
  // Keep one pixel for antialiasing and sampling at the delivery edge. Background styles sample
  // their input from snapshots of a separately sized capture surface.
  PixelRect renderRect = {core.left - 1, core.top - 1, core.right + 1, core.bottom + 1};
  auto captureBounds = ToPreciseRect(renderRect);
  const bool mayDraw = !ancestorScrollBounds.has_value() ||
                       Rect::Intersects(ToRect(renderRect), *ancestorScrollBounds);
  bool requiresBackground = false;
  for (const auto* effects : {&targetEffects, &precedingEffects}) {
    if (!mayDraw || (effects == &precedingEffects && !requiresBackground)) {
      break;
    }
    for (auto it = effects->rbegin(); it != effects->rend(); ++it) {
      const auto& bounds = it->outputBounds;
      if (!requiresBackground && ancestorScrollBounds.has_value() &&
          !Rect::Intersects(bounds, *ancestorScrollBounds)) {
        continue;
      }
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
  if (requiresBackground) {
    captureBounds.roundOut();
    if (!IsSafePixelRect(captureBounds)) {
      control.fail(LayerImageErrorCode::UnsupportedContent);
      return result;
    }
    result.region.captureRect = ToPixelRect(captureBounds);
  }
  auto deviceLimit = limits.gpuLimits.maxTextureDimension2D;
  if (!FitsLimit(renderRect, surfaceLimit.width, surfaceLimit.height) ||
      (result.region.captureRect &&
       !FitsBackgroundCapture(*result.region.captureRect, deviceLimit))) {
    if (core.width() <= 1 && core.height() <= 1) {
      control.fail(LayerImageErrorCode::DeviceLimitExceeded);
      return result;
    }
    result.code = RegionPlanCode::RequiresSubdivision;
    return result;
  }
  result.region.coreRect = core;
  result.region.renderRect = renderRect;
  result.code = RegionPlanCode::Ready;
  return result;
}

BatchPlanResult LayerDrawSession::planBatch(const std::vector<LayerRegionPlan>& regions,
                                            const LayerDrawLimits& limits,
                                            DrawControl& control) const {
  BatchPlanResult result = {};
  if (control.cancelled()) {
    result.code = BatchPlanCode::Cancelled;
    return result;
  }
  for (const auto& region : regions) {
    if (!region.captureRect) {
      continue;
    }
    if (!result.captureRect) {
      result.captureRect = region.captureRect;
      continue;
    }
    auto& combined = *result.captureRect;
    const auto& capture = *region.captureRect;
    combined.left = std::min(combined.left, capture.left);
    combined.top = std::min(combined.top, capture.top);
    combined.right = std::max(combined.right, capture.right);
    combined.bottom = std::max(combined.bottom, capture.bottom);
  }
  if (result.captureRect &&
      !FitsBackgroundCapture(*result.captureRect, limits.gpuLimits.maxTextureDimension2D)) {
    if (regions.size() <= 1) {
      control.fail(LayerImageErrorCode::DeviceLimitExceeded);
    } else {
      result.code = BatchPlanCode::RequiresSplit;
    }
    return result;
  }
  result.code = BatchPlanCode::Ready;
  return result;
}

std::optional<LayerDrawBatch> LayerDrawSession::prepareBatch(
    Context* context, const std::optional<PixelRect>& captureRect, DrawControl& control) const {
  LayerDrawBatch batch;
  if (!captureRect) {
    return batch;
  }
  // Keep style sources lazy so export never rasterizes a texture the size of the entire layer.
  batch.snapshots.rasterizeStyleSources = false;
  auto captureSize = ISize::Make(captureRect->width(), captureRect->height());
  auto bgSource = BackgroundSource::MakeWithGeometry(
      context, captureSize, MakeTileMatrix(rootToOutput, *captureRect), request.renderColorSpace);
  if (bgSource == nullptr) {
    control.fail(LayerImageErrorCode::AllocationFailed);
    return std::nullopt;
  }
  if (request.clearColor != Color::Transparent()) {
    bgSource->getCanvas()->drawColor(request.clearColor, BlendMode::SrcOver);
  }
  DrawArgs args(context);
  args.dstColorSpace = request.renderColorSpace;
  args.drawControl = &control;
  // Capture and consume use the same cull rect when scene bounds apply.
  auto cullRect = outputToRoot.mapRect(ToRect(*captureRect));
  cullRect.roundOut();
  cullRect.outset(1.0f, 1.0f);
  batch.renderRects.push_back(cullRect);
  auto* captureRoot = sceneRoot ? sceneRoot.get() : request.target;
  BackgroundCapturer::RunForTarget(captureRoot, request.target, request.alpha, request.blendMode,
                                   args, std::move(bgSource), &batch.snapshots, batch.renderRects,
                                   clippedAncestors);
  if (control.failed()) {
    return std::nullopt;
  }
  return batch;
}

bool LayerDrawSession::drawClippedTarget(const DrawArgs& args, Canvas* canvas,
                                         const Rect& bounds) const {
  auto saveCount = canvas->getSaveCount();
  AutoCanvasRestore restore(canvas);
  std::shared_ptr<Shader> coverage;
  if (!LayerClip::ApplyAncestorClips(clippedAncestors, request.target, args, canvas,
                                     &coverage)) {
    return true;
  }
  request.target->drawLayer(args, canvas, request.alpha, request.blendMode);
  if (coverage == nullptr) {
    return true;
  }
  if (args.drawControl->failed()) {
    return false;
  }
  auto result = canvas->getSurface()->makeImageSnapshot();
  if (result == nullptr) {
    return false;
  }

  // Reapply the clear color before mixing in the already-blended tile. The masks affect the
  // complete group result, including when the target uses a blend mode such as Clear or Src.
  canvas->restoreToCount(saveCount);
  canvas->resetMatrix();
  canvas->clear(request.clearColor);
  Paint paint;
  paint.setAntiAlias(false);
  paint.setBlendMode(BlendMode::Src);
  paint.setMaskFilter(MaskFilter::MakeShader(std::move(coverage)));
  const SamplingOptions sampling{FilterMode::Nearest, MipmapMode::None};
  canvas->drawImageRect(std::move(result), bounds, bounds, sampling, &paint);
  return true;
}

DrawOutcome LayerDrawSession::drawRegion(const LayerRegionPlan& region, LayerDrawBatch& batch,
                                         Canvas* canvas, DrawControl& control) const {
  if (control.cancelled()) {
    return DrawOutcome::Cancelled;
  }
  AutoCanvasRestore restore(canvas);
  canvas->resetMatrix();
  canvas->clear(request.clearColor);
  const auto bounds = Rect::MakeWH(region.renderRect.width(), region.renderRect.height());
  canvas->clipRect(bounds, false);
  canvas->setMatrix(MakeTileMatrix(request.targetLocalToOutput, region.renderRect));

  DrawArgs args(canvas->getSurface()->getContext());
  args.dstColorSpace = request.renderColorSpace;
  args.drawControl = &control;
  const bool needsBackground = region.captureRect.has_value();
  if (needsBackground && useSceneBounds) {
    args.renderRects = &batch.renderRects;
  }
  BackgroundConsumer consumer(&batch.snapshots, false);
  args.backgroundHandler =
      needsBackground ? static_cast<BackgroundHandler*>(&consumer) : BackgroundHandler::NoOp();
  if (request.alpha > 0.0f) {
    if (clippedAncestors.empty()) {
      request.target->drawLayer(args, canvas, request.alpha, request.blendMode);
    } else if (!drawClippedTarget(args, canvas, bounds)) {
      control.fail(LayerImageErrorCode::DrawFailed);
    }
  }
  if (control.failed()) {
    return DrawOutcome::Failed;
  }
  return control.cancelled() ? DrawOutcome::Cancelled : DrawOutcome::Success;
}

}  // namespace tgfx

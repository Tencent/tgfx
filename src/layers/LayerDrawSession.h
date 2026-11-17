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

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Size.h"
#include "tgfx/gpu/GPULimits.h"

namespace tgfx {
class Canvas;
class Context;
class Layer;

/**
 * Integer rectangle in a final pixel grid. It is used for tiled output planning so that the float
 * Rect used by layout code never mixes with the integer pixel partition of the delivered image.
 */
struct PixelRect {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;

  int width() const {
    return right - left;
  }

  int height() const {
    return bottom - top;
  }

  bool isEmpty() const {
    return right <= left || bottom <= top;
  }
};

/**
 * Selects how background-sourced effects derive their sampling geometry.
 */
enum class LayerSamplingMode {
  // The default strategy of the regular Layer::draw() path.
  CanvasDefault,
  // One fixed sampling rule shared by the whole export, aligned to the global output grid.
  ExportFixed
};

/**
 * Everything a shared drawing session needs to know about one target and its scene.
 */
struct LayerDrawRequest {
  Layer* target = nullptr;
  Layer* captureRoot = nullptr;  // Its lifetime is guaranteed by the caller.

  // GPU context used for capture and effect surfaces. It may be null in CanvasDefault mode when
  // the target is drawn on a picture-only canvas; ExportFixed requires a usable context.
  Context* context = nullptr;

  Matrix targetLocalToOutput = Matrix::I();
  Rect logicalOutputBounds = {};
  std::shared_ptr<ColorSpace> renderColorSpace = ColorSpace::SRGB();
  Color clearColor = Color::Transparent();
  float alpha = 1.0f;
  BlendMode blendMode = BlendMode::SrcOver;
  LayerSamplingMode samplingMode = LayerSamplingMode::CanvasDefault;

  // CanvasDefault only: the canvas state the regular draw path has to preserve. outputOrigin of a
  // draw target is ignored in this mode and the existing canvas matrix, clip and content stay
  // untouched.
  bool hasCanvasState = false;
  Matrix canvasMatrix = Matrix::I();
  Rect canvasDrawBounds = {};      // Local-space bounds clipped by the canvas clip.
  bool canvasRooted = false;       // Whether the target belongs to a root layer.
  bool canvasCacheEnabled = true;  // Whether the draw may use the GPU context for effects.
  std::shared_ptr<ColorSpace> canvasColorSpace = ColorSpace::SRGB();
};

/**
 * Diagnostic code of a shared drawing failure.
 */
enum class DrawFailureCode {
  None,
  InvalidRequest,
  UnsupportedContent,
  SurfaceLimit,
  MissingDependency,
  AllocationFailure,
  RenderingFailure
};

/**
 * Phase in which a shared drawing failure was observed.
 */
enum class DrawStage { Validation, Planning, Background, Rendering };

/**
 * First failure of one drawing session. It is only filled by strict callers; the regular draw path
 * keeps its existing silent handling.
 */
struct DrawFailure {
  DrawFailureCode code = DrawFailureCode::None;
  DrawStage stage = DrawStage::Validation;
  Layer* layer = nullptr;
  ISize surfaceSize = {};
  std::string message = {};
};

/**
 * Carries cancellation and failure state through one shared drawing pass. A non-null control means
 * the caller wants observable failures; a null control keeps the regular draw behavior.
 */
struct DrawControl {
  const std::atomic<bool>* cancelRequested = nullptr;
  DrawFailure firstFailure = {};

  bool cancelled() const {
    return cancelRequested != nullptr && cancelRequested->load();
  }

  bool failed() const {
    return firstFailure.code != DrawFailureCode::None;
  }

  void fail(DrawFailure failure);

  void fail(DrawFailureCode code, DrawStage stage, Layer* layer = nullptr, ISize surfaceSize = {},
            std::string message = {});
};

/**
 * Result of drawing one planned region.
 */
enum class DrawOutcome { Success, Cancelled, Failed };

/**
 * Device limits and the optional limit for the surface that receives one output region. The
 * region surface limit includes the margin an effect needs, so it is not a core region size.
 */
struct LayerDrawLimits {
  GPULimits gpuLimits = {};
  ISize outputSurfaceLimit = {};
};

/**
 * CPU description of the background input one output region needs. captureRect is expressed in
 * final output pixels and may extend outside the output bounds.
 */
class RegionDependencyPlan {
 public:
  bool needsBackground = false;
  PixelRect captureRect = {};
};

/**
 * One output region: the pixels to deliver, the surface window that produces them and the
 * background inputs they depend on.
 */
struct LayerRegionPlan {
  PixelRect coreRect = {};
  PixelRect renderRect = {};
  std::shared_ptr<const RegionDependencyPlan> dependencies = nullptr;
};

/**
 * Planning outcome for one region.
 */
enum class RegionPlanCode { Ready, RequiresSubdivision, Cancelled, Failed };

struct RegionPlanResult {
  RegionPlanCode code = RegionPlanCode::Failed;
  LayerRegionPlan region = {};
};

/**
 * CPU description of one batch of regions sharing a background capture.
 */
class LayerDrawBatchPlan {
 public:
  std::vector<LayerRegionPlan> regions = {};
  PixelRect captureRect = {};
  bool needsBackground = false;
};

/**
 * Planning outcome for one batch.
 */
enum class BatchPlanCode { Ready, RequiresSplit, Cancelled, Failed };

struct BatchPlanResult {
  BatchPlanCode code = BatchPlanCode::Failed;
  std::shared_ptr<const LayerDrawBatchPlan> plan = nullptr;
};

/**
 * Destination of one region draw. outputOrigin is the region's top-left corner in final output
 * pixels; clearBeforeDraw is true for a freshly allocated export surface.
 */
struct LayerDrawTarget {
  Canvas* canvas = nullptr;
  Point outputOrigin = Point::Zero();
  bool clearBeforeDraw = false;
};

/**
 * GPU state that lives inside one valid context lock for a batch of regions.
 */
class LayerDrawBatch {
 public:
  ~LayerDrawBatch();
  LayerDrawBatch(const LayerDrawBatch&) = delete;
  LayerDrawBatch& operator=(const LayerDrawBatch&) = delete;

  size_t regionCount() const;

 private:
  LayerDrawBatch();
  class Impl;
  std::unique_ptr<Impl> impl;

  friend class LayerDrawSession;
};

/**
 * Shared drawing session used by both the regular Layer::draw() path and the tiled export reader.
 * It fixes the request, the root mapping and the sampling rules once, plans output regions and
 * batches, prepares per-batch background snapshots, and replays the existing Layer drawing.
 * Methods other than requestCancel() must run on the thread that owns the scene.
 */
class LayerDrawSession {
 public:
  // Validates the request and fixes the root mapping and sampling rules. For CanvasDefault the
  // context may be null; ExportFixed requires a usable GPU context.
  static std::unique_ptr<LayerDrawSession> Prepare(Context* context,
                                                   const LayerDrawRequest& request,
                                                   DrawControl* control);

  // Derives the surface window and background dependency of one core region. It only describes
  // CPU geometry and never allocates GPU pixels.
  RegionPlanResult planRegion(const PixelRect& core, const LayerDrawLimits& limits,
                              DrawControl* control) const;

  // Checks whether the given regions can share one background capture under the fixed sampling
  // rules. It never allocates GPU pixels.
  BatchPlanResult planBatch(const std::vector<LayerRegionPlan>& regions,
                            const LayerDrawLimits& limits, DrawControl* control) const;

  // Builds the background snapshots and style inputs a batch needs. The returned batch must not
  // outlive the current context lock.
  std::unique_ptr<LayerDrawBatch> prepareBatch(Context* context, const LayerDrawBatchPlan& plan,
                                               DrawControl* control);

  // Draws one region of the batch on the given target, applying the fixed output matrix, the
  // target window and the scene clear color.
  DrawOutcome drawRegion(Context* context, LayerDrawBatch& batch, size_t regionIndex,
                         const LayerDrawTarget& target, DrawControl* control);

  // Resolves the effective surface limit from the device limit and the optional output limit.
  static ISize ResolveSurfaceLimit(const LayerDrawLimits& limits);

 private:
  LayerDrawSession() = default;

  static bool IsSupported2DSubtree(Layer* layer);
  static bool IsSupportedAncestorChain(Layer* target, Layer* captureRoot);
  static void ScanBackgroundOutset(Layer* layer, const Matrix& localToRoot, float* maxOutset,
                                   bool* needsBackground);

  RegionPlanResult planExportRegion(const PixelRect& core, const LayerDrawLimits& limits,
                                    DrawControl* control) const;
  RegionPlanResult planCanvasRegion(DrawControl* control) const;
  std::unique_ptr<LayerDrawBatch> prepareExportBatch(Context* context,
                                                     const LayerDrawBatchPlan& plan,
                                                     DrawControl* control);
  std::unique_ptr<LayerDrawBatch> prepareCanvasBatch(Context* context, DrawControl* control);
  DrawOutcome drawExportRegion(Context* context, LayerDrawBatch& batch, size_t regionIndex,
                               const LayerDrawTarget& target, DrawControl* control);
  DrawOutcome drawCanvasRegion(LayerDrawBatch& batch, const LayerDrawTarget& target,
                               DrawControl* control);

  LayerDrawRequest request = {};
  Matrix rootToOutput = Matrix::I();  // captureRoot-local to final output pixels.
  Matrix outputToRoot = Matrix::I();
  Matrix outputToGlobal = Matrix::I();  // output pixels to the renderBounds space.
  Matrix globalToOutput = Matrix::I();
  PixelRect outputPixelBounds = {};
  bool needsBackground = false;
  float backgroundOutset = 0.0f;  // In captureRoot-local units.

  // CanvasDefault state captured from the original canvas.
  bool canvasCanInvert = true;
  Rect canvasRenderRect = {};  // In captureRoot-local space.
  std::vector<Rect> canvasRenderRects = {};
  Rect canvasBackgroundDrawRect = {};  // In device pixels.
  Matrix canvasBackgroundMatrix = Matrix::I();
  bool canvasFullLayerBackground = false;
  Rect canvasCaptureCullRect = {};  // In captureRoot-local space.
};

}  // namespace tgfx

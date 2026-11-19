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

#include <memory>
#include <optional>
#include <vector>
#include "layers/BackgroundSnapshotMap.h"
#include "layers/DrawControl.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Size.h"
#include "tgfx/gpu/GPULimits.h"
#include "tgfx/layers/LayerImageReader.h"

namespace tgfx {
class Canvas;
class Context;
class DrawArgs;
class Layer;
enum class LayerStylePosition;

// The tiled export uses float geometry, with subpixel precision below this magnitude.
inline constexpr int MaxLayerExportCoordinate = 1 << 23;

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
 * The target and output parameters for a tiled export.
 */
struct LayerDrawRequest {
  Layer* target = nullptr;  // Its lifetime is guaranteed by the caller.

  Matrix targetLocalToOutput = Matrix::I();
  // Keep the final output size integral to preserve exact pixel bounds for large images.
  ISize outputSize = {};
  std::shared_ptr<ColorSpace> renderColorSpace = ColorSpace::SRGB();
  Color clearColor = Color::Transparent();
  float alpha = 1.0f;
  BlendMode blendMode = BlendMode::SrcOver;
};

/**
 * Result of drawing one planned region.
 */
enum class DrawOutcome { Success, Cancelled, Failed };

/**
 * Device limits and the optional size limit for the output surface, including its one-pixel
 * sampling margin. Background captures have their own byte budget and use the device texture
 * limit.
 */
struct LayerDrawLimits {
  GPULimits gpuLimits = {};
  ISize outputSurfaceLimit = {};
};

struct LayerRegionPlan {
  PixelRect coreRect = {};
  PixelRect renderRect = {};
  // Background sampling bounds in output pixels. They may extend outside the image.
  std::optional<PixelRect> captureRect;
};

/**
 * Planning outcome for one region.
 */
enum class RegionPlanCode { Ready, RequiresSubdivision, Cancelled, Failed };

struct RegionPlanResult {
  RegionPlanCode code = RegionPlanCode::Failed;
  LayerRegionPlan region = {};
};

enum class BatchPlanCode { Ready, RequiresSplit, Cancelled, Failed };

struct BatchPlanResult {
  BatchPlanCode code = BatchPlanCode::Failed;
  std::optional<PixelRect> captureRect;
};

struct LayerDrawBatch {
  BackgroundSnapshotMap snapshots;
  std::vector<Rect> renderRects;
};

/**
 * Drawing session for the tiled export reader.
 * It fixes the request, the root mapping and the sampling rules once, plans output regions and
 * batches, prepares per-batch background snapshots, and replays the existing Layer drawing.
 * Session methods must run on the thread that owns the scene.
 */
class LayerDrawSession {
 public:
  // Validates the request and the scene it draws from, then records the ancestor scroll rects and
  // masks that clip the target and the background-sourced effects its regions may depend on. On
  // failure it reports the error through control and returns nullptr.
  static std::unique_ptr<LayerDrawSession> Prepare(const LayerDrawRequest& request,
                                                   DrawControl& control);

  // Derives the surface window and background dependency of one core region. It only describes
  // CPU geometry and never allocates GPU pixels.
  RegionPlanResult planRegion(const PixelRect& core, const LayerDrawLimits& limits,
                              DrawControl& control) const;

  // Checks whether the given regions can share one background capture under the fixed sampling
  // rules. It never allocates GPU pixels.
  BatchPlanResult planBatch(const std::vector<LayerRegionPlan>& regions,
                            const LayerDrawLimits& limits, DrawControl& control) const;

  // Builds the background snapshots and style inputs a batch needs. The returned batch must not
  // outlive the current context lock.
  std::optional<LayerDrawBatch> prepareBatch(Context* context,
                                             const std::optional<PixelRect>& captureRect,
                                             DrawControl& control) const;

  // Draws one region of the batch on the given target, applying the fixed output matrix, the
  // target window and the scene clear color.
  DrawOutcome drawRegion(const LayerRegionPlan& region, LayerDrawBatch& batch, Canvas* canvas,
                         DrawControl& control) const;

  // Resolves the effective surface limit from the device limit and the optional output limit.
  static ISize ResolveSurfaceLimit(const LayerDrawLimits& limits);

 private:
  LayerDrawSession() = default;

  static float FilterOutset(Layer* layer, float scale);
  static float AncestorFilterOutset(Layer* target, Layer* captureRoot, const Matrix& rootToOutput);
  static bool IsSupported2DScene(const Layer* layer, std::vector<const Layer*>& activeMasks);
  bool drawClippedTarget(const DrawArgs& args, Canvas* canvas, const Rect& bounds) const;
  struct BackgroundEffect {
    Rect outputBounds = {};
    float outset = 0;
  };

  static void CollectLayerEffects(Layer* layer, const Matrix& localToOutput, float filterOutset,
                                  LayerStylePosition position,
                                  std::vector<BackgroundEffect>* effects);
  static void CollectSubtreeEffects(Layer* layer, const Matrix& localToOutput, float ancestorOutset,
                                    std::vector<BackgroundEffect>* effects);
  void collectPrecedingEffects();

  LayerDrawRequest request = {};
  std::shared_ptr<Layer> sceneRoot = nullptr;
  Matrix rootToOutput = Matrix::I();  // captureRoot-local to final output pixels.
  Matrix outputToRoot = Matrix::I();  // output pixels to the capture-root coordinate space.
  PixelRect outputPixelBounds = {};
  std::optional<Rect> ancestorScrollBounds = std::nullopt;
  bool useSceneBounds = true;
  std::vector<Layer*> clippedAncestors = {};
  std::vector<BackgroundEffect> targetEffects = {};
  std::vector<BackgroundEffect> precedingEffects = {};
};

}  // namespace tgfx

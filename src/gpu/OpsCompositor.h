/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "core/MCState.h"
#include "gpu/ops/RRectDrawOp.h"
#include "gpu/ops/RectDrawOp.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Fill.h"
#include "tgfx/core/Shape.h"

namespace tgfx {
enum class PendingOpType {
  Unknown,
  Image,
  Rect,
  RRect,
  Shape,
  Atlas,
};

/**
 * OpsCompositor is a helper class for composing a series of draw operations into a single render
 * task.
 */
class OpsCompositor {
 public:
  /**
   * Creates an OpsCompositor with the given render target proxy, render flags and render queue.
   */
  OpsCompositor(std::shared_ptr<RenderTargetProxy> proxy, uint32_t renderFlags,
                std::optional<Color> clearColor = std::nullopt,
                std::shared_ptr<ColorSpace> colorSpace = nullptr);

  /**
   * Fills the given image with the given sampling options, state and fill.
   */
  void fillImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                 const MCState& state, const Fill& fill);
  /**
   * Fills the given rect with the image, using the given source rect, destination rect, sampling
   * options, state and fill.
   */
  void fillImageRect(std::shared_ptr<Image> image, const Rect& srcRect, const Rect& dstRect,
                     const SamplingOptions& sampling, const MCState& state, const Fill& fill,
                     SrcRectConstraint constraint);

  /**
   * Fills the given rect with the given state, fill and optional stroke.
   */
  void fillRect(const Rect& rect, const MCState& state, const Fill& fill, const Stroke* stroke);

  /**
   * Draw the given rrect with the given state, fill and optional stroke.
   */
  void drawRRect(const RRect& rRect, const MCState& state, const Fill& fill, const Stroke* stroke);

  /**
   * Fills the given shape with the given state and fill.
   */
  void drawShape(std::shared_ptr<Shape> shape, const MCState& state, const Fill& fill);

  /**
   * Fills the given rect with the given atlas textureProxy, sampling options, state and fill.
   */
  void fillTextAtlas(std::shared_ptr<TextureProxy> textureProxy, const Rect& rect,
                     const SamplingOptions& sampling, const MCState& state, const Fill& fill);

  /**
   * Discard all pending operations.
   */
  void discardAll();

  /**
   * Close the compositor and submit the composed render task to the render queue. After closing,
   * the compositor is no longer valid.
   */
  void makeClosed();

  /**
   * Returns true if the compositor is closed.
   */
  bool isClosed() const {
    return renderTarget == nullptr;
  }

 private:
  Context* context = nullptr;
  std::list<std::shared_ptr<OpsCompositor>>::iterator cachedPosition;
  std::shared_ptr<RenderTargetProxy> renderTarget = nullptr;
  uint32_t renderFlags = 0;
  UniqueKey clipKey = {};
  std::shared_ptr<TextureProxy> clipTexture = nullptr;
  bool hasRectToRectDraw = false;
  PendingOpType pendingType = PendingOpType::Unknown;
  Path pendingClip = {};
  Fill pendingFill = {};
  std::shared_ptr<Image> pendingImage = nullptr;
  SrcRectConstraint pendingConstraint = SrcRectConstraint::Fast;
  SamplingOptions pendingSampling = {};
  std::shared_ptr<TextureProxy> pendingAtlasTexture = nullptr;
  std::vector<PlacementPtr<RectRecord>> pendingRects = {};
  std::vector<PlacementPtr<Rect>> pendingUVRects = {};
  std::vector<PlacementPtr<RRectRecord>> pendingRRects = {};
  std::vector<PlacementPtr<Stroke>> pendingStrokes = {};
  std::optional<Color> clearColor = std::nullopt;
  std::vector<PlacementPtr<DrawOp>> drawOps = {};
  std::shared_ptr<ColorSpace> dstColorSpace = nullptr;

  static bool CompareFill(const Fill& a, const Fill& b);

  BlockAllocator* drawingAllocator() const {
    return context->drawingAllocator();
  }

  ProxyProvider* proxyProvider() const {
    return context->proxyProvider();
  }

  bool drawAsClear(const Rect& rect, const MCState& state, const Fill& fill);
  bool canAppend(PendingOpType type, const Path& clip, const Fill& fill) const;
  void flushPendingOps(PendingOpType currentType = PendingOpType::Unknown, Path currentClip = {},
                       Fill currentFill = {});
  void resetPendingOps(PendingOpType currentType = PendingOpType::Unknown, Path currentClip = {},
                       Fill currentFill = {});
  AAType getAAType(const Fill& fill) const;
  std::pair<bool, bool> needComputeBounds(const Fill& fill, bool hasCoverage,
                                          bool hasImageFill = false);
  Rect getClipBounds(const Path& clip);
  std::shared_ptr<TextureProxy> getClipTexture(const Path& clip, AAType aaType);
  std::pair<std::optional<Rect>, bool> getClipRect(const Path& clip);
  std::pair<PlacementPtr<FragmentProcessor>, bool> getClipMaskFP(const Path& clip, AAType aaType,
                                                                 Rect* scissorRect);
  DstTextureInfo makeDstTextureInfo(const Rect& deviceBounds, AAType aaType);
  void addDrawOp(PlacementPtr<DrawOp> op, const Path& clip, const Fill& fill,
                 const std::optional<Rect>& localBounds, const std::optional<Rect>& deviceBounds,
                 float drawScale);

  void submitDrawOps();

  friend class DrawingManager;
  friend class PendingOpsAutoReset;
};
}  // namespace tgfx

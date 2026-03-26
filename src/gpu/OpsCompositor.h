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

#include "core/ClipStack.h"
#include "gpu/ops/RRectDrawOp.h"
#include "gpu/ops/RectDrawOp.h"
#include "tgfx/core/Brush.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Mesh.h"
#include "tgfx/core/Shape.h"

namespace tgfx {

/**
 * AppliedClipStatus represents the result of applying a clip to a draw operation.
 */
enum class AppliedClipStatus {
  // The draw is completely clipped out, skip drawing.
  ClippedOut,
  // No clipping needed, draw normally.
  Unclipped,
  // Clipping is applied via scissor and/or coverage FP.
  Clipped
};

/**
 * AppliedClip holds the result of processing a ClipStack for a draw operation.
 */
struct AppliedClip {
  AppliedClipStatus status = AppliedClipStatus::Unclipped;
  std::optional<Rect> scissor = std::nullopt;
  // Coverage fragment processor for AA or mask clipping.
  PlacementPtr<FragmentProcessor> coverageFP = nullptr;
};

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
                std::optional<PMColor> clearColor = std::nullopt,
                std::shared_ptr<ColorSpace> colorSpace = nullptr);

  /**
   * Fills the given image with the given sampling options, state and fill.
   */
  void fillImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                 const Matrix& matrix, const ClipStack& clip, const Brush& brush);
  /**
   * Fills the given rect with the image, using the given source rect, destination rect, sampling
   * options, state and fill.
   */
  void fillImageRect(std::shared_ptr<Image> image, const Rect& srcRect, const Rect& dstRect,
                     const SamplingOptions& sampling, const Matrix& matrix, const ClipStack& clip,
                     const Brush& brush, SrcRectConstraint constraint);

  /**
   * Fills the given rect with the given state, fill and optional stroke.
   */
  void fillRect(const Rect& rect, const Matrix& matrix, const ClipStack& clip, const Brush& brush,
                const Stroke* stroke);

  /**
   * Draw the given rrect with the given state, fill and optional stroke.
   */
  void drawRRect(const RRect& rRect, const Matrix& matrix, const ClipStack& clip,
                 const Brush& brush, const Stroke* stroke);

  /**
   * Fills the given shape with the given state and fill.
   */
  void drawShape(std::shared_ptr<Shape> shape, const Matrix& matrix, const ClipStack& clip,
                 const Brush& brush);

  /**
   * Draws the given shape with hairline rendering.
   */
  void drawHairlineShape(std::shared_ptr<Shape> shape, const Matrix& matrix, const ClipStack& clip,
                         const Brush& brush, const Stroke* stroke);

  /**
   * Draws the given mesh with the given state and brush.
   */
  void drawMesh(std::shared_ptr<Mesh> mesh, const Matrix& matrix, const ClipStack& clip,
                const Brush& brush);

  /**
   * Fills the given rect with the given atlas textureProxy, sampling options, state and fill.
   */
  void fillTextAtlas(std::shared_ptr<TextureProxy> textureProxy, const Rect& rect,
                     const SamplingOptions& sampling, const Matrix& matrix, const ClipStack& clip,
                     const Brush& brush);

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
  uint32_t cachedClipID = 0;
  std::shared_ptr<TextureProxy> clipTexture = nullptr;
  bool hasRectToRectDraw = false;
  PendingOpType pendingType = PendingOpType::Unknown;
  ClipStack pendingClip = {};
  Brush pendingBrush = {};
  std::shared_ptr<Image> pendingImage = nullptr;
  SrcRectConstraint pendingConstraint = SrcRectConstraint::Fast;
  SamplingOptions pendingSampling = {};
  std::shared_ptr<TextureProxy> pendingAtlasTexture = nullptr;
  std::vector<PlacementPtr<RectRecord>> pendingRects = {};
  std::vector<PlacementPtr<Rect>> pendingUVRects = {};
  std::vector<PlacementPtr<RRectRecord>> pendingRRects = {};
  std::vector<PlacementPtr<Stroke>> pendingStrokes = {};
  std::shared_ptr<Shape> pendingShape = nullptr;
  Matrix pendingShapeMatrix = {};
  std::vector<Point> pendingShapeOffsets = {};
  std::vector<Color> pendingShapeColors = {};
  std::optional<PMColor> clearColor = std::nullopt;
  std::vector<PlacementPtr<DrawOp>> drawOps = {};
  std::shared_ptr<ColorSpace> dstColorSpace = nullptr;

  static bool CompareBrush(const Brush& a, const Brush& b);

  BlockAllocator* drawingAllocator() const {
    return context->drawingAllocator();
  }

  ProxyProvider* proxyProvider() const {
    return context->proxyProvider();
  }

  bool drawAsClear(const Rect& rect, const Matrix& matrix, const ClipStack& clip,
                   const Brush& brush);
  bool canAppend(PendingOpType type, const ClipStack& clip, const Brush& brush) const;
  void flushPendingOps(PendingOpType currentType = PendingOpType::Unknown,
                       ClipStack currentClip = {}, Brush currentBrush = {});
  void flushPendingShapeOps();
  void resetPendingOps(PendingOpType currentType = PendingOpType::Unknown,
                       ClipStack currentClip = {}, Brush currentBrush = {});
  AAType getAAType(const Brush& brush) const;
  AAType getAAType(bool antiAlias) const;
  std::pair<bool, bool> needComputeBounds(const Brush& brush, bool hasCoverage,
                                          bool hasImageFill = false);
  Rect getClipBounds(const ClipStack& clip) const;
  AppliedClip applyClip(const ClipStack& clipStack);
  PlacementPtr<FragmentProcessor> makeAnalyticFP(const ClipElement& element,
                                                 PlacementPtr<FragmentProcessor> inputFP);
  PlacementPtr<FragmentProcessor> getClipMaskFP(const std::vector<const ClipElement*>& elements,
                                                uint32_t uniqueID, const Rect& clipBound,
                                                PlacementPtr<FragmentProcessor> inputFP);
  std::shared_ptr<TextureProxy> makeClipTexture(const std::vector<const ClipElement*>& elements,
                                                const Rect& bounds) const;
  PlacementPtr<FragmentProcessor> makeMaskFP(std::shared_ptr<TextureProxy> maskTexture,
                                             const Rect& bounds,
                                             PlacementPtr<FragmentProcessor> inputFP) const;
  DstTextureInfo makeDstTextureInfo(const Rect& deviceBounds, AAType aaType);
  void addDrawOp(PlacementPtr<DrawOp> op, const ClipStack& clip, const Brush& brush,
                 const std::optional<Rect>& localBounds, const std::optional<Rect>& deviceBounds,
                 float drawScale);

  void submitDrawOps();

  friend class DrawingManager;
  friend class PendingOpsAutoReset;
};
}  // namespace tgfx

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "tgfx/core/Fill.h"
#include "tgfx/core/Shape.h"

namespace tgfx {
enum class PendingOpType {
  Unknown,
  Image,
  Rect,
  RRect,
  Shape,
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
  OpsCompositor(std::shared_ptr<RenderTargetProxy> proxy, uint32_t renderFlags);

  /**
   * Fills the given rect with the image, sampling options, state and fill.
   */
  void fillImage(std::shared_ptr<Image> image, const Rect& rect, const SamplingOptions& sampling,
                 const MCState& state, const Fill& fill);

  /**
   * Fills the given rect with the given state and fill.
   */
  void fillRect(const Rect& rect, const MCState& state, const Fill& fill);

  /**
   * Fills the given rrect with the given state and fill.
   */
  void fillRRect(const RRect& rRect, const MCState& state, const Fill& fill);

  /**
   * Fills the given shape with the given state and fill.
   */
  void fillShape(std::shared_ptr<Shape> shape, const MCState& state, const Fill& fill);

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
  PendingOpType pendingType = PendingOpType::Unknown;
  Path pendingClip = {};
  Fill pendingFill = {};
  std::shared_ptr<Image> pendingImage = nullptr;
  SamplingOptions pendingSampling = {};
  std::vector<PlacementPtr<RectRecord>> pendingRects = {};
  std::vector<PlacementPtr<RRectRecord>> pendingRRects = {};
  std::vector<PlacementPtr<Op>> ops = {};

  BlockBuffer* drawingBuffer() const {
    return context->drawingBuffer();
  }

  ProxyProvider* proxyProvider() const {
    return context->proxyProvider();
  }

  bool drawAsClear(const Rect& rect, const MCState& state, const Fill& fill);
  bool canAppend(PendingOpType type, const Path& clip, const Fill& fill) const;
  void flushPendingOps(PendingOpType type = PendingOpType::Unknown, Path clip = {}, Fill fill = {});
  AAType getAAType(const Fill& fill) const;
  std::pair<bool, bool> needComputeBounds(const Fill& fill, bool hasImageFill = false);
  Rect getClipBounds(const Path& clip);
  std::shared_ptr<TextureProxy> getClipTexture(const Path& clip, AAType aaType);
  std::pair<std::optional<Rect>, bool> getClipRect(const Path& clip);
  std::pair<PlacementPtr<FragmentProcessor>, bool> getClipMaskFP(const Path& clip, AAType aaType,
                                                                 Rect* scissorRect);
  DstTextureInfo makeDstTextureInfo(const Rect& deviceBounds, AAType aaType);
  void addDrawOp(PlacementPtr<DrawOp> op, const Path& clip, const Fill& fill,
                 const Rect& localBounds, const Rect& deviceBounds, bool needLocalBounds,
                 bool needDeviceBounds);

  friend class DrawingManager;
};
}  // namespace tgfx

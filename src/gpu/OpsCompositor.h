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

#include "core/FillStyle.h"
#include "core/MCState.h"
#include "gpu/ops/RRectDrawOp.h"
#include "gpu/ops/RectDrawOp.h"
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
  OpsCompositor(DrawingManager* drawingManager, std::shared_ptr<RenderTargetProxy> proxy,
                uint32_t renderFlags);

  /**
   * Fill the given rect with the image, sampling options, state and style.
   */
  void fillImage(std::shared_ptr<Image> image, const Rect& rect, const SamplingOptions& sampling,
                 const MCState& state, const FillStyle& style);

  /**
   * Fill the given rect with the given state and style.
   */
  void fillRect(const Rect& rect, const MCState& state, const FillStyle& style);

  /**
   * Fill the given rrect with the given state and style.
   */
  void fillRRect(const RRect& rRect, const MCState& state, const FillStyle& style);

  /**
   * Fill the given shape with the given state and style.
   */
  void fillShape(std::shared_ptr<Shape> shape, const MCState& state, const FillStyle& style);

  /**
   * Close the compositor and submit the composed render task to the render queue. After closing,
   * the compositor is no longer valid.
   */
  void makeClosed();

  /**
   * Returns true if the compositor is closed.
   */
  bool isClosed() const {
    return drawingManager == nullptr;
  }

 private:
  DrawingManager* drawingManager = nullptr;
  std::shared_ptr<RenderTargetProxy> renderTarget = nullptr;
  uint32_t renderFlags = 0;
  std::vector<std::unique_ptr<Op>> ops = {};
  UniqueKey clipKey = {};
  std::shared_ptr<TextureProxy> clipTexture = nullptr;
  PendingOpType pendingType = PendingOpType::Unknown;
  Path pendingClip = {};
  FillStyle pendingStyle = {};
  std::shared_ptr<Image> pendingImage = nullptr;
  SamplingOptions pendingSampling = {};
  std::vector<RectPaint> pendingRects = {};
  std::vector<RRectPaint> pendingRRects = {};

  bool drawAsClear(const Rect& rect, const MCState& state, const FillStyle& style);
  bool canAppend(PendingOpType type, const Path& clip, const FillStyle& style) const;
  void flushPendingOps(PendingOpType type = PendingOpType::Unknown, Path clip = {},
                       FillStyle style = {});
  AAType getAAType(const FillStyle& style) const;
  Rect getClipBounds(const Path& clip);
  std::shared_ptr<TextureProxy> getClipTexture(const Path& clip, AAType aaType);
  std::pair<std::optional<Rect>, bool> getClipRect(const Path& clip);
  std::unique_ptr<FragmentProcessor> getClipMaskFP(const Path& clip, const Rect& deviceBounds,
                                                   AAType aaType, Rect* scissorRect);
  void addDrawOp(std::unique_ptr<DrawOp> op, const Rect& localBounds, const Path& clip,
                 const FillStyle& style);
};
}  // namespace tgfx

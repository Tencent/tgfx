/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include <functional>
#include "core/DrawContext.h"
#include "core/PictureRecords.h"
#include "core/utils/BlockAllocator.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {
class PictureContext : public DrawContext {
 public:
  ~PictureContext() override;

  void clear();

  /**
   * Signals that the caller is done recording and returns a Picture object that captures all the
   * drawing commands made to the context. Returns nullptr if no recording is active or no commands
   * were recorded.
   */
  std::shared_ptr<Picture> finishRecordingAsPicture();

  void drawFill(const Brush& brush) override;

  void drawRect(const Rect& rect, const MCState& state, const Brush& brush,
                const Stroke* stroke) override;

  void drawRRect(const RRect& rRect, const MCState& state, const Brush& brush,
                 const Stroke* stroke) override;

  void drawPath(const Path& path, const MCState& state, const Brush& brush) override;

  void drawShape(std::shared_ptr<Shape> shape, const MCState& state, const Brush& brush,
                 const Stroke* stroke) override;

  void drawMesh(std::shared_ptr<Mesh> mesh, const MCState& state, const Brush& brush) override;

  void drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                 const MCState& state, const Brush& brush) override;

  void drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect, const Rect& dstRect,
                     const SamplingOptions& sampling, const MCState& state, const Brush& brush,
                     SrcRectConstraint constraint) override;

  void drawTextBlob(std::shared_ptr<TextBlob> textBlob, const MCState& state, const Brush& brush,
                    const Stroke* stroke) override;

  void drawPicture(std::shared_ptr<Picture> picture, const MCState& state) override;

  void drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                 const MCState& state, const Brush& brush) override;

 private:
  BlockAllocator blockAllocator = {};
  std::vector<PlacementPtr<PictureRecord>> records = {};
  size_t drawCount = 0;
  MCState lastState = {};
  Brush lastBrush = {};
  Stroke lastStroke = {};
  bool hasStroke = false;

  void recordState(const MCState& state);
  void recordBrush(const Brush& brush);
  void recordStroke(const Stroke& stroke);
  void recordAll(const MCState& state, const Brush& brush, const Stroke* stroke = nullptr);
};
}  // namespace tgfx

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/Matrix.h"

namespace tgfx {
class Record;
class Canvas;
class DrawContext;
class MCState;
class Image;
class Fill;
class BlockData;
template <typename T>
class PlacementPtr;

/**
 * The Picture class captures the drawing commands made on a Canvas, which can be replayed later.
 * The Picture object is thread-safe and immutable once created. Pictures can be created by a
 * Recorder or loaded from serialized data.
 */
class Picture {
 public:
  ~Picture();

  /**
   * Returns true if the Picture contains any drawing commands that fill an unbounded (infinite)
   * area. For example, drawing a Path with an inverse fill type or drawing a Paint to cover the
   * entire canvas.
   */
  bool hasUnboundedFill() const {
    return _hasUnboundedFill;
  }

  /**
   * Returns the bounding box of the Picture when drawn with the given Matrix. Since the Picture
   * may contain shape or glyph drawing commands whose outlines can change with different scale
   * factors, it's best to use the final drawing matrix to calculate the bounds for accuracy.
   * Note that the bounds only include the combined geometry of each drawing command, but some
   * commands may draw outside these bounds. Use the hasUnboundedFill() method to check for this.
   */
  Rect getBounds(const Matrix* matrix = nullptr) const;

  /**
   * Replays the drawing commands on the specified canvas. In the case that the commands are
   * recorded, each command in the Picture is sent separately to canvas. To add a single command to
   * draw the Picture to a canvas, call Canvas::drawPicture() instead.
   */
  void playback(Canvas* canvas) const;

 private:
  std::shared_ptr<BlockData> blockData = nullptr;
  std::vector<PlacementPtr<Record>> records;
  size_t drawCount = 0;
  bool _hasUnboundedFill = false;

  Picture(std::shared_ptr<BlockData> data, std::vector<PlacementPtr<Record>> records,
          size_t drawCount);

  void playback(DrawContext* drawContext, const MCState& state) const;

  std::shared_ptr<Image> asImage(Point* offset, const Matrix* matrix = nullptr,
                                 const ISize* clipSize = nullptr) const;

  const Record* firstDrawRecord(MCState* state = nullptr, Fill* fill = nullptr) const;

  friend class MeasureContext;
  friend class RenderContext;
  friend class RecordingContext;
  friend class SVGExportContext;
  friend class Image;
  friend class PictureImage;
  friend class Canvas;
};
}  // namespace tgfx

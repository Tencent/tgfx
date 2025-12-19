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

#include <atomic>
#include "tgfx/core/Matrix.h"

namespace tgfx {
class PictureRecord;
class Canvas;
class DrawContext;
class MCState;
class Image;
class Brush;
class BlockBuffer;
template <typename T>
class PlacementPtr;

/**
 * The Picture class captures the drawing commands made on a Canvas, which can be replayed later.
 * The Picture object is thread-safe and immutable once created. Pictures can be created by a
 * PictureRecorder or loaded from serialized data.
 */
class Picture {
 public:
  /**
   * AbortCallback is an abstract class that allows interrupting the playback of a Picture.
   * Subclasses can override the abort() method to determine whether to stop playback.
   */
  class AbortCallback {
   public:
    virtual ~AbortCallback() = default;

    /**
     * Called before each drawing command during playback. If this returns true, the playback will
     * be aborted immediately.
     */
    virtual bool abort() = 0;
  };

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
   * Returns the bounding box of the Picture. Note that the bounds only include the combined
   * geometry of each drawing command, but some commands may draw outside these bounds. Use the
   * hasUnboundedFill() method to check for this.
   */
  Rect getBounds() const;

  /**
   * Replays the drawing commands on the specified canvas. In the case that the commands are
   * recorded, each command in the Picture is sent separately to canvas. To add a single command to
   * draw the Picture to a canvas, call Canvas::drawPicture() instead.
   * @param canvas The receiver of drawing commands.
   * @param callback Optional callback that can abort playback. If callback->abort() returns true,
   *                 playback stops immediately.
   */
  void playback(Canvas* canvas, AbortCallback* callback = nullptr) const;

 private:
  std::unique_ptr<BlockBuffer> blockBuffer;
  std::vector<PlacementPtr<PictureRecord>> records;
  mutable std::atomic<Rect*> bounds = {nullptr};
  size_t drawCount = 0;
  bool _hasUnboundedFill = false;

  Picture(std::unique_ptr<BlockBuffer> buffer, std::vector<PlacementPtr<PictureRecord>> records,
          size_t drawCount);

  void playback(DrawContext* drawContext, const MCState& state,
                AbortCallback* callback = nullptr) const;

  std::shared_ptr<Image> asImage(Point* offset, const Matrix* matrix = nullptr,
                                 const ISize* clipSize = nullptr) const;

  const PictureRecord* getFirstDrawRecord(MCState* state = nullptr, Brush* brush = nullptr,
                                          bool* hasStroke = nullptr) const;

  friend class MeasureContext;
  friend class RenderContext;
  friend class PictureContext;
  friend class SVGExportContext;
  friend class Image;
  friend class PictureImage;
  friend class Canvas;
  friend class PDFExportContext;
  friend class ContourContext;
};
}  // namespace tgfx

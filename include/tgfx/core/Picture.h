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
#include "tgfx/core/Path.h"

namespace tgfx {
class Record;
class Canvas;
class DrawContext;
class MCState;
class Image;

/**
 * The Picture class captures the drawing commands made on a Canvas, which can be replayed later.
 * The Picture object is thread-safe and immutable once created. Pictures can be created by a
 * Recorder or loaded from serialized data.
 */
class Picture {
 public:
  ~Picture();

  /**
   * Returns the bounding box of the Picture when drawn with the given Matrix.
   */
  Rect getBounds(const Matrix& matrix = Matrix::I()) const;

  /**
   * Replays the drawing commands on the specified canvas. In the case that the commands are
   * recorded, each command in the Picture is sent separately to canvas. To add a single command to
   * draw Picture to recording canvas, call Canvas::drawPicture() instead.
   */
  void playback(Canvas* canvas) const;

 private:
  std::vector<Record*> records = {};

  explicit Picture(std::vector<Record*> records);

  void playback(DrawContext* drawContext, const MCState& state) const;

  std::shared_ptr<Image> asImage(int width, int height, const Matrix* matrix) const;

  friend class MeasureContext;
  friend class RenderContext;
  friend class RecordingContext;
  friend class PictureImage;
  friend class Canvas;
};
}  // namespace tgfx

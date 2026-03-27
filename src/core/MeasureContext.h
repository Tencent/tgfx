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

#include "core/DrawContext.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {
class MeasureContext : public DrawContext {
 public:
  Rect getBounds() const {
    return bounds;
  }

  void drawFill(const Brush& brush) override;

  void drawRect(const Rect& rect, const Matrix& matrix, const ClipStack& clip, const Brush& brush,
                const Stroke* stroke) override;

  void drawRRect(const RRect& rRect, const Matrix& matrix, const ClipStack& clip,
                 const Brush& brush, const Stroke* stroke) override;

  void drawPath(const Path& path, const Matrix& matrix, const ClipStack& clip,
                const Brush& brush) override;

  void drawShape(std::shared_ptr<Shape> shape, const Matrix& matrix, const ClipStack& clip,
                 const Brush& brush, const Stroke* stroke) override;

  void drawMesh(std::shared_ptr<Mesh> mesh, const Matrix& matrix, const ClipStack& clip,
                const Brush& brush) override;

  void drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                 const Matrix& matrix, const ClipStack& clip, const Brush& brush) override;

  void drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect, const Rect& dstRect,
                     const SamplingOptions& sampling, const Matrix& matrix, const ClipStack& clip,
                     const Brush& brush, SrcRectConstraint constraint) override;

  void drawTextBlob(std::shared_ptr<TextBlob> textBlob, const Matrix& matrix, const ClipStack& clip,
                    const Brush& brush, const Stroke* stroke) override;

  void drawPicture(std::shared_ptr<Picture> picture, const Matrix& matrix,
                   const ClipStack& clip) override;

  void drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                 const Matrix& matrix, const ClipStack& clip, const Brush& brush) override;

 private:
  Rect bounds = {};

  void addLocalBounds(const Matrix& matrix, const ClipStack& clip, const Rect& localBounds,
                      bool unbounded = false);
  void addDeviceBounds(const ClipStack& clip, const Rect& deviceBounds, bool unbounded = false);
};
}  // namespace tgfx

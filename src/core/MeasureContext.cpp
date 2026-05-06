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

#include "MeasureContext.h"
#include "core/utils/Log.h"
#include "core/utils/StrokeUtils.h"
#include "tgfx/core/Shape.h"
#include "utils/MathExtra.h"

namespace tgfx {
void MeasureContext::drawFill(const Brush&) {
}

void MeasureContext::drawRect(const Rect& rect, const Matrix& matrix, const ClipStack& clip,
                              const Brush&, const Stroke* stroke) {
  if (stroke == nullptr) {
    addLocalBounds(matrix, clip, rect);
    return;
  }
  auto localBounds = rect;
  ApplyStrokeToBounds(*stroke, &localBounds, matrix);
  addLocalBounds(matrix, clip, localBounds);
}

void MeasureContext::drawRRect(const RRect& rRect, const Matrix& matrix, const ClipStack& clip,
                               const Brush&, const Stroke* stroke) {
  auto rect = rRect.rect();
  if (stroke) {
    ApplyStrokeToBounds(*stroke, &rect, matrix);
  }
  addLocalBounds(matrix, clip, rect, false);
}

void MeasureContext::drawPath(const Path& path, const Matrix& matrix, const ClipStack& clip,
                              const Brush&) {
  auto localBounds = path.getBounds();
  addLocalBounds(matrix, clip, localBounds, path.isInverseFillType());
}

void MeasureContext::drawShape(std::shared_ptr<Shape> shape, const Matrix& matrix,
                               const ClipStack& clip, const Brush&, const Stroke* stroke) {
  DEBUG_ASSERT(shape != nullptr);
  auto localBounds = shape->getBounds();
  if (stroke) {
    ApplyStrokeToBounds(*stroke, &localBounds, matrix, true);
  }
  addLocalBounds(matrix, clip, localBounds, shape->isInverseFillType());
}

void MeasureContext::drawMesh(std::shared_ptr<Mesh> mesh, const Matrix& matrix,
                              const ClipStack& clip, const Brush&) {
  DEBUG_ASSERT(mesh != nullptr);
  addLocalBounds(matrix, clip, mesh->bounds());
}

void MeasureContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions&,
                               const Matrix& matrix, const ClipStack& clip, const Brush&) {
  addLocalBounds(matrix, clip, Rect::MakeWH(image->width(), image->height()));
}

void MeasureContext::drawImageRect(std::shared_ptr<Image>, const Rect&, const Rect& dstRect,
                                   const SamplingOptions&, const Matrix& matrix,
                                   const ClipStack& clip, const Brush&, SrcRectConstraint) {
  addLocalBounds(matrix, clip, dstRect);
}

void MeasureContext::drawTextBlob(std::shared_ptr<TextBlob> textBlob, const Matrix& matrix,
                                  const ClipStack& clip, const Brush&, const Stroke* stroke) {
  DEBUG_ASSERT(textBlob != nullptr);
  auto localBounds = textBlob->getBounds();
  if (stroke) {
    ApplyStrokeToBounds(*stroke, &localBounds, matrix);
  }
  addLocalBounds(matrix, clip, localBounds);
}

void MeasureContext::drawLayer(std::shared_ptr<Picture> picture,
                               std::shared_ptr<ImageFilter> imageFilter, const Matrix& matrix,
                               const ClipStack& clip, const Brush&) {
  DEBUG_ASSERT(picture != nullptr);
  auto localBounds = picture->getBounds();
  if (imageFilter) {
    localBounds = imageFilter->filterBounds(localBounds);
  }
  addLocalBounds(matrix, clip, localBounds, picture->hasUnboundedFill());
}

void MeasureContext::drawPicture(std::shared_ptr<Picture> picture, const Matrix& matrix,
                                 const ClipStack& clip) {
  DEBUG_ASSERT(picture != nullptr);
  picture->playback(this, matrix, clip);
}

void MeasureContext::addLocalBounds(const Matrix& matrix, const ClipStack& clip,
                                    const Rect& localBounds, bool unbounded) {
  auto deviceBounds = matrix.mapRect(localBounds);
  addDeviceBounds(clip, deviceBounds, unbounded);
}

void MeasureContext::addDeviceBounds(const ClipStack& clip, const Rect& deviceBounds,
                                     bool unbounded) {
  if (clip.state() == ClipState::WideOpen) {
    bounds.join(deviceBounds);
    return;
  }
  if (clip.state() == ClipState::Empty) {
    return;
  }
  auto intersectBounds = clip.bounds();
  if (!unbounded && !intersectBounds.intersect(deviceBounds)) {
    return;
  }
  bounds.join(intersectBounds);
}
}  // namespace tgfx

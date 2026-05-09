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

#include "MaskContext.h"
#include "core/utils/Log.h"
#include "tgfx/core/Canvas.h"

namespace tgfx {

/**
 * Maximum number of draw operations allowed for mask path extraction. When a picture's drawCount
 * exceeds this threshold, MaskContext skips path extraction because the cost of multiple PathOp
 * Union operations grows super-linearly and the resulting complex path becomes expensive to
 * rasterize, making the MaskFilter fallback more efficient.
 */
static constexpr size_t MaxMaskPathDrawCount = 30;

/**
 * Hard cap on the verb count of the accumulated mask path. PathOp::Union cost is dominated by the
 * size of its left operand, so when the running mask grows past this size each additional Union
 * gets prohibitively expensive — drawCount alone cannot bound it because a single record may
 * contribute thousands of verbs (dense SVG paths, stroke-applied curves, etc). Once exceeded,
 * abort path extraction so the caller falls back to the MaskFilter shader path.
 */
static constexpr int MaxAccumulatedMaskVerbs = 2000;

bool MaskContext::GetMaskPath(const std::shared_ptr<Picture>& picture, Path* result) {
  if (picture == nullptr || result == nullptr) {
    return false;
  }
  if (picture->drawCount > MaxMaskPathDrawCount) {
    return false;
  }
  MaskContext maskContext;
  picture->playback(&maskContext, Matrix::I(), ClipStack(), &maskContext);
  return maskContext.finish(result);
}

void MaskContext::addPath(Path path, const Matrix& matrix, const ClipStack& clip,
                          const Stroke* stroke) {
  if (_aborted) {
    return;
  }
  // Empty clip means this draw is completely clipped out, skip this record.
  if (clip.state() == ClipState::Empty) {
    return;
  }
  PathRecord record = {};
  record.path = std::move(path);
  record.matrix = matrix;
  record.clip = clip;
  if (stroke) {
    record.stroke = *stroke;
    record.hasStroke = true;
  }
  _records.push_back(std::move(record));
}

bool MaskContext::finish(Path* result) {
  if (_aborted || result == nullptr) {
    return false;
  }
  Path maskPath = {};
  for (auto& record : _records) {
    if (record.hasStroke && !record.stroke.applyToPath(&record.path)) {
      return false;
    }
    record.path.transform(record.matrix);
    // Pre-Union circuit breaker: PathOp::Union cost is dominated by the combined verb count of
    // both operands, and grows super-linearly. Cut off BEFORE paying the next Union when the
    // projected size already exceeds the cap. The caller falls back to the MaskFilter shader
    // path, which scales much better with complex masks.
    auto projected = maskPath.countVerbs() + record.path.countVerbs();
    if (record.clip.state() == ClipState::Complex ||
        record.clip.state() == ClipState::Rect) {
      // Complex / partial-rect clip will run an extra Intersect on the clip path, which inflates
      // the right operand. Account for the clip path verbs too.
      projected += record.clip.getClipPath().countVerbs();
    }
    if (projected > MaxAccumulatedMaskVerbs) {
      return false;
    }
    switch (record.clip.state()) {
      case ClipState::Empty:
        // Already filtered out in addPath().
        DEBUG_ASSERT(false);
        break;
      case ClipState::WideOpen:
        maskPath.addPath(record.path, PathOp::Union);
        break;
      case ClipState::Rect: {
        auto clipBounds = record.clip.bounds();
        auto pathBounds = record.path.getBounds();
        if (!Rect::Intersects(clipBounds, pathBounds)) {
          break;
        }
        if (clipBounds.contains(pathBounds)) {
          maskPath.addPath(record.path, PathOp::Union);
          break;
        }
        // Partial intersection, use PathOp.
        [[fallthrough]];
      }
      case ClipState::Complex: {
        // Anti-aliasing attributes of clip elements can be safely discarded for mask generation.
        auto clippedPath = record.clip.getClipPath();
        clippedPath.addPath(record.path, PathOp::Intersect);
        maskPath.addPath(clippedPath, PathOp::Union);
        break;
      }
    }
  }
  *result = std::move(maskPath);
  return true;
}

void MaskContext::drawFill(const Brush& brush) {
  if (!brush.isOpaque()) {
    _aborted = true;
    return;
  }
  Path shapePath = {};
  shapePath.toggleInverseFillType();
  addPath(std::move(shapePath), Matrix::I(), ClipStack(), nullptr);
}

void MaskContext::drawRect(const Rect& rect, const Matrix& matrix, const ClipStack& clip,
                           const Brush& brush, const Stroke* stroke) {
  if (!brush.isOpaque()) {
    _aborted = true;
    return;
  }
  Path shapePath = {};
  shapePath.addRect(rect);
  addPath(std::move(shapePath), matrix, clip, stroke);
}

void MaskContext::drawRRect(const RRect& rRect, const Matrix& matrix, const ClipStack& clip,
                            const Brush& brush, const Stroke* stroke) {
  if (!brush.isOpaque()) {
    _aborted = true;
    return;
  }
  Path shapePath = {};
  shapePath.addRRect(rRect);
  addPath(std::move(shapePath), matrix, clip, stroke);
}

void MaskContext::drawPath(const Path& path, const Matrix& matrix, const ClipStack& clip,
                           const Brush& brush) {
  if (!brush.isOpaque()) {
    _aborted = true;
    return;
  }
  addPath(path, matrix, clip, nullptr);
}

void MaskContext::drawShape(std::shared_ptr<Shape>, const Matrix&, const ClipStack&, const Brush&,
                            const Stroke*) {
  // Avoid getting path directly due to performance concerns.
  _aborted = true;
}

void MaskContext::drawMesh(std::shared_ptr<Mesh>, const Matrix&, const ClipStack&, const Brush&) {
  _aborted = true;
}

void MaskContext::drawImage(std::shared_ptr<Image>, const SamplingOptions&, const Matrix&,
                            const ClipStack&, const Brush&) {
  _aborted = true;
}

void MaskContext::drawImageRect(std::shared_ptr<Image>, const Rect&, const Rect&,
                                const SamplingOptions&, const Matrix&, const ClipStack&,
                                const Brush&, SrcRectConstraint) {
  _aborted = true;
}

void MaskContext::drawTextBlob(std::shared_ptr<TextBlob>, const Matrix&, const ClipStack&,
                               const Brush&, const Stroke*) {
  // Avoid getting path directly due to performance concerns.
  _aborted = true;
}

void MaskContext::drawPicture(std::shared_ptr<Picture> picture, const Matrix& matrix,
                              const ClipStack& clip) {
  if (_aborted || picture == nullptr) {
    return;
  }
  // Apply the same drawCount budget as the top-level entry: nested pictures with too many ops
  // would otherwise bypass the outer guard and blow up the union accumulation in finish().
  if (picture->drawCount > MaxMaskPathDrawCount) {
    _aborted = true;
    return;
  }
  MaskContext subContext;
  picture->playback(&subContext, matrix, clip, &subContext);
  Path subPath = {};
  if (!subContext.finish(&subPath)) {
    _aborted = true;
    return;
  }
  addPath(std::move(subPath), Matrix::I(), ClipStack(), nullptr);
}

void MaskContext::drawLayer(std::shared_ptr<Picture>, std::shared_ptr<ImageFilter>, const Matrix&,
                            const ClipStack&, const Brush&) {
  _aborted = true;
}

}  // namespace tgfx

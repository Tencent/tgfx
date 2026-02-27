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
#include "core/MCState.h"
#include "tgfx/core/Canvas.h"

namespace tgfx {

bool MaskContext::GetMaskPath(const std::shared_ptr<Picture>& picture, Path* result) {
  if (picture == nullptr || result == nullptr) {
    return false;
  }
  MaskContext maskContext;
  picture->playback(&maskContext, MCState(Matrix::I()), &maskContext);
  return maskContext.finish(result);
}

void MaskContext::addPath(Path path, const MCState& state, const Stroke* stroke) {
  if (_aborted) {
    return;
  }
  auto& clip = state.clip;
  if (clip.isEmpty() && !clip.isInverseFillType()) {
    return;
  }
  PathRecord record = {};
  record.path = std::move(path);
  record.state = state;
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
    record.path.transform(record.state.matrix);
    Path clippedPath = record.state.clip.path;
    clippedPath.addPath(record.path, PathOp::Intersect);
    maskPath.addPath(clippedPath);
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
  addPath(std::move(shapePath), MCState(Matrix::I()), nullptr);
}

void MaskContext::drawRect(const Rect& rect, const MCState& state, const Brush& brush,
                           const Stroke* stroke) {
  if (!brush.isOpaque()) {
    _aborted = true;
    return;
  }
  Path shapePath = {};
  shapePath.addRect(rect);
  addPath(std::move(shapePath), state, stroke);
}

void MaskContext::drawRRect(const RRect& rRect, const MCState& state, const Brush& brush,
                            const Stroke* stroke) {
  if (!brush.isOpaque()) {
    _aborted = true;
    return;
  }
  Path shapePath = {};
  shapePath.addRRect(rRect);
  addPath(std::move(shapePath), state, stroke);
}

void MaskContext::drawPath(const Path& path, const MCState& state, const Brush& brush) {
  if (!brush.isOpaque()) {
    _aborted = true;
    return;
  }
  addPath(path, state, nullptr);
}

void MaskContext::drawShape(std::shared_ptr<Shape>, const MCState&, const Brush&, const Stroke*) {
  // Avoid getting path directly due to performance concerns.
  _aborted = true;
}

void MaskContext::drawImage(std::shared_ptr<Image>, const SamplingOptions&, const MCState&,
                            const Brush&) {
  _aborted = true;
}

void MaskContext::drawImageRect(std::shared_ptr<Image>, const Rect&, const Rect&,
                                const SamplingOptions&, const MCState&, const Brush&,
                                SrcRectConstraint) {
  _aborted = true;
}

void MaskContext::drawTextBlob(std::shared_ptr<TextBlob>, const MCState&, const Brush&,
                               const Stroke*) {
  // Avoid getting path directly due to performance concerns.
  _aborted = true;
}

void MaskContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  if (_aborted || picture == nullptr) {
    return;
  }
  MaskContext subContext;
  picture->playback(&subContext, state, &subContext);
  Path subPath = {};
  if (!subContext.finish(&subPath)) {
    _aborted = true;
    return;
  }
  addPath(std::move(subPath), MCState(Matrix::I()), nullptr);
}

void MaskContext::drawLayer(std::shared_ptr<Picture>, std::shared_ptr<ImageFilter>, const MCState&,
                            const Brush&) {
  _aborted = true;
}

}  // namespace tgfx

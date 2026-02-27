/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") {} you may not use this file except
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

#include "core/PictureContext.h"
#include "utils/Log.h"
#include "utils/RectToRectMatrix.h"

namespace tgfx {
/**
 * This const is used to strike a balance between the speed of referencing a sub-picture into a
 * parent picture and the playback cost of recursing into the sub-picture to access its actual
 * operations. Currently, it is set to a conservatively small value. However, based on measurements
 * and other factors such as the type of operations contained, this value may need to be adjusted.
 */
constexpr int MaxPictureDrawsToUnrollInsteadOfReference = 1;

PictureContext::~PictureContext() {
  // make sure the records are cleared before the blockAllocator is destroyed.
  records.clear();
}

void PictureContext::clear() {
  records.clear();
  blockAllocator.clear();
  lastState = {};
  lastBrush = {};
  lastStroke = {};
  hasStroke = false;
  drawCount = 0;
}

std::shared_ptr<Picture> PictureContext::finishRecordingAsPicture() {
  if (records.empty()) {
    return nullptr;
  }
  auto blockBuffer = blockAllocator.release();
  if (blockBuffer == nullptr) {
    return nullptr;
  }
  std::shared_ptr<Picture> picture =
      std::shared_ptr<Picture>(new Picture(std::move(blockBuffer), std::move(records), drawCount));
  lastState = {};
  lastBrush = {};
  lastStroke = {};
  hasStroke = false;
  drawCount = 0;
  return picture;
}

void PictureContext::drawFill(const Brush& brush) {
  if (brush.isOpaque()) {
    // The clip is wide open, and the brush is opaque, so we can discard all previous records as
    // they are now invisible.
    clear();
  }
  if (brush.color.alpha > 0.0f) {
    recordAll({}, brush);
    auto record = blockAllocator.make<DrawFill>();
    records.emplace_back(std::move(record));
    drawCount++;
  }
}

void PictureContext::drawRect(const Rect& rect, const MCState& state, const Brush& brush,
                              const Stroke* stroke) {
  recordAll(state, brush, stroke);
  auto record = blockAllocator.make<DrawRect>(rect);
  records.emplace_back(std::move(record));
  drawCount++;
}

void PictureContext::drawRRect(const RRect& rRect, const MCState& state, const Brush& brush,
                               const Stroke* stroke) {
  recordAll(state, brush, stroke);
  auto record = blockAllocator.make<DrawRRect>(rRect);
  records.emplace_back(std::move(record));
  drawCount++;
}

void PictureContext::drawPath(const Path& path, const MCState& state, const Brush& brush) {
  recordAll(state, brush);
  auto record = blockAllocator.make<DrawPath>(path);
  records.emplace_back(std::move(record));
  drawCount++;
}

void PictureContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                               const Brush& brush, const Stroke* stroke) {
  DEBUG_ASSERT(shape != nullptr);
  recordAll(state, brush, stroke);
  auto record = blockAllocator.make<DrawShape>(std::move(shape));
  records.emplace_back(std::move(record));
  drawCount++;
}

void PictureContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                               const MCState& state, const Brush& brush) {
  DEBUG_ASSERT(image != nullptr);
  recordAll(state, brush);
  PlacementPtr<PictureRecord> record = nullptr;
  record = blockAllocator.make<DrawImage>(std::move(image), sampling);
  records.emplace_back(std::move(record));
  drawCount++;
}

void PictureContext::drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect,
                                   const Rect& dstRect, const SamplingOptions& sampling,
                                   const MCState& state, const Brush& brush,
                                   SrcRectConstraint constraint) {
  DEBUG_ASSERT(image != nullptr);
  auto newState = state;
  auto newBrush = brush;
  bool needDstRect = true;
  if (srcRect.width() == dstRect.width() && srcRect.height() == dstRect.height()) {
    auto viewMatrix = MakeRectToRectMatrix(srcRect, dstRect);
    newState.matrix.preConcat(viewMatrix);
    Matrix brushMatrix = Matrix::I();
    viewMatrix.invert(&brushMatrix);
    newBrush = newBrush.makeWithMatrix(brushMatrix);
    needDstRect = false;
  }
  recordAll(newState, newBrush);
  auto imageRect = Rect::MakeWH(image->width(), image->height());
  PlacementPtr<PictureRecord> record = nullptr;
  if (srcRect == imageRect && !needDstRect) {
    record = blockAllocator.make<DrawImage>(std::move(image), sampling);
  } else if (!needDstRect) {
    record = blockAllocator.make<DrawImageRect>(std::move(image), srcRect, sampling, constraint);
  } else {
    record = blockAllocator.make<DrawImageRectToRect>(std::move(image), srcRect, dstRect, sampling,
                                                      constraint);
  }
  records.emplace_back(std::move(record));
  drawCount++;
}

void PictureContext::drawTextBlob(std::shared_ptr<TextBlob> textBlob, const MCState& state,
                                  const Brush& brush, const Stroke* stroke) {
  DEBUG_ASSERT(textBlob != nullptr);
  recordAll(state, brush, stroke);
  auto record = blockAllocator.make<DrawTextBlob>(std::move(textBlob));
  records.emplace_back(std::move(record));
  drawCount++;
}

void PictureContext::drawLayer(std::shared_ptr<Picture> picture,
                               std::shared_ptr<ImageFilter> filter, const MCState& state,
                               const Brush& brush) {
  DEBUG_ASSERT(picture != nullptr);
  recordAll(state, brush);
  auto record = blockAllocator.make<DrawLayer>(std::move(picture), std::move(filter));
  records.emplace_back(std::move(record));
  drawCount++;
}

void PictureContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  DEBUG_ASSERT(picture != nullptr);
  if (picture->drawCount > MaxPictureDrawsToUnrollInsteadOfReference) {
    recordState(state);
    drawCount += picture->drawCount;
    auto record = blockAllocator.make<DrawPicture>(std::move(picture));
    records.emplace_back(std::move(record));
  } else {
    picture->playback(this, state);
  }
}

static bool CompareBrush(const Brush& a, const Brush& b) {
  // Ignore the color differences.
  return a.antiAlias == b.antiAlias && a.blendMode == b.blendMode && a.shader == b.shader &&
         a.maskFilter == b.maskFilter && a.colorFilter == b.colorFilter;
}

void PictureContext::recordState(const MCState& state) {
  if (lastState.matrix != state.matrix) {
    auto record = blockAllocator.make<SetMatrix>(state.matrix);
    records.emplace_back(std::move(record));
    lastState.matrix = state.matrix;
  }
  if (lastState.clip != state.clip) {
    auto record = blockAllocator.make<SetClip>(state.clip);
    records.emplace_back(std::move(record));
    lastState.clip = state.clip;
  }
}

void PictureContext::recordBrush(const Brush& brush) {
  if (!CompareBrush(lastBrush, brush)) {
    auto record = blockAllocator.make<SetBrush>(brush);
    records.emplace_back(std::move(record));
    lastBrush = brush;
  } else if (lastBrush.color != brush.color) {
    auto record = blockAllocator.make<SetColor>(brush.color);
    records.emplace_back(std::move(record));
    lastBrush.color = brush.color;
  }
}

void PictureContext::recordStroke(const Stroke& stroke) {
  if (stroke.cap != lastStroke.cap || stroke.join != lastStroke.join ||
      stroke.miterLimit != lastStroke.miterLimit) {
    auto record = blockAllocator.make<SetStroke>(stroke);
    records.emplace_back(std::move(record));
    lastStroke = stroke;
  } else if (stroke.width != lastStroke.width) {
    auto record = blockAllocator.make<SetStrokeWidth>(stroke.width);
    records.emplace_back(std::move(record));
    lastStroke.width = stroke.width;
  } else if (!hasStroke) {
    auto record = blockAllocator.make<SetHasStroke>(true);
    records.emplace_back(std::move(record));
  }
  hasStroke = true;
}

void PictureContext::recordAll(const MCState& state, const Brush& brush, const Stroke* stroke) {
  recordState(state);
  recordBrush(brush);
  if (stroke) {
    recordStroke(*stroke);
  } else if (hasStroke) {
    auto record = blockAllocator.make<SetHasStroke>(false);
    records.emplace_back(std::move(record));
    hasStroke = false;
  }
}
}  // namespace tgfx
